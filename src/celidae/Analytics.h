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
    // True when those "outliers" are too numerous to be anomalies. A robust
    // z-score test run over a two-population field flags the whole smaller
    // population - in this repo's sample data, 12 of 44 orders. Twelve
    // records are not twelve mistakes, they are a second kind of order, and
    // saying "12 outliers" actively misleads. The indexes are still reported,
    // but a view must describe them as a second population rather than as
    // anomalies.
    bool secondPopulation = false;

    // Categorical fields: most frequent values first, capped.
    std::vector<std::pair<std::string, std::size_t>> topValues;
    // Shannon entropy normalised to 0..1. 0 means every record shares one
    // value (a useless grouping); 1 means perfectly even spread.
    double entropy = 0;
};

// Values further than this many robust z-scores from the median are reported
// as outliers. 3.5 is the conventional modified-z-score cutoff.
constexpr double kOutlierZ = 3.5;

// Above this share of a field's values, a "robust z-score" exceedance is
// evidence of a second population rather than of anomalies.
constexpr double kOutlierShareLimit = 0.10;

// At most this many categories are listed per categorical field; beyond it a
// bar chart stops being readable and the field is better described by its
// cardinality.
constexpr std::size_t kMaxCategories = 12;

// A field whose distinct-value count exceeds this fraction of its present
// count is treated as an identifier: grouping by it produces one record per
// group, which no business view benefits from.
constexpr double kIdentifierDistinctRatio = 0.92;

// Fallback share of leading-zero values that reads a *variable-width* integer
// field as a code rather than a quantity. Fixed-width padding is conclusive on
// its own and does not consult this.
constexpr double kNominalCodePaddedShare = 0.35;

std::vector<FieldStats> profileFields(const FactProfile& profile);

// Dated records a fact type needs before it earns a place on the timeline.
// Below this there is no sequence to see: a single record drawn "over time"
// is one bar, and three are three, and neither shows a trend, a gap or a
// spike - the things a timeline exists to show.
constexpr std::size_t kMinTimelineRecords = 4;

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

// How many *declared* fields the matrix's columns come from, as opposed to how
// many columns there are.
//
// One categorical field expands to one column per level, so a fact type with a
// single usable field can present as a wide matrix. Anything that treats
// column count as a measure of how much the data has to say - correlation,
// parallel coordinates, segmentation - has to consult this instead, or it will
// find relationships between a field and itself and report them as structure.
std::size_t distinctSourceFields(const FeatureMatrix& matrix);

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

// PCA and k-means need more rows than dimensions to mean anything. Two
// segments of kMinRecordsPerCluster each is the floor.
constexpr std::size_t kMinClusterRows = 8;
// k is chosen by silhouette over this range, so the view is not forced to
// invent a fixed number of segments.
constexpr int kMaxClusters = 6;
// k is additionally bounded so every segment could hold at least this many
// records. Without it, silhouette happily splits eight couriers into five
// groups - two of which hold one record each. A segment of one is a record
// with a label on it, not a finding, and it reads as insight when it is not.
constexpr std::size_t kMinRecordsPerCluster = 4;

ClusterResult clusterRecords(const FeatureMatrix& matrix);

// ---------------------------------------------------------------------------
// Structure assessment (SVD)
// ---------------------------------------------------------------------------

// What shape the encoded data actually has, measured before anything is fitted
// to it. This is the gate the rest of the pipeline consults: k-means will
// happily cut a uniform ramp into "segments" and report a healthy silhouette
// for it, because a silhouette only measures whether the cut is clean, never
// whether there was anything there to cut.
struct StructureReport {
    bool valid = false;
    std::size_t rows = 0;
    std::size_t columns = 0;

    // Singular values of the standardised matrix, descending.
    std::vector<double> singularValues;
    // Components needed to carry kVarianceForRank of the total variance.
    std::size_t effectiveRank = 0;
    // Largest/smallest singular value. Very large means the matrix is close to
    // singular, so a covariance inverse (Mahalanobis) cannot be trusted.
    double conditionNumber = 0;
    bool collinear = false;
    // Column pairs whose |correlation| is so high they carry one signal
    // between them; reported so a reader is not told the same thing twice.
    std::vector<std::pair<std::string, std::string>> redundantPairs;

