#pragma once

// Data-analysis layer for Celidae's business views.
//
// Celidae's older diagrams answer structural questions ("what extends what",
// "who calls whom"). Those are code-shaped, and an IDE already draws them.
// The views built on this header answer questions about the *data* a program
// declares: how values are distributed, which records are unusual, which
// fields move together, which records group into segments.
//
// Nothing here executes the program. Every number is derived from the literal
// values Celidae's parser already saw in fact declarations, so the analysis is
// static in exactly the same sense the rest of Celidae is.
//
// The linear algebra (PCA, correlation) runs on Eigen, vendored header-only in
// third_party/Eigen and reached via the -isystem third_party the build scripts
// already pass. Clustering, histogram binning and robust outlier detection are
// small enough that a dependency would cost more than it saves, and they are
// implemented here against the same matrix type.

#include <cstddef>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace Felidae::Celidae {

// One fact record's literal field values, e.g. {name: "Ada", born: "1815-12-10"}.
using FactRecordValues = std::map<std::string, std::string>;

struct FactProfile {
    std::size_t records = 0;
    std::map<std::string, std::size_t> fields;
    // Literal values per record, for the diagram types that plot values rather
    // than structure. Capped: a large fact set would otherwise retain every
    // literal in the program.
    std::vector<FactRecordValues> samples;
};

// Enough records to draw a representative distribution without letting a
// generated fact file balloon memory. Views that sample say so in their
// output rather than presenting a partial answer as a complete one.
constexpr std::size_t kMaxFactSamples = 500;

// ---------------------------------------------------------------------------
// Literal value inspection
// ---------------------------------------------------------------------------

// True when the whole literal parses as a number.
bool looksNumeric(const std::string& value);

// Recognises the date shapes a fact field realistically carries: an ISO date
// or timestamp, or a bare year. Deliberately strict - guessing wrong would
// scatter unrelated facts along a time axis.
bool looksLikeDate(const std::string& value);

// A date literal as a day number (days since 1970-01-01), so dates and
// numbers share one ordering scale. Returns false when the value is not a
// date this recognises.
bool dateToDayNumber(const std::string& value, double& out);

// The ordering scale a value belongs to, used to decide which chart a field
// can support at all.
enum class FieldType {
    Empty,        // no literal values were captured
    Numeric,      // orderable, measurable: counts, scores, amounts
    Date,         // orderable in time
    Categorical,  // a label: groupable but not orderable
    Identifier    // categorical with near-unique values; groups tell you nothing
};

const char* fieldTypeName(FieldType type);

// ---------------------------------------------------------------------------
// Per-field statistics
// ---------------------------------------------------------------------------

struct FieldStats {
    std::string name;
    FieldType type = FieldType::Empty;
    std::size_t present = 0;   // records carrying a literal for this field
    std::size_t missing = 0;   // sampled records without one
    std::size_t distinct = 0;

    // Numeric/date fields. Dates are summarised on their day-number scale.
    double min = 0;
    double max = 0;
    double mean = 0;
    double median = 0;
    double stddev = 0;
    // Median absolute deviation: the spread measure the outlier test uses,
    // because a single extreme value inflates stddev enough to hide itself.
    double mad = 0;
    double skewness = 0;

    // Sample indexes whose robust z-score exceeds the outlier threshold.
    std::vector<std::size_t> outliers;

    // Categorical fields: most frequent values first, capped.
    std::vector<std::pair<std::string, std::size_t>> topValues;
    // Shannon entropy normalised to 0..1. 0 means every record shares one
    // value (a useless grouping); 1 means perfectly even spread.
    double entropy = 0;
};

// Values further than this many robust z-scores from the median are reported
// as outliers. 3.5 is the conventional modified-z-score cutoff.
constexpr double kOutlierZ = 3.5;

// At most this many categories are listed per categorical field; beyond it a
// bar chart stops being readable and the field is better described by its
// cardinality.
constexpr std::size_t kMaxCategories = 12;

// A field whose distinct-value count exceeds this fraction of its present
// count is treated as an identifier: grouping by it produces one record per
// group, which no business view benefits from.
constexpr double kIdentifierDistinctRatio = 0.92;

std::vector<FieldStats> profileFields(const FactProfile& profile);

// ---------------------------------------------------------------------------
// Histogram binning
// ---------------------------------------------------------------------------

struct Histogram {
    std::string field;
    std::vector<double> edges;        // size == counts.size() + 1
    std::vector<std::size_t> counts;
    double binWidth = 0;
    bool fromDates = false;           // edges are day numbers, not raw values
};

// Freedman-Diaconis binning: bin width from the interquartile range, which
// adapts to spread instead of assuming a bin count. Falls back to Sturges'
// rule when the IQR is zero (a heavily tied distribution), and clamps the bin
// count so a wide-ranging field cannot produce thousands of bars.
Histogram buildHistogram(const FactProfile& profile, const FieldStats& field);

// ---------------------------------------------------------------------------
// Feature matrix
// ---------------------------------------------------------------------------

