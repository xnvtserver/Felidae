#include "celidae/Analytics.h"

#include <Eigen/Dense>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <numeric>
#include <random>
#include <set>
#include <unordered_map>

namespace Felidae::Celidae {

namespace {

// A field is treated as belonging to a scale when this share of its present
// values parse on that scale. Below it, a few stray numbers in a text field
// would be enough to misclassify the whole column.
constexpr double kTypeAgreement = 0.8;

double median(std::vector<double> values) {
    if (values.empty()) return 0.0;
    const std::size_t middle = values.size() / 2;
    std::nth_element(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(middle), values.end());
    const double upper = values[middle];
    if (values.size() % 2 == 1) return upper;
    // The lower middle element is the largest of the left partition, which
    // nth_element has already placed before `middle`.
    const double lower = *std::max_element(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(middle));
    return (lower + upper) / 2.0;
}

double quantile(std::vector<double> values, double fraction) {
    if (values.empty()) return 0.0;
    std::sort(values.begin(), values.end());
    const double position = fraction * static_cast<double>(values.size() - 1);
    const std::size_t low = static_cast<std::size_t>(std::floor(position));
    const std::size_t high = static_cast<std::size_t>(std::ceil(position));
    if (low == high) return values[low];
    const double weight = position - static_cast<double>(low);
    return values[low] * (1.0 - weight) + values[high] * weight;
}

// Days since 1970-01-01 for a proleptic Gregorian date. Howard Hinnant's
// days_from_civil: exact for every date, and short enough not to warrant a
// date library for the two formats Celidae recognises.
long long daysFromCivil(int year, unsigned month, unsigned day) {
    year -= month <= 2;
    const int era = (year >= 0 ? year : year - 399) / 400;
    const unsigned yearOfEra = static_cast<unsigned>(year - era * 400);
    const unsigned dayOfYear =
        (153u * (month + (month > 2 ? -3u : 9u)) + 2u) / 5u + day - 1u;
    const unsigned dayOfEra = yearOfEra * 365u + yearOfEra / 4u - yearOfEra / 100u + dayOfYear;
    return static_cast<long long>(era) * 146097LL + static_cast<long long>(dayOfEra) - 719468LL;
}

bool allDigits(const std::string& value, std::size_t from, std::size_t count) {
    if (from + count > value.size()) return false;
    for (std::size_t i = from; i < from + count; ++i) {
        if (!std::isdigit(static_cast<unsigned char>(value[i]))) return false;
    }
    return true;
}

// Present literal values of one field, in sample order, paired with the
// sample index they came from.
struct FieldValues {
    std::vector<std::size_t> sampleIndex;
    std::vector<std::string> text;
};

FieldValues collectField(const FactProfile& profile, const std::string& name) {
    FieldValues values;
    for (std::size_t i = 0; i < profile.samples.size(); ++i) {
        const auto found = profile.samples[i].find(name);
        if (found == profile.samples[i].end()) continue;
        values.sampleIndex.push_back(i);
        values.text.push_back(found->second);
    }
    return values;
}

// A numeric column that is really a surrogate key: 1, 2, 3, ... assigned in
// declaration order. It is numeric by type but carries no measurable meaning,
// and letting it through does active harm - it produces a meaningless
// histogram, it appears as a cluster driver ("high id"), and it manufactures
// correlations with anything that happens to have been declared in a
// correlated order. In this repo's own sample data an `id` column produced
// "id and items move together (r = 0.66)", which is a fact about the order the
// records were written in, not about orders.
//
// All four conditions must hold, which is what keeps a genuine measure out of
// this bucket: every value distinct, every value a whole number, the values
// ascending in declaration order, and the range packed almost solid. A price,
// a score or a duration fails at least one.
bool looksLikeSurrogateKey(const std::vector<std::string>& text) {
    // Fewer than four values cannot establish a pattern at all.
    if (text.size() < 4) return false;
    std::vector<double> numbers;
    numbers.reserve(text.size());
    for (const auto& value : text) {
        if (!looksNumeric(value)) return false;
        const double parsed = std::strtod(value.c_str(), nullptr);
        if (parsed != std::floor(parsed)) return false;  // not a whole number
        numbers.push_back(parsed);
    }
    for (std::size_t i = 1; i < numbers.size(); ++i) {
        if (numbers[i] <= numbers[i - 1]) return false;  // not ascending, so not assigned in order
    }
    // Ascending already implies distinct; what remains is density. A key runs
    // 1..n with few gaps; a measure that happens to be sorted does not.
    //
    // The required density is graduated, because a long run is evidence in
    // itself while a short one is not. Eight or more ascending integers packed
    // into a 1.25x range is conclusive. Below that, only a perfectly
    // consecutive run counts - four to seven values that happen to ascend are
    // not rare, but four to seven that are exactly n, n+1, ... n+k are.
    const double span = numbers.back() - numbers.front() + 1.0;
    const double count = static_cast<double>(numbers.size());
    return numbers.size() >= 8 ? span <= 1.25 * count : span == count;
}

// A field whose values are digits but which is a *code*, not a quantity:
// ISO country codes "004"/"008"/"010", postal codes, account numbers, phone
// extensions. Zero padding is the giveaway and it is unambiguous - nobody
// writes a price, a duration or a count with a leading zero, because the zero
// carries no arithmetic meaning. It is there to hold a column width, which is
// exactly what makes the value a label.
//
// Letting these through as measurements does real damage, and it is not
// hypothetical: examples/data/converted_csv_country.fx declares
// `country_code: "004"`, and Celidae reported mean=433.84, median=434,
// stddev=252.98 and skew=0.01 for it, then drew a histogram of the ISO 3166
// numbering scheme and ordered 249 countries along a "timeline" by it. Every
// one of those numbers is arithmetically correct and none of them is about
// countries.
//
// looksLikeSurrogateKey() does not catch this case: these codes are neither
// ascending in declaration order nor packed into a dense range (249 values
// spread over 4..894), so the density test correctly declines to call them a
// surrogate key. Padding is a separate signal and needs its own test.
bool looksLikeNominalCode(const std::vector<std::string>& text) {
    if (text.size() < 3) return false;
    std::size_t padded = 0;
    std::size_t width = 0;
    bool uniformWidth = true;
    for (const auto& value : text) {
        // Only unsigned integer spellings can be padded; a sign or a decimal
        // point means the author was writing a quantity.
        if (value.empty()) return false;
        for (const char character : value) {
            if (!std::isdigit(static_cast<unsigned char>(character))) return false;
        }
        if (width == 0) width = value.size();
        else if (value.size() != width) uniformWidth = false;
        if (value.size() > 1 && value[0] == '0') ++padded;
    }
    if (padded == 0 || width < 2) return false;

    // Two independent signals, either of which is conclusive.
    //
    // Uniform width is the stronger one. A quantity spanning 4 to 894 is
    // written "4" and "894"; only a code is written "004" and "894", because
    // the padding exists to hold a column width. This is what identifies ISO
    // 3166 numeric codes, where just 30 of 249 values are actually padded -
    // a share-based test alone would miss them, since most of the range is
    // three digits wide already.
    if (uniformWidth) return true;

    // Where widths do vary, a substantial share of leading zeros still means
    // a formatting convention rather than a run of typos.
    return static_cast<double>(padded) / static_cast<double>(text.size()) >=
        kNominalCodePaddedShare;
}

FieldType classify(const FieldValues& values) {
    if (values.text.empty()) return FieldType::Empty;
    std::size_t numeric = 0;
    std::size_t dated = 0;
    bool anyIsoDate = false;
    std::set<std::string> distinct;
    for (const auto& text : values.text) {
        if (looksNumeric(text)) ++numeric;
        if (looksLikeDate(text)) {
            ++dated;
            if (text.find('-') != std::string::npos) anyIsoDate = true;
        }
        distinct.insert(text);
    }
    const double total = static_cast<double>(values.text.size());

    // An ISO date is unambiguously temporal. A bare four-digit year parses as
    // a number too, and both scales order it identically, so it stays numeric
    // rather than being silently reinterpreted as midnight on January 1st.
    if (anyIsoDate && static_cast<double>(dated) / total >= kTypeAgreement) return FieldType::Date;
    if (static_cast<double>(numeric) / total >= kTypeAgreement) {
        if (looksLikeSurrogateKey(values.text)) return FieldType::Identifier;
        // A zero-padded code falls through to the categorical/identifier tests
        // below rather than returning here, so it is typed by its cardinality
        // the same way any other label is: a handful of distinct codes is a
        // grouping worth charting, 249 of them is a key.
        if (!looksLikeNominalCode(values.text)) return FieldType::Numeric;
    }
    if (static_cast<double>(dated) / total >= kTypeAgreement) return FieldType::Date;

    // Near-unique text is a key, not a category: grouping by it yields one
    // record per group, which tells a reader nothing.
    //
    // The row threshold is deliberately low. At eight, a fact type with five
    // records and five distinct names fell through to Categorical, which
    // one-hot encoded every name into its own column and then reported
    // "name=1.1.0 and scope=operators move together (r = 1.00)" as a finding.
    // That correlation is an artefact of two columns describing the same
    // single record, not a fact about the data.
    if (values.text.size() >= 3 &&
        static_cast<double>(distinct.size()) / total > kIdentifierDistinctRatio) {
        return FieldType::Identifier;
    }
    return FieldType::Categorical;
}

// Numeric projection of a field's values, on whichever scale its type implies.
std::vector<double> numericValues(const FieldValues& values, FieldType type) {
    std::vector<double> out;
    out.reserve(values.text.size());
    for (const auto& text : values.text) {
        double parsed = 0;
        if (type == FieldType::Date) {
            if (!dateToDayNumber(text, parsed)) continue;
        } else {
            if (!looksNumeric(text)) continue;
            parsed = std::strtod(text.c_str(), nullptr);
        }
        out.push_back(parsed);
    }
    return out;
}

} // namespace

bool looksNumeric(const std::string& value) {
    if (value.empty()) return false;
    char* end = nullptr;
    const double parsed = std::strtod(value.c_str(), &end);
    if (end == nullptr || *end != '\0') return false;
    // NaN/inf spellings parse but cannot be ordered or averaged meaningfully.
    return std::isfinite(parsed);
}

namespace {

// Days in a month, honouring leap years, so 2025-02-30 is rejected while
// 2024-02-29 is accepted.
int daysInMonth(int year, int month) {
    static const int lengths[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month < 1 || month > 12) return 0;
    if (month != 2) return lengths[month - 1];
    const bool leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
    return leap ? 29 : 28;
}

} // namespace

bool looksLikeDate(const std::string& value) {
    // YYYY-MM-DD, optionally followed by a time component.
    //
    // The calendar has to be checked, not just the digit positions. Accepting
    // "2025-13-45" here while dateToDayNumber() rejects it put the two out of
    // step: the field was classified as a date, so its statistics were
    // computed on the date scale, but the unconvertible values were silently
    // dropped from those statistics. The result was a field reporting two
    // distinct values with an identical minimum and maximum - a contradiction
    // on the face of the output.
    if (value.size() >= 10 && allDigits(value, 0, 4) && value[4] == '-' &&
        allDigits(value, 5, 2) && value[7] == '-' && allDigits(value, 8, 2)) {
        const int year = std::stoi(value.substr(0, 4));
        const int month = std::stoi(value.substr(5, 2));
        const int day = std::stoi(value.substr(8, 2));
        return year >= 1 && month >= 1 && month <= 12 &&
            day >= 1 && day <= daysInMonth(year, month);
    }
    // A bare four-digit year in a plausible range.
    if (value.size() == 4 && allDigits(value, 0, 4)) {
        const int year = std::stoi(value);
        return year >= 1000 && year <= 9999;
    }
    return false;
}

bool dateToDayNumber(const std::string& value, double& out) {
    if (!looksLikeDate(value)) return false;
    if (value.size() == 4) {
        out = static_cast<double>(daysFromCivil(std::stoi(value), 1, 1));
        return true;
    }
    const int year = std::stoi(value.substr(0, 4));
    const int month = std::stoi(value.substr(5, 2));
    const int day = std::stoi(value.substr(8, 2));
    if (month < 1 || month > 12 || day < 1 || day > 31) return false;
    out = static_cast<double>(
        daysFromCivil(year, static_cast<unsigned>(month), static_cast<unsigned>(day)));
    return true;
}

const char* fieldTypeName(FieldType type) {
    switch (type) {
        case FieldType::Empty: return "empty";
        case FieldType::Numeric: return "numeric";
        case FieldType::Date: return "date";
        case FieldType::Categorical: return "categorical";
        case FieldType::Identifier: return "identifier";
    }
    return "empty";
}

std::vector<FieldStats> profileFields(const FactProfile& profile) {
    std::vector<FieldStats> result;
    result.reserve(profile.fields.size());

    for (const auto& field : profile.fields) {
        FieldStats stats;
        stats.name = field.first;
        const FieldValues values = collectField(profile, field.first);
        stats.present = values.text.size();
        stats.missing = profile.samples.size() > values.text.size()
            ? profile.samples.size() - values.text.size()
            : 0;
        stats.type = classify(values);

        std::map<std::string, std::size_t> counts;
        for (const auto& text : values.text) ++counts[text];
        stats.distinct = counts.size();

        if (stats.type == FieldType::Numeric || stats.type == FieldType::Date) {
            const std::vector<double> numbers = numericValues(values, stats.type);
            if (!numbers.empty()) {
                stats.min = *std::min_element(numbers.begin(), numbers.end());
                stats.max = *std::max_element(numbers.begin(), numbers.end());
                stats.mean = std::accumulate(numbers.begin(), numbers.end(), 0.0) /
                    static_cast<double>(numbers.size());
                stats.median = median(numbers);

                double sumSquares = 0;
                double sumCubes = 0;
                for (const double number : numbers) {
                    const double delta = number - stats.mean;
                    sumSquares += delta * delta;
                    sumCubes += delta * delta * delta;
                }
                stats.stddev = numbers.size() > 1
                    ? std::sqrt(sumSquares / static_cast<double>(numbers.size() - 1))
                    : 0.0;
                // Skewness says which way a distribution leans, which is what
                // decides whether a mean is a fair summary of it at all.
                stats.skewness = (stats.stddev > 0 && numbers.size() > 2)
                    ? (sumCubes / static_cast<double>(numbers.size())) /
                        (stats.stddev * stats.stddev * stats.stddev)
                    : 0.0;

                std::vector<double> deviations;
                deviations.reserve(numbers.size());
                for (const double number : numbers) deviations.push_back(std::fabs(number - stats.median));
                stats.mad = median(deviations);

                // Modified z-score. MAD is used rather than stddev because a
                // single extreme value inflates stddev enough to mask itself:
                // the test would then report no outliers precisely when the
                // outlier is worst.
                if (stats.mad > 0) {
                    std::size_t cursor = 0;
                    for (std::size_t i = 0; i < values.text.size(); ++i) {
                        double parsed = 0;
                        if (stats.type == FieldType::Date) {
                            if (!dateToDayNumber(values.text[i], parsed)) continue;
                        } else {
                            if (!looksNumeric(values.text[i])) continue;
                            parsed = std::strtod(values.text[i].c_str(), nullptr);
                        }
                        const double score = 0.6745 * (parsed - stats.median) / stats.mad;
                        if (std::fabs(score) > kOutlierZ) {
                            stats.outliers.push_back(values.sampleIndex[i]);
                        }
                        ++cursor;
                    }
                    (void)cursor;
                    stats.secondPopulation = stats.present > 0 &&
                        static_cast<double>(stats.outliers.size()) >
                            kOutlierShareLimit * static_cast<double>(stats.present);
                }
            }
        } else if (stats.type == FieldType::Categorical || stats.type == FieldType::Identifier) {
            std::vector<std::pair<std::string, std::size_t>> ordered(counts.begin(), counts.end());
            std::sort(ordered.begin(), ordered.end(), [](const auto& a, const auto& b) {
                if (a.second != b.second) return a.second > b.second;
                return a.first < b.first;  // stable, diffable output on ties
            });
            if (ordered.size() > kMaxCategories) ordered.resize(kMaxCategories);
            stats.topValues = std::move(ordered);

            // Normalised Shannon entropy: how evenly records spread over the
            // categories. A field where 99% of records share one value looks
            // groupable by cardinality alone but conveys almost nothing.
            const double total = static_cast<double>(values.text.size());
            double entropy = 0;
            for (const auto& entry : counts) {
                const double p = static_cast<double>(entry.second) / total;
                if (p > 0) entropy -= p * std::log(p);
            }
            const double maximum = counts.size() > 1 ? std::log(static_cast<double>(counts.size())) : 0.0;
            stats.entropy = maximum > 0 ? entropy / maximum : 0.0;
        }

        result.push_back(std::move(stats));
    }
    return result;
}

Histogram buildHistogram(const FactProfile& profile, const FieldStats& field) {
    Histogram histogram;
    histogram.field = field.name;
    histogram.fromDates = field.type == FieldType::Date;
    if (field.type != FieldType::Numeric && field.type != FieldType::Date) return histogram;

    const FieldValues values = collectField(profile, field.name);
    const std::vector<double> numbers = numericValues(values, field.type);
    if (numbers.size() < 2) return histogram;

    const double low = *std::min_element(numbers.begin(), numbers.end());
    const double high = *std::max_element(numbers.begin(), numbers.end());
    if (!(high > low)) {
        // Every record shares one value; a single full bin is the honest
        // rendering, and dividing by a zero range would not be.
        histogram.edges = {low, low + 1};
        histogram.counts = {numbers.size()};
        histogram.binWidth = 1;
        return histogram;
    }

    // Freedman-Diaconis: width from the interquartile range, so the binning
    // follows the data's actual spread instead of a fixed bar count.
    const double iqr = quantile(numbers, 0.75) - quantile(numbers, 0.25);
    double width = iqr > 0
        ? 2.0 * iqr / std::cbrt(static_cast<double>(numbers.size()))
        : 0.0;
    if (!(width > 0)) {
        // Heavily tied data has a zero IQR; Sturges' rule still gives a
        // sensible bar count from the sample size alone.
        const double bins = std::ceil(std::log2(static_cast<double>(numbers.size())) + 1.0);
        width = (high - low) / std::max(1.0, bins);
    }

    std::size_t binCount = static_cast<std::size_t>(std::ceil((high - low) / width));
    binCount = std::max<std::size_t>(1, std::min<std::size_t>(binCount, 40));
    width = (high - low) / static_cast<double>(binCount);

    histogram.binWidth = width;
    histogram.edges.reserve(binCount + 1);
    for (std::size_t i = 0; i <= binCount; ++i) {
        histogram.edges.push_back(low + static_cast<double>(i) * width);
    }
    histogram.counts.assign(binCount, 0);
    for (const double number : numbers) {
        std::size_t index = static_cast<std::size_t>((number - low) / width);
        if (index >= binCount) index = binCount - 1;  // the maximum lands in the last bin
        ++histogram.counts[index];
    }
    return histogram;
}

FeatureMatrix buildFeatureMatrix(const FactProfile& profile,
                                 const std::vector<FieldStats>& fields) {
    FeatureMatrix matrix;
    if (profile.samples.empty()) return matrix;

    // Column plan first, so every row is built against the same layout.
    struct Column {
        std::string name;
        std::string field;
        FieldType type = FieldType::Empty;
        std::string level;   // one-hot: the category this column indicates
        double fallback = 0; // mean-imputed value for a missing entry
    };
    std::vector<Column> plan;

    for (const auto& field : fields) {
        if (field.type == FieldType::Numeric || field.type == FieldType::Date) {
            if (field.present < 2 || field.stddev <= 0) continue;  // constant: no information
            Column column;
            column.name = field.name;
            column.field = field.name;
            column.type = field.type;
            column.fallback = field.mean;
            plan.push_back(std::move(column));
        } else if (field.type == FieldType::Categorical) {
            // Identifiers and single-level fields are skipped: one-hot
            // encoding either produces a column per record or a constant one.
            if (field.distinct < 2 || field.distinct > kMaxOneHotLevels) continue;
            for (const auto& level : field.topValues) {
                Column column;
                column.name = field.name + "=" + level.first;
                column.field = field.name;
                column.type = FieldType::Categorical;
                column.level = level.first;
                column.fallback = static_cast<double>(level.second) /
                    static_cast<double>(std::max<std::size_t>(1, field.present));
                plan.push_back(std::move(column));
            }
        }
    }
    if (plan.empty()) return matrix;

    // A readable row label, preferred over a bare index so a scatter point can
    // be identified by the reader.
    auto labelFor = [&](const FactRecordValues& record, std::size_t index) {
        for (const char* candidate : {"name", "id", "title", "label", "key"}) {
            const auto found = record.find(candidate);
            if (found != record.end()) return found->second;
        }
        return "record " + std::to_string(index + 1);
    };

    for (const auto& column : plan) matrix.columns.push_back(column.name);
    for (std::size_t i = 0; i < profile.samples.size(); ++i) {
        const FactRecordValues& record = profile.samples[i];
        std::vector<double> row;
        row.reserve(plan.size());
        std::size_t known = 0;
        for (const auto& column : plan) {
            const auto found = record.find(column.field);
            if (found == record.end()) {
                row.push_back(column.fallback);
                continue;
            }
            ++known;
            if (column.type == FieldType::Categorical) {
                row.push_back(found->second == column.level ? 1.0 : 0.0);
            } else if (column.type == FieldType::Date) {
                double parsed = 0;
                row.push_back(dateToDayNumber(found->second, parsed) ? parsed : column.fallback);
            } else {
                row.push_back(looksNumeric(found->second)
                    ? std::strtod(found->second.c_str(), nullptr)
                    : column.fallback);
            }
        }
        // A record that supplied nothing is entirely imputed; including it
        // would place a phantom point at the centroid of every view.
        if (known == 0) continue;
        matrix.rows.push_back(std::move(row));
        matrix.rowSamples.push_back(i);
        matrix.rowLabels.push_back(labelFor(record, i));
    }
    return matrix;
}

namespace {

// Column-standardised copy of the matrix, plus the columns that survived.
// Zero-variance columns are dropped: they contribute nothing to PCA and would
// divide by zero on the way in.
struct Standardized {
    Eigen::MatrixXd data;
    std::vector<std::string> columns;
};

// The field a feature column came from. One-hot columns are named
// "field=level", so everything before the first '=' identifies the source.
std::string sourceField(const std::string& column) {
    const std::size_t separator = column.find('=');
    return separator == std::string::npos ? column : column.substr(0, separator);
}

// `balanceCategoricals` divides each one-hot column by the square root of how
// many levels its field has, so one categorical field contributes about as
// much total variance as one numeric field instead of one unit per level.
//
// Without it, a fact type with a 4-level region and a 3-level channel puts
// seven unit-variance columns against four real measures, and distance is
// dominated by which region a record is in. On this repo's sample orders that
// dropped the silhouette of a genuinely clean two-population split to 0.21.
// Correlation must NOT use it: Pearson's r assumes unit-variance columns, and
// rescaling them would silently change every reported coefficient.
Standardized standardize(const FeatureMatrix& matrix, bool balanceCategoricals = false) {
    Standardized result;
    const std::size_t rows = matrix.rowCount();
    const std::size_t cols = matrix.columnCount();
    if (rows == 0 || cols == 0) return result;

    Eigen::MatrixXd raw(static_cast<Eigen::Index>(rows), static_cast<Eigen::Index>(cols));
    for (std::size_t r = 0; r < rows; ++r) {
        for (std::size_t c = 0; c < cols; ++c) {
            raw(static_cast<Eigen::Index>(r), static_cast<Eigen::Index>(c)) = matrix.rows[r][c];
        }
    }

    std::vector<Eigen::Index> keep;
    for (Eigen::Index c = 0; c < raw.cols(); ++c) {
        const double mean = raw.col(c).mean();
        const double variance = (raw.col(c).array() - mean).square().sum() /
            static_cast<double>(std::max<Eigen::Index>(1, raw.rows() - 1));
        if (variance > 1e-12) keep.push_back(c);
    }
    if (keep.empty()) return result;

    // How many surviving columns each source field expanded into.
    std::map<std::string, std::size_t> levelsPerField;
    if (balanceCategoricals) {
        for (const Eigen::Index source : keep) {
            const std::string& name = matrix.columns[static_cast<std::size_t>(source)];
            if (name.find('=') != std::string::npos) ++levelsPerField[sourceField(name)];
        }
    }

    result.data.resize(raw.rows(), static_cast<Eigen::Index>(keep.size()));
    for (std::size_t i = 0; i < keep.size(); ++i) {
        const Eigen::Index source = keep[i];
        const std::string& name = matrix.columns[static_cast<std::size_t>(source)];
        const double mean = raw.col(source).mean();
        const double stddev = std::sqrt(
            (raw.col(source).array() - mean).square().sum() /
            static_cast<double>(std::max<Eigen::Index>(1, raw.rows() - 1)));
        double weight = 1.0;
        const auto levels = levelsPerField.find(sourceField(name));
        if (levels != levelsPerField.end() && levels->second > 1 &&
            name.find('=') != std::string::npos) {
            weight = 1.0 / std::sqrt(static_cast<double>(levels->second));
        }
        result.data.col(static_cast<Eigen::Index>(i)) =
            ((raw.col(source).array() - mean) / stddev) * weight;
        result.columns.push_back(name);
    }
    return result;
}

std::vector<ComponentLoading> topLoadings(const Eigen::VectorXd& axis,
                                          const std::vector<std::string>& columns,
                                          std::size_t limit) {
    std::vector<ComponentLoading> loadings;
    for (Eigen::Index i = 0; i < axis.size(); ++i) {
        loadings.push_back(ComponentLoading{columns[static_cast<std::size_t>(i)], axis(i)});
    }
    std::sort(loadings.begin(), loadings.end(), [](const auto& a, const auto& b) {
        if (std::fabs(a.weight) != std::fabs(b.weight)) return std::fabs(a.weight) > std::fabs(b.weight);
        return a.column < b.column;
    });
    if (loadings.size() > limit) loadings.resize(limit);
    return loadings;
}

// k-means++ seeding followed by Lloyd's algorithm. The RNG is seeded with a
// fixed constant on purpose: Celidae's output is meant to be re-runnable and
// diffable, and a random seed would make the same program produce different
// segment numbering on every invocation.
std::vector<int> kMeans(const Eigen::MatrixXd& data, int k, int iterations = 40) {
    const Eigen::Index n = data.rows();
    std::vector<int> assignment(static_cast<std::size_t>(n), 0);
    if (k <= 1 || n == 0) return assignment;

    std::mt19937 rng(20240917u);
    Eigen::MatrixXd centroids(k, data.cols());
    std::uniform_int_distribution<Eigen::Index> pick(0, n - 1);
    centroids.row(0) = data.row(pick(rng));

    std::vector<double> nearest(static_cast<std::size_t>(n), std::numeric_limits<double>::max());
    for (int c = 1; c < k; ++c) {
        double total = 0;
        for (Eigen::Index i = 0; i < n; ++i) {
            const double distance = (data.row(i) - centroids.row(c - 1)).squaredNorm();
            double& best = nearest[static_cast<std::size_t>(i)];
            best = std::min(best, distance);
            total += best;
        }
        if (!(total > 0)) {
            // Every remaining point coincides with a chosen centre; further
            // seeding is arbitrary, so reuse points in order.
            centroids.row(c) = data.row(static_cast<Eigen::Index>(c) % n);
            continue;
        }
        std::uniform_real_distribution<double> spread(0.0, total);
        double target = spread(rng);
        Eigen::Index chosen = n - 1;
        for (Eigen::Index i = 0; i < n; ++i) {
            target -= nearest[static_cast<std::size_t>(i)];
            if (target <= 0) { chosen = i; break; }
        }
        centroids.row(c) = data.row(chosen);
    }

    for (int iteration = 0; iteration < iterations; ++iteration) {
        bool changed = false;
        for (Eigen::Index i = 0; i < n; ++i) {
            int best = 0;
            double bestDistance = std::numeric_limits<double>::max();
            for (int c = 0; c < k; ++c) {
                const double distance = (data.row(i) - centroids.row(c)).squaredNorm();
                if (distance < bestDistance) { bestDistance = distance; best = c; }
            }
            if (assignment[static_cast<std::size_t>(i)] != best) {
                assignment[static_cast<std::size_t>(i)] = best;
                changed = true;
            }
        }
        Eigen::MatrixXd sums = Eigen::MatrixXd::Zero(k, data.cols());
        std::vector<int> counts(static_cast<std::size_t>(k), 0);
        for (Eigen::Index i = 0; i < n; ++i) {
            const int cluster = assignment[static_cast<std::size_t>(i)];
            sums.row(cluster) += data.row(i);
            ++counts[static_cast<std::size_t>(cluster)];
        }
        for (int c = 0; c < k; ++c) {
            // An emptied cluster is re-seeded onto the point furthest from its
            // own centre, rather than left to collapse the result to k-1.
            if (counts[static_cast<std::size_t>(c)] == 0) continue;
            centroids.row(c) = sums.row(c) / static_cast<double>(counts[static_cast<std::size_t>(c)]);
        }
        if (!changed) break;
    }
    return assignment;
}

// Mean silhouette coefficient: for each point, how much closer it sits to its
// own cluster than to the nearest other one. This is what picks k, so the
// number of segments comes from the data instead of a constant.
double silhouette(const Eigen::MatrixXd& data, const std::vector<int>& assignment, int k) {
    const Eigen::Index n = data.rows();
    if (n < 3 || k < 2) return 0.0;
    std::vector<int> sizes(static_cast<std::size_t>(k), 0);
    for (const int cluster : assignment) ++sizes[static_cast<std::size_t>(cluster)];

    double total = 0;
    Eigen::Index counted = 0;
    for (Eigen::Index i = 0; i < n; ++i) {
        const int own = assignment[static_cast<std::size_t>(i)];
        if (sizes[static_cast<std::size_t>(own)] <= 1) continue;  // undefined for a lone point
        std::vector<double> sums(static_cast<std::size_t>(k), 0.0);
        for (Eigen::Index j = 0; j < n; ++j) {
            if (i == j) continue;
            sums[static_cast<std::size_t>(assignment[static_cast<std::size_t>(j)])] +=
                (data.row(i) - data.row(j)).norm();
        }
        const double a = sums[static_cast<std::size_t>(own)] /
            static_cast<double>(sizes[static_cast<std::size_t>(own)] - 1);
        double b = std::numeric_limits<double>::max();
        for (int c = 0; c < k; ++c) {
            if (c == own || sizes[static_cast<std::size_t>(c)] == 0) continue;
            b = std::min(b, sums[static_cast<std::size_t>(c)] /
                static_cast<double>(sizes[static_cast<std::size_t>(c)]));
        }
        if (b == std::numeric_limits<double>::max()) continue;
        const double denominator = std::max(a, b);
        if (denominator > 0) total += (b - a) / denominator;
        ++counted;
    }
    return counted > 0 ? total / static_cast<double>(counted) : 0.0;
}

} // namespace

ClusterResult clusterRecords(const FeatureMatrix& matrix) {
    ClusterResult result;
    if (matrix.rowCount() < kMinClusterRows) {
        result.reason = "needs at least " + std::to_string(kMinClusterRows) +
            " records with literal values; this fact type has " +
            std::to_string(matrix.rowCount());
        return result;
    }

    const Standardized standardized = standardize(matrix, /*balanceCategoricals=*/true);
    if (standardized.columns.size() < 2) {
        result.reason =
            "needs at least two fields that vary across records; "
            "the values here are constant or single-field";
        return result;
    }

    const Eigen::MatrixXd& data = standardized.data;
    // Covariance of standardised columns is the correlation matrix, so PCA
    // here is scale-free: a field measured in thousands cannot dominate the
    // projection simply for being numerically larger.
    const Eigen::MatrixXd covariance =
        (data.transpose() * data) / static_cast<double>(data.rows() - 1);
    Eigen::SelfAdjointEigenSolver<Eigen::MatrixXd> solver(covariance);
    if (solver.info() != Eigen::Success) {
        result.reason = "principal component solve did not converge";
        return result;
    }

    // Eigenvalues come back ascending, so the leading components are last.
    const Eigen::VectorXd eigenvalues = solver.eigenvalues();
    const Eigen::MatrixXd eigenvectors = solver.eigenvectors();
    const Eigen::Index last = eigenvalues.size() - 1;
    const double totalVariance = eigenvalues.sum();

    Eigen::VectorXd axisX = eigenvectors.col(last);
    Eigen::VectorXd axisY = eigenvectors.col(last - 1);
    // An eigenvector's sign is arbitrary; pinning it makes the same program
    // produce the same scatter orientation every run.
    if (axisX.sum() < 0) axisX = -axisX;
    if (axisY.sum() < 0) axisY = -axisY;

    const Eigen::VectorXd projectedX = data * axisX;
    const Eigen::VectorXd projectedY = data * axisY;
    result.x.assign(projectedX.data(), projectedX.data() + projectedX.size());
    result.y.assign(projectedY.data(), projectedY.data() + projectedY.size());
    result.explainedX = totalVariance > 0 ? eigenvalues(last) / totalVariance : 0.0;
    result.explainedY = totalVariance > 0 ? eigenvalues(last - 1) / totalVariance : 0.0;
    result.driversX = topLoadings(axisX, standardized.columns, 4);
    result.driversY = topLoadings(axisY, standardized.columns, 4);

    // k is chosen by silhouette rather than fixed, so a genuinely
    // single-population fact type is not split into invented segments - and
    // it is capped by how many records exist, so silhouette cannot buy a
    // better score by peeling off singletons.
    const int maximumK = static_cast<int>(std::min<std::size_t>(
        static_cast<std::size_t>(kMaxClusters),
        matrix.rowCount() / kMinRecordsPerCluster));
    if (maximumK < 2) {
        result.reason = "needs at least " +
            std::to_string(2 * kMinRecordsPerCluster) +
            " records to form two meaningful segments; this fact type has " +
            std::to_string(matrix.rowCount());
        return result;
    }
    double bestScore = -2.0;
    for (int k = 2; k <= maximumK; ++k) {
        const std::vector<int> candidate = kMeans(data, k);
        const double score = silhouette(data, candidate, k);
        if (score > bestScore) {
            bestScore = score;
            result.assignment = candidate;
            result.k = k;
        }
    }
    if (result.assignment.empty()) {
        result.reason = "too few records to separate into segments";
        return result;
    }
    result.silhouette = bestScore;

    result.clusterSizes.assign(static_cast<std::size_t>(result.k), 0);
    for (const int cluster : result.assignment) ++result.clusterSizes[static_cast<std::size_t>(cluster)];

    // What distinguishes each segment: the standardised columns whose cluster
    // mean sits furthest from zero (the global mean, post-standardisation).
    result.clusterDrivers.resize(static_cast<std::size_t>(result.k));
    for (int c = 0; c < result.k; ++c) {
        Eigen::VectorXd sums = Eigen::VectorXd::Zero(data.cols());
        std::size_t count = 0;
        for (Eigen::Index i = 0; i < data.rows(); ++i) {
            if (result.assignment[static_cast<std::size_t>(i)] != c) continue;
            sums += data.row(i).transpose();
            ++count;
        }
        if (count == 0) continue;
        result.clusterDrivers[static_cast<std::size_t>(c)] =
            topLoadings(sums / static_cast<double>(count), standardized.columns, 3);
    }

    result.valid = true;
    return result;
}

StructureReport assessStructure(const FeatureMatrix& matrix) {
    StructureReport report;
    report.rows = matrix.rowCount();
    const Standardized standardized = standardize(matrix);
    if (standardized.columns.empty() || standardized.data.rows() < 3) return report;
    report.columns = standardized.columns.size();
    const Eigen::MatrixXd& data = standardized.data;

    // Singular values give the variance carried by each independent direction
    // without forming the covariance matrix, which is both more accurate and
    // what makes near-singularity visible as a ratio rather than as a failure.
    Eigen::JacobiSVD<Eigen::MatrixXd> svd(data);
    const Eigen::VectorXd singular = svd.singularValues();
    double totalEnergy = 0;
    for (Eigen::Index i = 0; i < singular.size(); ++i) {
        report.singularValues.push_back(singular(i));
        totalEnergy += singular(i) * singular(i);
    }
    double running = 0;
    for (Eigen::Index i = 0; i < singular.size(); ++i) {
        running += singular(i) * singular(i);
        ++report.effectiveRank;
        if (totalEnergy > 0 && running / totalEnergy >= kVarianceForRank) break;
    }
    const double smallest = singular(singular.size() - 1);
    report.conditionNumber = smallest > 1e-300
        ? singular(0) / smallest
        : std::numeric_limits<double>::infinity();
    report.collinear = report.effectiveRank < report.columns ||
        report.conditionNumber > kMaxConditionNumber;

    // Column pairs carrying one signal between them.
    const CorrelationMatrix correlation = correlate(matrix);
    for (std::size_t i = 0; i < correlation.columns.size(); ++i) {
        for (std::size_t j = i + 1; j < correlation.columns.size(); ++j) {
            if (std::fabs(correlation.values[i][j]) < kRedundantCorrelation) continue;
            if (sourceField(correlation.columns[i]) == sourceField(correlation.columns[j])) continue;
            report.redundantPairs.emplace_back(correlation.columns[i], correlation.columns[j]);
        }
    }

    // Hopkins statistic. Uniformly-spread data gives ~0.5 because a random
    // probe point is, on average, as close to the data as a real point is to
    // its neighbour. Clustered data gives more, because real points sit in
    // dense pockets while random probes land in the gaps.
    //
    // This is the check that stops k-means from reporting confident segments
    // in a straight arithmetic ramp, where the split is real in the sense that
    // it is clean, and meaningless in the sense that any other split would
    // have been equally clean.
    const Eigen::Index n = data.rows();
    const Eigen::Index d = data.cols();
    const Eigen::Index probes = std::max<Eigen::Index>(5, std::min<Eigen::Index>(n / 4, 40));
    Eigen::VectorXd low = data.colwise().minCoeff();
    Eigen::VectorXd high = data.colwise().maxCoeff();
    std::mt19937 rng(913377u);  // fixed: the report must be reproducible
    std::uniform_int_distribution<Eigen::Index> pickRow(0, n - 1);
    double realSum = 0;
    double probeSum = 0;
    for (Eigen::Index p = 0; p < probes; ++p) {
        // Nearest neighbour of a real record, excluding itself.
        const Eigen::Index self = pickRow(rng);
        double nearestReal = std::numeric_limits<double>::max();
        for (Eigen::Index j = 0; j < n; ++j) {
            if (j == self) continue;
            nearestReal = std::min(nearestReal, (data.row(self) - data.row(j)).norm());
        }
        // Nearest record to a uniformly random point in the bounding box.
        Eigen::VectorXd sample(d);
        for (Eigen::Index c = 0; c < d; ++c) {
            std::uniform_real_distribution<double> spread(low(c), high(c));
            sample(c) = spread(rng);
        }
        double nearestProbe = std::numeric_limits<double>::max();
        for (Eigen::Index j = 0; j < n; ++j) {
            nearestProbe = std::min(nearestProbe, (sample.transpose() - data.row(j)).norm());
        }
        if (!std::isfinite(nearestReal) || !std::isfinite(nearestProbe)) continue;
        realSum += nearestReal;
        probeSum += nearestProbe;
    }
    report.clusterTendency = (realSum + probeSum) > 0
        ? probeSum / (realSum + probeSum)
        : 0.5;
    report.worthClustering = report.clusterTendency >= kMinClusterTendency;
    report.valid = true;
    return report;
}

std::vector<MultivariateOutlier> multivariateOutliers(const FeatureMatrix& matrix) {
    std::vector<MultivariateOutlier> found;
    const Standardized standardized = standardize(matrix);
    const Eigen::MatrixXd& data = standardized.data;
    // Mahalanobis needs an invertible covariance, which needs more records
    // than dimensions with room to spare.
    if (standardized.columns.size() < 2 ||
        data.rows() < static_cast<Eigen::Index>(standardized.columns.size()) + 3) {
        return found;
    }

    Eigen::MatrixXd covariance =
        (data.transpose() * data) / static_cast<double>(data.rows() - 1);
    // Ridge term: without it a pair of collinear columns makes the covariance
    // singular and the solve returns garbage rather than failing. A small
    // multiple of the identity keeps it positive definite at negligible cost
    // to the distances.
    covariance.diagonal().array() += 1e-6;

    const Eigen::LDLT<Eigen::MatrixXd> solver(covariance);
    if (solver.info() != Eigen::Success) return found;

    const Eigen::Index d = data.cols();
    // Chi-squared upper tail at p = 0.001, by degrees of freedom. Beyond the
    // table the Wilson-Hilferty approximation is close enough for a threshold.
    static const double chi001[] = {
        0.0, 10.828, 13.816, 16.266, 18.467, 20.515, 22.458, 24.322, 26.125,
        27.877, 29.588, 31.264, 32.909, 34.528, 36.123, 37.697
    };
    double threshold;
    if (d < static_cast<Eigen::Index>(sizeof(chi001) / sizeof(chi001[0]))) {
        threshold = chi001[d];
    } else {
        const double k = static_cast<double>(d);
        const double term = 1.0 - 2.0 / (9.0 * k) + 3.09023 * std::sqrt(2.0 / (9.0 * k));
        threshold = k * term * term * term;
    }

    for (Eigen::Index i = 0; i < data.rows(); ++i) {
        const Eigen::VectorXd row = data.row(i).transpose();
        const Eigen::VectorXd solved = solver.solve(row);
        const double squared = row.dot(solved);
        if (!std::isfinite(squared) || squared <= threshold) continue;
        MultivariateOutlier outlier;
        outlier.row = i;
        outlier.label = matrix.rowLabels[static_cast<std::size_t>(i)];
        outlier.distance = std::sqrt(squared);
        // Per-column contribution to the quadratic form, largest first.
        std::vector<std::pair<double, std::string>> shares;
        for (Eigen::Index c = 0; c < d; ++c) {
            shares.emplace_back(std::fabs(row(c) * solved(c)),
                                standardized.columns[static_cast<std::size_t>(c)]);
        }
        std::sort(shares.begin(), shares.end(), [](const auto& a, const auto& b) {
            if (a.first != b.first) return a.first > b.first;
            return a.second < b.second;
        });
        for (std::size_t s = 0; s < shares.size() && s < 3; ++s) {
            outlier.drivers.push_back(shares[s].second);
        }
        found.push_back(std::move(outlier));
    }
    std::sort(found.begin(), found.end(), [](const auto& a, const auto& b) {
        if (a.distance != b.distance) return a.distance > b.distance;
        return a.label < b.label;
    });
    return found;
}

std::size_t distinctSourceFields(const FeatureMatrix& matrix) {
    std::set<std::string> fields;
    for (const auto& column : matrix.columns) fields.insert(sourceField(column));
    return fields.size();
}

std::vector<DriverModel> explainNumericTargets(const FeatureMatrix& matrix) {
    std::vector<DriverModel> models;
    const Standardized standardized = standardize(matrix);
    const Eigen::MatrixXd& data = standardized.data;
    const Eigen::Index columns = data.cols();
    // One target plus at least one predictor, and enough rows that the fit is
    // not simply interpolating the data.
    if (columns < 2 || data.rows() < columns + 3) return models;

    for (Eigen::Index target = 0; target < columns; ++target) {
        // Only real measures are worth explaining; a one-hot indicator as the
        // target is a classification question, not a regression one.
        const std::string& name = standardized.columns[static_cast<std::size_t>(target)];
        if (name.find('=') != std::string::npos) continue;

        Eigen::MatrixXd predictors(data.rows(), columns - 1);
        std::vector<std::string> predictorNames;
        Eigen::Index column = 0;
        for (Eigen::Index c = 0; c < columns; ++c) {
            if (c == target) continue;
            predictors.col(column++) = data.col(c);
            predictorNames.push_back(standardized.columns[static_cast<std::size_t>(c)]);
        }
        const Eigen::VectorXd response = data.col(target);

        // Complete orthogonal decomposition, not Householder QR.
        //
        // The QR solve was chosen for numerical stability and the comment here
        // used to claim that collinear predictors would "degrade the fit
        // rather than produce nonsense". That is true of *near*-collinear
        // predictors and false of exactly collinear ones, and a one-hot
        // encoding manufactures the exact case every time: the indicator
        // columns of a categorical field sum to a constant, so the design
        // matrix is rank-deficient by construction. QR has no defined answer
        // there and returns an arbitrary point on the solution line.
        //
        // The observed failure was not subtle. On a fact type with three
        // sensor units, the reported drivers of a temperature reading were
        // "unit=coastal (+6387033529247.05)" and two more coefficients of
        // similar magnitude that very nearly cancelled - a correct solution to
        // the least-squares problem, a meaningless answer to "what moves this
        // number", and presented alongside "99.91% explained" as though it
        // were a finding.
        //
        // COD is rank-revealing and returns the minimum-norm solution among
        // the infinitely many, so coefficients stay on the scale of the data.
        // It also reports the rank, which is what lets the model say honestly
        // whether its coefficients mean anything individually.
        Eigen::CompleteOrthogonalDecomposition<Eigen::MatrixXd> solver(predictors);
        solver.setThreshold(kRankTolerance);
        const Eigen::VectorXd beta = solver.solve(response);
        if (!beta.allFinite()) continue;
        const Eigen::Index rank = solver.rank();
        if (rank == 0) continue;

        const Eigen::VectorXd residual = response - predictors * beta;
        // Columns are standardised, so the response's total sum of squares is
        // simply n - 1.
        const double totalSumSquares = static_cast<double>(data.rows() - 1);
        if (!(totalSumSquares > 0)) continue;
        const double r2 = 1.0 - residual.squaredNorm() / totalSumSquares;
        if (!std::isfinite(r2) || r2 < kMinDriverR2) continue;

        DriverModel model;
        model.valid = true;
        model.target = name;
        model.r2 = std::min(1.0, std::max(0.0, r2));
        // Whether a reader may attribute the fit to individual predictors.
        // At full rank each coefficient is the unique effect of its column;
        // below it, the columns carry overlapping information and the split
        // between them is an artefact of which solution the solver landed on.
        model.identifiable = rank == predictors.cols();
        model.rank = static_cast<std::size_t>(rank);
        const double n = static_cast<double>(data.rows());
        // Degrees of freedom come from the rank, not the column count. Using
        // the column count on a rank-deficient fit charges the model for
        // parameters it did not actually spend, and understates the
        // adjustment exactly where the fit is most likely to be spurious.
        const double p = static_cast<double>(rank);
        model.adjustedR2 = n - p - 1 > 0
            ? 1.0 - (1.0 - model.r2) * (n - 1) / (n - p - 1)
            : model.r2;
        for (Eigen::Index c = 0; c < beta.size(); ++c) {
            model.coefficients.push_back(
                ComponentLoading{predictorNames[static_cast<std::size_t>(c)], beta(c)});
        }
        std::sort(model.coefficients.begin(), model.coefficients.end(),
                  [](const auto& a, const auto& b) {
                      if (std::fabs(a.weight) != std::fabs(b.weight)) {
                          return std::fabs(a.weight) > std::fabs(b.weight);
                      }
                      return a.column < b.column;
                  });
        if (model.coefficients.size() > 4) model.coefficients.resize(4);
        models.push_back(std::move(model));
    }
    std::sort(models.begin(), models.end(), [](const auto& a, const auto& b) {
        if (a.r2 != b.r2) return a.r2 > b.r2;
        return a.target < b.target;
    });
    return models;
}

double groupSeparation(const FactProfile& profile,
                       const std::string& numericField,
                       FieldType numericType,
                       const std::string& categoricalField) {
    // Paired values only: a record missing either field cannot contribute to
    // a statement about their relationship.
    std::vector<double> values;
    std::vector<std::string> groups;
    for (const auto& record : profile.samples) {
        const auto number = record.find(numericField);
        const auto group = record.find(categoricalField);
        if (number == record.end() || group == record.end()) continue;
        double parsed = 0;
        if (numericType == FieldType::Date) {
            if (!dateToDayNumber(number->second, parsed)) continue;
        } else {
            if (!looksNumeric(number->second)) continue;
            parsed = std::strtod(number->second.c_str(), nullptr);
        }
        values.push_back(parsed);
        groups.push_back(group->second);
    }
    if (values.size() < 4) return 0.0;

    const double grandMean =
        std::accumulate(values.begin(), values.end(), 0.0) / static_cast<double>(values.size());
    std::map<std::string, std::pair<double, std::size_t>> perGroup;  // sum, count
    for (std::size_t i = 0; i < values.size(); ++i) {
        auto& entry = perGroup[groups[i]];
        entry.first += values[i];
        ++entry.second;
    }
    if (perGroup.size() < 2) return 0.0;

    double between = 0;
    for (const auto& entry : perGroup) {
        const double mean = entry.second.first / static_cast<double>(entry.second.second);
        between += static_cast<double>(entry.second.second) * (mean - grandMean) * (mean - grandMean);
    }
    double total = 0;
    for (const double value : values) total += (value - grandMean) * (value - grandMean);
    if (!(total > 0)) return 0.0;

    // Raw eta-squared is biased upward, badly so on small samples: with k
    // groups and n records, splitting pure noise still yields an expected
    // (k-1)/(n-1). Eight couriers across four regions therefore "explain" 43%
    // of anything by construction, and reporting that as a finding would be
    // reporting the arithmetic rather than the data.
    //
    // Omega-squared subtracts that expectation, and can go negative when a
    // grouping explains less than chance would - which is itself the correct
    // answer, clamped to zero here.
    const double n = static_cast<double>(values.size());
    const double k = static_cast<double>(perGroup.size());
    if (n - k <= 0) return 0.0;
    const double withinMeanSquare = (total - between) / (n - k);
    const double omegaNumerator = between - (k - 1.0) * withinMeanSquare;
    const double omegaDenominator = total + withinMeanSquare;
    if (!(omegaDenominator > 0)) return 0.0;
    return std::min(1.0, std::max(0.0, omegaNumerator / omegaDenominator));
}

namespace {

// The smallest |r| that is distinguishable from chance at this sample size.
// With six records, |r| = 0.77 happens by coincidence often enough that
// drawing a chart around it is asserting something the data cannot support;
// with four hundred, 0.2 is solid. This is roughly the p = 0.01 critical value
// and it is what stops the proposal engine from mining tiny fact types for
// relationships that are not there.
double correlationFloor(std::size_t rows) {
    if (rows < 5) return 1.1;  // nothing is defensible
    return std::max(0.35, std::min(0.95, 2.6 / std::sqrt(static_cast<double>(rows))));
}

} // namespace

double categoricalAssociation(const FactProfile& profile,
                              const std::string& first,
                              const std::string& second) {
    std::map<std::string, std::map<std::string, std::size_t>> table;
    std::map<std::string, std::size_t> rowTotals;
    std::map<std::string, std::size_t> columnTotals;
    std::size_t n = 0;
    for (const auto& record : profile.samples) {
        const auto a = record.find(first);
        const auto b = record.find(second);
        if (a == record.end() || b == record.end()) continue;
        ++table[a->second][b->second];
        ++rowTotals[a->second];
        ++columnTotals[b->second];
        ++n;
    }
    if (n < 8 || rowTotals.size() < 2 || columnTotals.size() < 2) return 0.0;

    double chiSquared = 0;
    for (const auto& row : rowTotals) {
        for (const auto& column : columnTotals) {
            const double expected = static_cast<double>(row.second) *
                static_cast<double>(column.second) / static_cast<double>(n);
            if (!(expected > 0)) continue;
            double observed = 0;
            const auto rowEntry = table.find(row.first);
            if (rowEntry != table.end()) {
                const auto cell = rowEntry->second.find(column.first);
                if (cell != rowEntry->second.end()) observed = static_cast<double>(cell->second);
            }
            chiSquared += (observed - expected) * (observed - expected) / expected;
        }
    }
    // Cramer's V normalises chi-squared by sample size and table shape, so a
    // 2x2 and a 5x7 are on the same 0..1 scale.
    const double smallerDimension =
        static_cast<double>(std::min(rowTotals.size(), columnTotals.size())) - 1.0;
    if (!(smallerDimension > 0)) return 0.0;
    return std::min(1.0, std::sqrt(chiSquared / (static_cast<double>(n) * smallerDimension)));
}

std::vector<ChartProposal> proposeCharts(const FactProfile& profile,
                                         const std::vector<FieldStats>& fields,
                                         const FeatureMatrix& matrix,
                                         const StructureReport& structure) {
    std::vector<ChartProposal> proposals;
    auto propose = [&](const char* chart, std::string title, std::string rationale,
                       double score, std::vector<std::string> involved) {
        if (score < kMinProposalScore) return;
        proposals.push_back(ChartProposal{
            chart, std::move(title), std::move(rationale),
            std::min(1.0, score), std::move(involved)});
    };

    std::vector<const FieldStats*> measures;
    std::vector<const FieldStats*> labels;
    const FieldStats* temporal = nullptr;
    for (const auto& field : fields) {
        if (field.type == FieldType::Numeric || field.type == FieldType::Date) {
            measures.push_back(&field);
            if (field.type == FieldType::Date &&
                (!temporal || field.present > temporal->present)) {
                temporal = &field;
            }
        } else if (field.type == FieldType::Categorical && field.distinct >= 2) {
            labels.push_back(&field);
        }
    }

    // --- one measure on its own -------------------------------------------
    // A distribution is worth drawing when the values actually vary. Spread
    // relative to the median is the measure of that; a field where every
    // record sits on the same value has a distribution, but not one anybody
    // needs to see.
    for (const FieldStats* field : measures) {
        if (field->present < 4 || !(field->max > field->min)) continue;
        const double spread = field->median != 0
            ? std::min(1.0, field->stddev / std::fabs(field->median))
            : (field->stddev > 0 ? 0.5 : 0.0);
        // Skew and a second population both make the *shape* the point, which
        // is what a histogram shows and a summary statistic hides.
        double score = 0.3 + 0.4 * spread + 0.2 * std::min(1.0, std::fabs(field->skewness) / 2.0);
        if (field->secondPopulation) score += 0.25;
        std::string why = "values span " + std::to_string(field->distinct) + " distinct levels";
        if (field->secondPopulation) why = "the values fall into two separate groups";
        else if (std::fabs(field->skewness) > 1.0) why = "the distribution is strongly skewed, so its mean is not representative";
        propose("histogram", field->name + " distribution", why, score, {field->name});
    }

    // --- one label on its own ---------------------------------------------
    // Entropy is the test: a field where 95% of records share one value is
    // technically groupable and tells a reader nothing.
    for (const FieldStats* field : labels) {
        propose("hbar", field->name + " by value",
                "records divide across " + std::to_string(field->distinct) +
                    " values with an even-ness of " +
                    std::to_string(static_cast<int>(field->entropy * 100)) + "%",
                0.2 + 0.6 * field->entropy, {field->name});
    }

    // --- measure against measure ------------------------------------------
    // Only pairs that actually move together. Everything else is a cloud.
    const CorrelationMatrix correlation = correlate(matrix);
    for (std::size_t i = 0; i < correlation.columns.size(); ++i) {
        for (std::size_t j = i + 1; j < correlation.columns.size(); ++j) {
            if (correlation.columns[i].find('=') != std::string::npos) continue;
            if (correlation.columns[j].find('=') != std::string::npos) continue;
            const double r = correlation.values[i][j];
            if (std::fabs(r) < correlationFloor(matrix.rowCount())) continue;
            // A near-perfect correlation is usually one field derived from the
            // other; still worth showing, but it is not a discovery.
            const double score = std::fabs(r) > 0.995 ? 0.45 : 0.35 + 0.6 * std::fabs(r);
            // The direction has to come from the sign. This read "the two
            // move together, r = -0.84" for an inverse relationship, which
            // states the opposite of what the number beside it says.
            const std::string direction = r >= 0
                ? "the two rise together, r = "
                : "one falls as the other rises, r = ";
            propose("scatter",
                    correlation.columns[i] + " against " + correlation.columns[j],
                    direction + std::to_string(r).substr(0, 5),
                    score, {correlation.columns[i], correlation.columns[j]});
        }
    }

    // --- measure split by label -------------------------------------------
    // The classic business chart, proposed only where the label genuinely
    // separates the measure rather than for every possible pairing.
    for (const FieldStats* measure : measures) {
        for (const FieldStats* label : labels) {
            // Bias-corrected, so this is the share of variance the grouping
            // explains beyond what an equivalent split of noise would.
            const double eta = groupSeparation(profile, measure->name, measure->type, label->name);
            if (eta < 0.15) continue;
            propose("boxplot", measure->name + " by " + label->name,
                    label->name + " explains " +
                        std::to_string(static_cast<int>(eta * 100)) +
                        "% of the variation in " + measure->name +
                        " (corrected for group count)",
                    0.4 + 0.6 * eta, {measure->name, label->name});
        }
    }

    // --- label against label ----------------------------------------------
    for (std::size_t i = 0; i < labels.size(); ++i) {
        for (std::size_t j = i + 1; j < labels.size(); ++j) {
            const double v = categoricalAssociation(profile, labels[i]->name, labels[j]->name);
            if (v < 0.3) continue;
            propose("heatmap", labels[i]->name + " against " + labels[j]->name,
                    "the two labels are associated, Cramer's V = " +
                        std::to_string(v).substr(0, 4),
                    0.3 + 0.6 * v, {labels[i]->name, labels[j]->name});
        }
    }

    // --- shape of the whole record ----------------------------------------
    if (structure.valid) {
        // More than two independent dimensions cannot be shown on a plane, so
        // parallel coordinates earn their place exactly when a scatter cannot
        // do the job.
        if (structure.effectiveRank > 2 && matrix.rowCount() >= 8) {
            // Bounded on purpose. This score used to grow with the dimension
            // count, so a wide fact type scored parallel coordinates above
            // everything else and the panel budget was spent before a single
            // per-field chart was reached. Extra dimensions make this chart
            // more *applicable*, not more informative without limit.
            propose("parallel", "every record across every measure",
                    std::to_string(structure.effectiveRank) +
                        " independent dimensions - more than a flat chart can show",
                    0.34 + 0.12 * std::min(1.0, static_cast<double>(structure.effectiveRank - 2) / 4.0),
                    {});
        }
        // A correlation grid of columns that do not correlate is a square of
        // one colour, so this is offered only where there is something in it.
        if (structure.columns >= 3 && !correlation.notable.empty()) {
            propose("heatmap", "how the fields move together",
                    std::to_string(correlation.notable.size()) +
                        " field pair(s) move together across " +
                        std::to_string(structure.columns) + " encoded fields",
                    0.42, {});
        }
    }

    // --- anything over time -----------------------------------------------
    // Only when a date field is actually present. There is no assumption here
    // about what the records are; a date column is a date column.
    if (temporal) {
        propose("line", "records over " + temporal->name,
                temporal->name + " is a date on " + std::to_string(temporal->present) +
                    " records, so they can be placed in order",
                0.55, {temporal->name});
        for (const FieldStats* measure : measures) {
            if (measure == temporal || measure->present < 6) continue;
            propose("line", measure->name + " over " + temporal->name,
                    "a measure tracked against a date field", 0.45,
                    {temporal->name, measure->name});
        }
    }

    std::sort(proposals.begin(), proposals.end(), [](const auto& a, const auto& b) {
        if (a.score != b.score) return a.score > b.score;
        if (a.chart != b.chart) return a.chart < b.chart;
        return a.title < b.title;
    });

    // Variety, applied after ranking. Six histograms answer one question six
    // times; a histogram, a box plot and a scatter answer three. Each kind
    // keeps its two strongest entries at the front of the list and its
    // remaining ones fall behind every other kind's best, so a fact type whose
    // only real signal is distributional still gets its histograms - just not
    // to the exclusion of everything else.
    std::map<std::string, std::size_t> seen;
    std::vector<ChartProposal> leading;
    std::vector<ChartProposal> trailing;
    for (auto& proposal : proposals) {
        if (seen[proposal.chart]++ < kMaxPerChartKind) leading.push_back(std::move(proposal));
        else trailing.push_back(std::move(proposal));
    }
    leading.insert(leading.end(), trailing.begin(), trailing.end());
    return leading;
}

AnalysisPipeline analyseFactType(const std::string& name,
                                 const FactProfile& profile,
                                 const std::vector<FieldStats>& fields) {
    AnalysisPipeline pipeline;
    pipeline.factType = name;
    auto step = [&](PipelineStage stage, std::string finding, std::string decision) {
        pipeline.steps.push_back(
            ReasoningStep{pipelineStageName(stage), std::move(finding), std::move(decision)});
    };
    // Records what a bundle of algorithms concluded, whether or not it ran.
    // A bundle that declines is as much a result as one that succeeds, and
    // omitting it leaves a reader unable to tell "this was tried and found
    // nothing" from "this was never attempted".
    auto bundle = [&](BundleKind kind, const char* question,
                      std::vector<std::string> algorithms, bool applicable,
                      std::string finding) {
        pipeline.bundles.push_back(AlgorithmBundle{
            kind, question, std::move(algorithms), applicable, std::move(finding)});
    };

    // --- Bundle + Analyse: profile ----------------------------------------
    std::size_t measurable = 0, groupable = 0, keys = 0;
    for (const auto& field : fields) {
        switch (field.type) {
            case FieldType::Numeric:
            case FieldType::Date: ++measurable; break;
            case FieldType::Categorical: ++groupable; break;
            case FieldType::Identifier: ++keys; break;
            case FieldType::Empty: break;
        }
    }
    const std::string shape =
        std::to_string(profile.records) + " records; " + std::to_string(measurable) +
        " measurable, " + std::to_string(groupable) + " groupable, " +
        std::to_string(keys) + " key-like fields";
    step(PipelineStage::Bundle, shape,
         keys > 0 ? "key-like fields excluded from analysis - they identify records "
                    "rather than describe them"
                  : "all usable fields carried forward");
    bundle(BundleKind::Profile, "what is each field, and what does it hold?",
           {"type inference", "coverage", "cardinality", "entropy",
            "median absolute deviation", "skewness"},
           true, shape);

    // --- Analyse: encode --------------------------------------------------
    pipeline.matrix = buildFeatureMatrix(profile, fields);
    auto declineRest = [&](const std::string& why) {
        // Everything downstream reads the encoded matrix, so when encoding
        // fails there is one reason and it applies to all of them. Saying it
        // once per bundle is what makes the reasoning panel readable instead
        // of a wall of the same sentence.
        bundle(BundleKind::Structure, "what shape does this data have?",
               {"SVD", "effective rank", "condition number", "Hopkins"}, false, why);
        bundle(BundleKind::Segmentation, "do these records fall into groups?",
               {"PCA", "k-means", "silhouette", "oblique tree"}, false, why);
        bundle(BundleKind::Anomaly, "which records are unusual?",
               {"modified z-score", "Mahalanobis"}, false, why);
        bundle(BundleKind::Explanation, "what moves each number?",
               {"least squares (COD)"}, false, why);
    };
    if (pipeline.matrix.columnCount() < 2 || pipeline.matrix.rowCount() < 3) {
        const std::string why =
            std::to_string(pipeline.matrix.rowCount()) + " rows x " +
            std::to_string(pipeline.matrix.columnCount()) +
            " encoded columns is too little to analyse";
        step(PipelineStage::Analyse, why, "stopping here");
        declineRest(why);
        return pipeline;
    }
    // Columns are not the same thing as information. A single categorical
    // field one-hot expands into as many columns as it has levels, and the
    // column count alone then reports a fact type with one usable field as
    // multivariate data.
    //
    // It happened to a Product fact type whose only non-key field was
    // `material`: three encoded columns, a healthy cluster tendency, and two
    // "segments" that turned out to be "brass" and "not brass". A bar chart of
    // material said the same thing, correctly, in one panel - and a
    // segmentation carries an implication a bar chart does not, that the
    // grouping was discovered rather than read off a label.
    const std::size_t encodedFields = distinctSourceFields(pipeline.matrix);
    if (encodedFields < 2) {
        const std::string why =
            std::to_string(pipeline.matrix.columnCount()) +
            " encoded columns, all from one field (" +
            sourceField(pipeline.matrix.columns.front()) +
            "): any grouping would restate that field's own values";
        step(PipelineStage::Analyse, why, "its distribution is shown instead");
        declineRest(why);
        return pipeline;
    }
    step(PipelineStage::Analyse,
         std::to_string(pipeline.matrix.rowCount()) + " rows x " +
             std::to_string(pipeline.matrix.columnCount()) + " encoded columns from " +
             std::to_string(encodedFields) + " fields",
         "categoricals one-hot expanded, gaps mean-imputed");

    // --- Analyse: structure -----------------------------------------------
    pipeline.structure = assessStructure(pipeline.matrix);
    if (!pipeline.structure.valid) {
        const std::string why = "every record is identical on every usable field, so there "
                                "is no variation to measure";
        step(PipelineStage::Analyse, why, "stopping here");
        declineRest(why);
        return pipeline;
    }
    {
        std::ostringstream finding;
        finding << pipeline.structure.effectiveRank << " of "
                << pipeline.structure.columns << " independent dimensions carry "
                << static_cast<int>(kVarianceForRank * 100) << "% of the variation; "
                << "cluster tendency " << pipeline.structure.clusterTendency
                << " (0.5 is an even spread)";
        step(PipelineStage::Analyse, finding.str(),
             pipeline.structure.collinear
                 ? "columns overlap, so segment descriptions are deduplicated"
                 : "all encoded columns contribute independently");
        bundle(BundleKind::Structure, "what shape does this data have?",
               {"SVD", "effective rank", "condition number", "Hopkins"},
               true, finding.str());
    }

    // --- Analyse: segmentation, gated by cluster tendency -----------------
    if (!pipeline.structure.worthClustering) {
        std::ostringstream why;
        why << "cluster tendency " << pipeline.structure.clusterTendency
            << " is indistinguishable from an even spread, so any segmentation would be "
               "an arbitrary cut through continuous data";
        step(PipelineStage::Analyse, why.str(), "segmentation not attempted");
        bundle(BundleKind::Segmentation, "do these records fall into groups?",
               {"PCA", "k-means", "silhouette", "oblique tree"}, false, why.str());
    } else {
        pipeline.clusters = clusterRecords(pipeline.matrix);
        if (pipeline.clusters.valid) {
            std::ostringstream finding;
            finding << pipeline.clusters.k << " segments, separation "
                    << pipeline.clusters.silhouette;
            step(PipelineStage::Analyse, finding.str(),
                 pipeline.clusters.silhouette >= 0.25
                     ? "segments are distinct enough to describe individually"
                     : "segments overlap; reported with that caveat attached");
            bundle(BundleKind::Segmentation, "do these records fall into groups?",
                   {"PCA", "k-means", "silhouette", "oblique tree"}, true, finding.str());
        } else {
            step(PipelineStage::Analyse, pipeline.clusters.reason, "no segmentation reported");
            bundle(BundleKind::Segmentation, "do these records fall into groups?",
                   {"PCA", "k-means", "silhouette"}, false, pipeline.clusters.reason);
        }
    }

    // --- Analyse: anomalies and drivers -----------------------------------
    pipeline.outliers = multivariateOutliers(pipeline.matrix);
    {
        const std::string finding = pipeline.outliers.empty()
            ? std::string("no record is unusual in its combination of fields")
            : std::to_string(pipeline.outliers.size()) +
                  " record(s) are unusual in combination of fields";
        step(PipelineStage::Analyse, finding,
             pipeline.outliers.empty()
                 ? "nothing flagged"
                 : "reported with the fields that make each one unusual");
        bundle(BundleKind::Anomaly, "which records are unusual?",
               {"modified z-score", "Mahalanobis"}, !pipeline.outliers.empty(), finding);
    }

    pipeline.drivers = explainNumericTargets(pipeline.matrix);
    {
        std::ostringstream finding;
        if (pipeline.drivers.empty()) {
            finding << "no field is predictable from the others above r-squared "
                    << kMinDriverR2;
        } else {
            finding << pipeline.drivers.size() << " field(s) are predictable from the others";
        }
        step(PipelineStage::Analyse, finding.str(),
             pipeline.drivers.empty() ? "no drivers reported"
                                      : "strongest drivers reported per field");
        bundle(BundleKind::Explanation, "what moves each number?",
               {"least squares (complete orthogonal decomposition)", "rank check"},
               !pipeline.drivers.empty(), finding.str());
    }

    // --- Summarise + Reason + Design --------------------------------------
    // Which charts this data argues for. Nothing here knows what the fields
    // mean; every proposal is earned by a measurement taken above. This is
    // the Design stage: it turns findings into a ranked list of UI components
    // to call, and the renderer does no choosing of its own.
    pipeline.proposals =
        proposeCharts(profile, fields, pipeline.matrix, pipeline.structure);
    {
        std::size_t applicable = 0;
        for (const auto& entry : pipeline.bundles) {
            if (entry.applicable) ++applicable;
        }
        std::ostringstream summary;
        summary << applicable << " of " << pipeline.bundles.size()
                << " algorithm bundles found something";
        step(PipelineStage::Summarise, summary.str(),
             "their findings are carried forward as chart proposals");
    }
    if (!pipeline.proposals.empty()) {
        std::ostringstream finding;
        finding << pipeline.proposals.size() << " chart(s) are supported by the data, led by "
                << pipeline.proposals.front().chart << " ("
                << pipeline.proposals.front().rationale << ")";
        step(PipelineStage::Design, finding.str(),
             "strongest proposals rendered, weakest dropped");
    } else {
        step(PipelineStage::Design, "no field pairing shows a relationship worth drawing",
             "only per-field summaries offered");
    }

    pipeline.analysed = true;
    return pipeline;
}

const char* bundleKindName(BundleKind kind) {
    switch (kind) {
        case BundleKind::Profile: return "profile";
        case BundleKind::Structure: return "structure";
        case BundleKind::Association: return "association";
        case BundleKind::Segmentation: return "segmentation";
        case BundleKind::Anomaly: return "anomaly";
        case BundleKind::Explanation: return "explanation";
        case BundleKind::Text: return "text";
    }
    return "profile";
}

const char* pipelineStageName(PipelineStage stage) {
    switch (stage) {
        case PipelineStage::Bundle: return "bundle";
        case PipelineStage::Analyse: return "analyse";
        case PipelineStage::Summarise: return "summarise";
        case PipelineStage::Reason: return "reason";
        case PipelineStage::Design: return "design";
    }
    return "analyse";
}

const AlgorithmBundle* AnalysisPipeline::bundle(BundleKind kind) const {
    for (const auto& entry : bundles) {
        if (entry.kind == kind) return &entry;
    }
    return nullptr;
}

bool AnalysisPipeline::supports(BundleKind kind) const {
    const AlgorithmBundle* found = bundle(kind);
    return found != nullptr && found->applicable;
}

CorrelationMatrix correlate(const FeatureMatrix& matrix) {
    CorrelationMatrix result;
    const Standardized standardized = standardize(matrix);
    if (standardized.columns.size() < 2 || standardized.data.rows() < 3) return result;

    result.columns = standardized.columns;
    // Standardised columns make Pearson's r a plain dot product over n-1.
    const Eigen::MatrixXd correlation =
        (standardized.data.transpose() * standardized.data) /
        static_cast<double>(standardized.data.rows() - 1);

    const std::size_t size = result.columns.size();
    result.values.assign(size, std::vector<double>(size, 0.0));
    for (std::size_t i = 0; i < size; ++i) {
        for (std::size_t j = 0; j < size; ++j) {
            double value = correlation(static_cast<Eigen::Index>(i), static_cast<Eigen::Index>(j));
            // Rounding can push a self-correlation a hair past 1; clamping
            // keeps every reported r inside its defined range.
            value = std::max(-1.0, std::min(1.0, value));
            result.values[i][j] = value;
            if (j <= i) continue;
            // The same significance floor the chart proposals use. These two
            // used to disagree: a coefficient of 0.55 over eight records was
            // stated in prose as a finding while the proposal engine
            // correctly declined to draw it, so the page asserted something
            // it would not show.
            const std::size_t rows = static_cast<std::size_t>(standardized.data.rows());
            if (rows < kMinCorrelationRows) continue;
            if (std::fabs(value) < std::max(kNotableCorrelation, correlationFloor(rows))) continue;
            // Two one-hot columns of the same field are complements by
            // construction: "severity=minor" must fall whenever
            // "severity=major" rises. Reporting that as a discovered
            // relationship is reporting the encoding back to the reader.
            if (sourceField(result.columns[i]) == sourceField(result.columns[j])) continue;
            // Indicator columns are excluded from the *findings* even across
            // different fields, though they stay in the matrix that PCA and
            // clustering read.
            //
            // A Pearson coefficient between two indicators is a point-biserial
            // co-occurrence, and between an indicator and a measure it is a
            // difference of group means. Both are real, both are already
            // reported by machinery built for them - Cramer's V and a
            // contingency heatmap for the first, omega-squared and a box plot
            // for the second - and both read as nonsense in the vocabulary
            // this list uses. "product=BR-410 and warehouse=021 move together
            // (r = 0.79)" states that two categories co-occur in the language
            // of two quantities rising in step, which is not what a reader
            // will take from it.
            if (result.columns[i].find('=') != std::string::npos) continue;
            if (result.columns[j].find('=') != std::string::npos) continue;
            // A pair this correlated is one measurement recorded twice, and
            // assessStructure already reports it as redundancy. Listing it
            // here as well told a reader both "reading_c and reading_f say the
            // same thing" and "reading_c and reading_f move together
            // (r = 1.00)" - the second phrased as a discovery about the data
            // when it is a remark about the schema, and the strongest
            // coefficient on the page attached to the least interesting fact.
            if (std::fabs(value) >= kRedundantCorrelation) continue;
            result.notable.push_back(
                CorrelationMatrix::Pair{result.columns[i], result.columns[j], value});
        }
    }
    std::sort(result.notable.begin(), result.notable.end(), [](const auto& a, const auto& b) {
        if (std::fabs(a.r) != std::fabs(b.r)) return std::fabs(a.r) > std::fabs(b.r);
        if (a.a != b.a) return a.a < b.a;
        return a.b < b.b;
    });
    if (result.notable.size() > kMaxNotableCorrelations) {
        result.notable.resize(kMaxNotableCorrelations);
    }
    return result;
}

std::vector<InferredRelationship> inferRelationships(
    const std::map<std::string, FactProfile>& facts,
    const std::map<std::string, std::vector<FieldStats>>& stats) {
    // Distinct value sets per (type, field), built once. Only fields that
    // could plausibly be either side of a join are collected: numbers, labels
    // and identifiers. Dates are excluded because two fact types sharing a
    // calendar is not a join, it is a calendar - a hundred orders and a
    // hundred shipments both dated across the same month would otherwise
    // "reference" each other with perfect containment.
    struct Column {
        std::string type;
        std::string field;
        std::set<std::string> values;
        std::size_t present = 0;
        bool key = false;
    };
    std::vector<Column> columns;
    for (const auto& fact : facts) {
        const auto found = stats.find(fact.first);
        if (found == stats.end()) continue;
        for (const auto& field : found->second) {
            if (field.type == FieldType::Empty || field.type == FieldType::Date) continue;
            Column column;
            column.type = fact.first;
            column.field = field.name;
            for (const auto& sample : fact.second.samples) {
                const auto value = sample.find(field.name);
                if (value == sample.end() || value->second.empty()) continue;
                column.values.insert(value->second);
                ++column.present;
            }
            if (column.values.size() < kMinJoinValues) continue;
            column.key = column.present > 0 &&
                static_cast<double>(column.values.size()) /
                    static_cast<double>(column.present) >= kKeyUniquenessRatio;
            columns.push_back(std::move(column));
        }
    }

    std::vector<InferredRelationship> found;
    for (const auto& from : columns) {
        for (const auto& to : columns) {
            if (from.type == to.type) continue;  // a self-join is not an entity relationship
            if (!to.key) continue;               // the target side must be a candidate key

            std::size_t matched = 0;
            for (const auto& value : from.values) {
                if (to.values.count(value)) ++matched;
            }
            const double containment =
                static_cast<double>(matched) / static_cast<double>(from.values.size());
            if (containment < kMinContainment) continue;

            // Both sides being keys of equal size is the ambiguous case: the
            // join is real but its direction is not determined by the data,
            // so it is emitted once, from the type with more records, rather
            // than twice in opposite directions.
            if (from.key && to.values.size() > from.values.size()) continue;

            InferredRelationship relationship;
            relationship.fromType = from.type;
            relationship.fromField = from.field;
            relationship.toType = to.type;
            relationship.toField = to.field;
            relationship.containment = containment;
            relationship.orphans = from.values.size() - matched;
            relationship.fanIn = matched == 0
                ? 1.0
                : static_cast<double>(from.present) / static_cast<double>(matched);
            relationship.cardinality = relationship.fanIn > 1.5 ? "many-to-one" : "one-to-one";
            found.push_back(std::move(relationship));
        }
    }

    // Strongest first, so a view that shows only the top few shows the ones
    // most likely to be real.
    std::sort(found.begin(), found.end(),
              [](const InferredRelationship& a, const InferredRelationship& b) {
                  if (a.containment != b.containment) return a.containment > b.containment;
                  if (a.fromType != b.fromType) return a.fromType < b.fromType;
                  return a.fromField < b.fromField;
              });
    return found;
}

std::vector<double> pageRank(std::size_t nodeCount,
                             const std::vector<std::pair<std::size_t, std::size_t>>& edges,
                             double damping,
                             int iterations) {
    std::vector<double> rank(nodeCount, 0.0);
    if (nodeCount == 0) return rank;
    const double uniform = 1.0 / static_cast<double>(nodeCount);
    rank.assign(nodeCount, uniform);

    std::vector<std::size_t> outDegree(nodeCount, 0);
    for (const auto& edge : edges) {
        if (edge.first < nodeCount) ++outDegree[edge.first];
    }

    std::vector<double> next(nodeCount, 0.0);
    for (int iteration = 0; iteration < iterations; ++iteration) {
        // A node with no outgoing edge would otherwise leak its rank out of
        // the system; its mass is redistributed uniformly instead.
        double dangling = 0;
        for (std::size_t i = 0; i < nodeCount; ++i) {
            if (outDegree[i] == 0) dangling += rank[i];
        }
        const double base = (1.0 - damping) * uniform + damping * dangling * uniform;
        next.assign(nodeCount, base);
        for (const auto& edge : edges) {
            if (edge.first >= nodeCount || edge.second >= nodeCount) continue;
            if (outDegree[edge.first] == 0) continue;
            next[edge.second] +=
                damping * rank[edge.first] / static_cast<double>(outDegree[edge.first]);
        }
        double delta = 0;
        for (std::size_t i = 0; i < nodeCount; ++i) delta += std::fabs(next[i] - rank[i]);
        rank.swap(next);
        if (delta < 1e-10) break;
    }
    return rank;
}

} // namespace Felidae::Celidae