    // Hopkins statistic: compares how far real records sit from their nearest
    // neighbour against how far uniformly-random points sit from the nearest
    // record. ~0.5 means the data is spread evenly and has no group structure;
    // approaching 1 means it is genuinely lumpy.
    double clusterTendency = 0.5;
    bool worthClustering = false;
};

// A component counts towards the effective rank until this much of the total
// variance is accounted for.
constexpr double kVarianceForRank = 0.95;
// Above this, the standardised matrix is treated as numerically singular.
constexpr double kMaxConditionNumber = 1e8;
// |r| at or above this means two columns are carrying the same signal.
constexpr double kRedundantCorrelation = 0.97;
// Hopkins below this is indistinguishable from an even spread, so any
// segmentation of it would be an arbitrary cut through continuous data.
constexpr double kMinClusterTendency = 0.58;

StructureReport assessStructure(const FeatureMatrix& matrix);

// ---------------------------------------------------------------------------
// Multivariate outliers (Mahalanobis)
// ---------------------------------------------------------------------------

// A record that is unremarkable on every field taken alone yet unusual in
// combination - a small order that took an hour, a cheap item bought in bulk.
// Per-field z-scores cannot see these at all, because no single field is
// extreme; the covariance structure is what makes them visible.
struct MultivariateOutlier {
    std::size_t row = 0;
    std::string label;
    double distance = 0;   // Mahalanobis distance
    // Which columns contributed most to that distance, so the finding can say
    // *why* the record is unusual rather than only that it is.
    std::vector<std::string> drivers;
};

// Chi-squared critical value at p = 0.001 is used as the cutoff, scaled by
// degrees of freedom inside the implementation.
std::vector<MultivariateOutlier> multivariateOutliers(const FeatureMatrix& matrix);

// ---------------------------------------------------------------------------
// Driver analysis (least squares)
// ---------------------------------------------------------------------------

// "What moves this number?" - an ordinary least-squares fit of one numeric
// column against all the others, solved by QR. Coefficients are on the
// standardised scale, so they are directly comparable to each other: a beta of
// 0.6 moves the target twice as hard as one of 0.3.
struct DriverModel {
    bool valid = false;
    std::string target;
    double r2 = 0;
    double adjustedR2 = 0;
    std::vector<ComponentLoading> coefficients;  // strongest first
    // False when the predictors are rank-deficient - which a one-hot encoding
    // guarantees, since a categorical field's indicator columns sum to a
    // constant. The fit is still real; the attribution of it to individual
    // predictors is not, because infinitely many coefficient vectors produce
    // exactly this fit. A view must report the fit and withhold the
    // per-predictor claim, rather than printing whichever solution the solver
    // happened to return.
    bool identifiable = true;
    // Numerical rank of the predictor matrix, i.e. how many independent
    // signals the predictors actually carry between them.
    std::size_t rank = 0;
};

// Only fits worth reporting are returned.
constexpr double kMinDriverR2 = 0.3;

// Singular values below this fraction of the largest are treated as zero when
// determining rank. Eigen's default is tighter than is useful here: a one-hot
// group is exactly singular in exact arithmetic but lands a little above zero
// in floating point, and counting that as a real dimension is what lets a
// non-identifiable fit pass as an identifiable one.
constexpr double kRankTolerance = 1e-9;

std::vector<DriverModel> explainNumericTargets(const FeatureMatrix& matrix);

// ---------------------------------------------------------------------------
// Field-pair association
// ---------------------------------------------------------------------------

// How strongly a categorical field separates a numeric one: the share of the
// numeric field's variance explained by group membership (eta-squared, the
// one-way ANOVA effect size). 0 means the groups are indistinguishable; near 1
// means knowing the group tells you the value.
//
// This is what decides whether "revenue by region" is worth drawing at all.
// Without it a view either draws every numeric-by-categorical combination -
// most of which show four identical boxes - or hard-codes which pairing its
// author guessed would be interesting.
double groupSeparation(const FactProfile& profile,
                       const std::string& numericField,
                       FieldType numericType,
                       const std::string& categoricalField);

// Cramer's V for two categorical fields: chi-squared association normalised
// to 0..1, so a 2x2 and a 5x7 contingency are comparable. Decides whether a
// cross-tab of two labels carries a relationship worth showing.
double categoricalAssociation(const FactProfile& profile,
                              const std::string& first,
                              const std::string& second);