// Numeric encoding of a fact type's records: numeric and date fields pass
// through on their own scale, low-cardinality categoricals are one-hot
// expanded, identifiers and empty fields are dropped. Missing values are
// mean-imputed so a record with a gap still contributes its other fields
// rather than being discarded.
struct FeatureMatrix {
    std::vector<std::string> columns;
    std::vector<std::string> rowLabels;
    // Index into FactProfile::samples for each row, so a result can be traced
    // back to the record it came from.
    std::vector<std::size_t> rowSamples;
    std::vector<std::vector<double>> rows;

    std::size_t rowCount() const { return rows.size(); }
    std::size_t columnCount() const { return columns.size(); }
};

// Only categoricals with at most this many levels are one-hot encoded; more
// would swamp the numeric columns with sparse indicator dimensions.
constexpr std::size_t kMaxOneHotLevels = 8;

FeatureMatrix buildFeatureMatrix(const FactProfile& profile,
                                 const std::vector<FieldStats>& fields);

// ---------------------------------------------------------------------------
// PCA + clustering
// ---------------------------------------------------------------------------

struct ComponentLoading {
    std::string column;
    double weight = 0;  // signed contribution to the component
};

struct ClusterResult {
    bool valid = false;             // false when there was too little data
    std::string reason;             // why, when !valid

    // Principal component projection, one 2D point per matrix row.
    std::vector<double> x;
    std::vector<double> y;
    // Fraction of total variance each of the two components explains.
    double explainedX = 0;
    double explainedY = 0;
    // Which original fields drive each axis, strongest first.
    std::vector<ComponentLoading> driversX;
    std::vector<ComponentLoading> driversY;

    int k = 0;
    std::vector<int> assignment;    // cluster index per row
    std::vector<std::size_t> clusterSizes;
    // Mean silhouette over all rows: how well-separated the segments are.
    // Below ~0.25 the clustering is not saying much, and the view says so.
    double silhouette = 0;
    // Per cluster, the columns whose mean deviates most from the global mean -
    // what actually distinguishes this segment.
    std::vector<std::vector<ComponentLoading>> clusterDrivers;
};

// PCA and k-means need more rows than dimensions to mean anything.
constexpr std::size_t kMinClusterRows = 6;
// k is chosen by silhouette over this range, so the view is not forced to
// invent a fixed number of segments.
constexpr int kMaxClusters = 6;

ClusterResult clusterRecords(const FeatureMatrix& matrix);

// ---------------------------------------------------------------------------
// Correlation
// ---------------------------------------------------------------------------

struct CorrelationMatrix {
    std::vector<std::string> columns;
    std::vector<std::vector<double>> values;  // Pearson r, square, symmetric
    // Off-diagonal pairs with |r| above the reporting threshold, strongest
    // first: the pairs actually worth a reader's attention.
    struct Pair {
        std::string a;
        std::string b;
        double r = 0;
    };
    std::vector<Pair> notable;
};

constexpr double kNotableCorrelation = 0.5;

// Below this many records, a correlation is arithmetic rather than evidence:
// four points can produce |r| = 1.0 by coincidence. The matrix is still
// computed and drawn - only the "worth telling the reader about" list is
// gated, because that list is what gets stated as a finding in prose.
constexpr std::size_t kMinCorrelationRows = 8;

// Reporting every pair above the threshold buries the strongest ones.
constexpr std::size_t kMaxNotableCorrelations = 8;

CorrelationMatrix correlate(const FeatureMatrix& matrix);

// ---------------------------------------------------------------------------
// Graph centrality
// ---------------------------------------------------------------------------

// PageRank over a node/edge index list. Used to size and rank nodes in the
// structural views by how central they are, rather than by declaration order.
// Damping and iteration count are the standard values; the result is
// normalised so the scores sum to 1.
std::vector<double> pageRank(std::size_t nodeCount,
                             const std::vector<std::pair<std::size_t, std::size_t>>& edges,
                             double damping = 0.85,
                             int iterations = 60);

// ---------------------------------------------------------------------------
// Reasoning layer input
// ---------------------------------------------------------------------------

// What the program's data actually looks like. The view recommender reads
// only this, so the "which chart fits" decision is made from measured shape
// rather than from a fixed default.
struct DataShape {
    std::size_t factTypes = 0;
    std::size_t records = 0;
    std::size_t sampledRecords = 0;
    std::size_t numericFields = 0;
    std::size_t dateFields = 0;
    std::size_t categoricalFields = 0;
    std::size_t identifierFields = 0;
    std::size_t inheritanceEdges = 0;
    std::size_t methods = 0;
    std::size_t globals = 0;
    std::size_t imports = 0;
    std::size_t callEdges = 0;
    // Largest per-type record count, and the type holding it.
    std::size_t largestFactRecords = 0;
    std::string largestFactName;
    // True once any fact type has enough encoded rows for PCA/k-means.
    bool clusterable = false;
    std::size_t outlierCount = 0;
    std::size_t notableCorrelations = 0;
};

} // namespace Felidae::Celidae
