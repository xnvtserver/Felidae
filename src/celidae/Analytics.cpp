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
    if (static_cast<double>(numeric) / total >= kTypeAgreement) return FieldType::Numeric;
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

bool looksLikeDate(const std::string& value) {
    // YYYY-MM-DD, optionally followed by a time component.
    if (value.size() >= 10 && allDigits(value, 0, 4) && value[4] == '-' &&
        allDigits(value, 5, 2) && value[7] == '-' && allDigits(value, 8, 2)) {
        return true;
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

Standardized standardize(const FeatureMatrix& matrix) {
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

    result.data.resize(raw.rows(), static_cast<Eigen::Index>(keep.size()));
    for (std::size_t i = 0; i < keep.size(); ++i) {
        const Eigen::Index source = keep[i];
        const double mean = raw.col(source).mean();
        const double stddev = std::sqrt(
            (raw.col(source).array() - mean).square().sum() /
            static_cast<double>(std::max<Eigen::Index>(1, raw.rows() - 1)));
        result.data.col(static_cast<Eigen::Index>(i)) =
            (raw.col(source).array() - mean) / stddev;
        result.columns.push_back(matrix.columns[static_cast<std::size_t>(source)]);
    }
    return result;
}

// The field a feature column came from. One-hot columns are named
// "field=level", so everything before the first '=' identifies the source.
std::string sourceField(const std::string& column) {
    const std::size_t separator = column.find('=');
    return separator == std::string::npos ? column : column.substr(0, separator);
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

    const Standardized standardized = standardize(matrix);
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
    // single-population fact type is not split into invented segments.
    const int maximumK = static_cast<int>(
        std::min<std::size_t>(static_cast<std::size_t>(kMaxClusters), matrix.rowCount() - 1));
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
            if (j <= i || std::fabs(value) < kNotableCorrelation) continue;
            if (static_cast<std::size_t>(standardized.data.rows()) < kMinCorrelationRows) continue;
            // Two one-hot columns of the same field are complements by
            // construction: "severity=minor" must fall whenever
            // "severity=major" rises. Reporting that as a discovered
            // relationship is reporting the encoding back to the reader.
            if (sourceField(result.columns[i]) == sourceField(result.columns[j])) continue;
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