// ---------------------------------------------------------------------------
// Visualization proposals
// ---------------------------------------------------------------------------

// A chart the data argues for, with the measurement that argues for it.
//
// Celidae does not know what a program's facts mean, and must not pretend to.
// It cannot know that `placed` is an order date or that `region` is a sales
// territory - so a view built around "orders over time" would be a guess that
// happens to fit one program. What Celidae *can* measure is shape: that one
// field is temporal and another is a measure, that a label separates a number,
// that two numbers are collinear. Every proposal below is generated from such
// a measurement, which is why the same code produces a delivery-time histogram
// for one program and a species cross-tab for another without either being
// written down anywhere.
struct ChartProposal {
    std::string chart;      // histogram | bar | hbar | line | scatter | boxplot | heatmap | parallel
    std::string title;
    std::string rationale;  // the measurement that earned it a place
    double score = 0;       // strength of that measurement, 0..1
    std::vector<std::string> fields;
};

// Proposals below this are not worth a reader's screen space.
constexpr double kMinProposalScore = 0.12;

// How many of any one chart kind lead the ranking before other kinds get a
// turn. Six histograms answer the same question six times.
constexpr std::size_t kMaxPerChartKind = 2;

std::vector<ChartProposal> proposeCharts(const FactProfile& profile,
                                         const std::vector<FieldStats>& fields,
                                         const FeatureMatrix& matrix,
                                         const StructureReport& structure);

// ---------------------------------------------------------------------------
// Algorithm bundles
// ---------------------------------------------------------------------------

// A named group of algorithms that together answer one question about the
// data, and the verdict on whether this fact type can support them.
//
// Bundles exist because the algorithms were previously reached ad hoc: each
// view called whichever primitives it happened to need, in whatever order its
// author wrote them, so "does this program have text structure worth showing"
// was answered in one place, "does it have segments" in another, and neither
// could see the other's conclusion. The stages below make the sequence
// uniform - every view runs the same pipeline and differs only in which
// bundles it consumes - which is what lets a reader be told, in one list, what
// was tried and what it found.
//
// A bundle is the unit that gets a verdict, because applicability is a
// property of a question rather than of a single algorithm: Hopkins, k-means,
// silhouette and the oblique tree stand or fall together, and reporting them
// separately would mean reporting a segmentation whose tendency test had
// already declined it.
enum class BundleKind {
    Profile,       // what each field is: type, coverage, cardinality, spread
    Structure,     // rank, condition, collinearity, cluster tendency (SVD)
    Association,   // which fields relate: correlation, eta-squared, Cramer's V
    Segmentation,  // PCA, k-means, silhouette, oblique tree
    Anomaly,       // robust z-scores and Mahalanobis distance
    Explanation,   // least-squares drivers
    Text           // TF-IDF, latent semantics, vocabulary
};

const char* bundleKindName(BundleKind kind);

struct AlgorithmBundle {
    BundleKind kind = BundleKind::Profile;
    // The question this bundle answers, in the words a reader would use.
    std::string question;
    // The algorithms it ran, named so the reasoning panel can show its work.
    std::vector<std::string> algorithms;
    bool applicable = false;
    // What was measured, and - when it did not apply - why not. Always
    // populated: a bundle that declines silently is indistinguishable from
    // one that was never tried.
    std::string finding;
};

// ---------------------------------------------------------------------------
// Multi-step reasoning
// ---------------------------------------------------------------------------

// One step of the analysis, recorded as it happens. The point is that a
// reader (or a reviewer) can see which measurement caused which decision,
// rather than being handed a chart and asked to trust it.
// The stages every fact type passes through, in order. Naming them makes the
// sequence the same for every view and every program, so the reasoning panel
// reads as one account rather than as whatever each view happened to log.
//
//   Bundle     decide which groups of algorithms this data can support
//   Analyse    run them
//   Summarise  reduce the results to the findings worth a reader's attention
//   Reason     turn those findings into explanations - rules, orderings, maps
//   Design     choose the UI components that carry them, and score them
//
// Rendering is deliberately not a stage here: by the time Design has run, the
// answer is a list of component requests, and turning those into panels is a
// mechanical step with no decisions left in it. Keeping the judgement in this
// header and the drawing in Visualization.cpp is what stops chart-choice logic
// from accumulating in the renderer again.
enum class PipelineStage { Bundle, Analyse, Summarise, Reason, Design };

const char* pipelineStageName(PipelineStage stage);

struct ReasoningStep {
    std::string stage;     // one of pipelineStageName(), or a bundle name
    std::string finding;   // what was measured
    std::string decision;  // what that caused the pipeline to do
};

// The whole analysis of one fact type, run as a pipeline where each stage's
// result gates the next.
struct AnalysisPipeline {
    std::string factType;
    std::vector<ReasoningStep> steps;
    // Which bundles were tried and what each concluded, in run order. This is
    // the summary of the analysis as a whole - a reader can see that
    // segmentation was attempted and declined, rather than inferring it from
    // the absence of a chart.
    std::vector<AlgorithmBundle> bundles;
    FeatureMatrix matrix;
    StructureReport structure;
    ClusterResult clusters;
    std::vector<MultivariateOutlier> outliers;
    std::vector<DriverModel> drivers;
    // Charts this fact type's measured shape argues for, strongest first.
    std::vector<ChartProposal> proposals;
    // False when the pipeline stopped early; `steps` says where and why.
    bool analysed = false;

    // Whether a given bundle applied, for callers that need to branch on it
    // without walking the list.
    bool supports(BundleKind kind) const;
    const AlgorithmBundle* bundle(BundleKind kind) const;
};

AnalysisPipeline analyseFactType(const std::string& name,
                                 const FactProfile& profile,
                                 const std::vector<FieldStats>& fields);

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
// Relationship inference
// ---------------------------------------------------------------------------

// A join between two fact types, discovered from the values themselves.
//
// This exists because Celidae's ER view had nothing to draw. Felidae has no
// foreign-key syntax, so the only relationship the view could show was
// `extend`, and a program that declares several fact types without inheritance
// - which is most of them - produced an "entity relationship" diagram with no
// relationships in it. It rendered each entity fanned out into its fields,
// which is precisely what the schema view already shows, so the two views were
// near-identical pictures under different names.
//
// A relationship is inferred when one field's values are contained in another
// field's values and the target side is near-unique. That is the definition of
// a foreign key pointing at a candidate key, and it is measurable without
// knowing what any field means: if every `Order.customer` appears exactly once
// in `Customer.name`, those two fact types are joined on it whatever they are
// called.
struct InferredRelationship {
    std::string fromType;
    std::string fromField;
    std::string toType;
    std::string toField;
    // Share of the referencing field's distinct values found on the target
    // side. 1.0 is total referential integrity.
    double containment = 0;
    // Distinct referencing values per referenced value. Around 1 means
    // one-to-one; well above means many-to-one.
    double fanIn = 1;
    // "many-to-one" | "one-to-one", from fanIn.
    std::string cardinality;
    // Referencing values with no match on the target side - dangling
    // references, which are a data-quality finding in their own right.
    std::size_t orphans = 0;
};

// The target side must be at least this unique to count as a key. Below it,
// the "containment" is just two fields drawing on a shared small vocabulary
// (two status columns both using "open"/"closed"), which is not a join.
constexpr double kKeyUniquenessRatio = 0.95;

// Share of referencing values that must resolve. Set below 1 so a genuine
// join with a few dangling rows is still found - and then reported, since
// those rows are the interesting part.
constexpr double kMinContainment = 0.80;

// Below this many distinct values on the referencing side, containment is
// coincidence: two fields each holding one value "match" trivially.
constexpr std::size_t kMinJoinValues = 3;

std::vector<InferredRelationship> inferRelationships(
    const std::map<std::string, FactProfile>& facts,
    const std::map<std::string, std::vector<FieldStats>>& stats);

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
    // Text fields whose values share enough vocabulary for latent semantic
    // analysis to place them. Counted separately from categoricalFields
    // because it is the only measurement that distinguishes a corpus with
    // structure from a column of unrelated strings - and it is what keeps a
    // fact set of near-unique names from being declared unanalysable, which
    // is exactly what happened to a 249-record list of country names.
    std::size_t textCorpusFields = 0;
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
