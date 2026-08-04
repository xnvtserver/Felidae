#include "celidae/Reasoning.h"

#include <Eigen/Dense>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <map>
#include <numeric>
#include <set>
#include <sstream>
#include <iomanip>

namespace Felidae::Celidae {

namespace {

std::string trimNumber(double value) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(2) << value;
    std::string text = out.str();
    // Drop a trailing ".00" so a rule reads "3" rather than "3.00".
    if (text.size() > 3 && text.compare(text.size() - 3, 3, ".00") == 0) {
        text.erase(text.size() - 3);
    }
    return text;
}

// Column-standardised copy of a feature matrix, dropping columns that do not
// vary. Split directions are only comparable across columns when the columns
// share a scale, and a zero-variance column would divide by zero on the way.
struct Prepared {
    Eigen::MatrixXd data;
    std::vector<std::string> columns;
    std::vector<double> centre;
    std::vector<double> scale;
};

Prepared prepare(const FeatureMatrix& matrix) {
    Prepared prepared;
    const std::size_t rows = matrix.rowCount();
    const std::size_t cols = matrix.columnCount();
    if (rows == 0 || cols == 0) return prepared;

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
    if (keep.empty()) return prepared;

    prepared.data.resize(raw.rows(), static_cast<Eigen::Index>(keep.size()));
    for (std::size_t i = 0; i < keep.size(); ++i) {
        const Eigen::Index source = keep[i];
        const double mean = raw.col(source).mean();
        const double stddev = std::sqrt(
            (raw.col(source).array() - mean).square().sum() /
            static_cast<double>(std::max<Eigen::Index>(1, raw.rows() - 1)));
        prepared.data.col(static_cast<Eigen::Index>(i)) = (raw.col(source).array() - mean) / stddev;
        prepared.columns.push_back(matrix.columns[static_cast<std::size_t>(source)]);
        prepared.centre.push_back(mean);
        prepared.scale.push_back(stddev);
    }
    return prepared;
}

double gini(const std::vector<std::size_t>& counts, std::size_t total) {
    if (total == 0) return 0.0;
    double sum = 0;
    for (const std::size_t count : counts) {
        const double p = static_cast<double>(count) / static_cast<double>(total);
        sum += p * p;
    }
    return 1.0 - sum;
}

// The direction that best separates two groups of rows: Fisher's linear
// discriminant, w = Sw^-1 (m1 - m0), where Sw is the pooled within-class
// scatter. Solved rather than inverted, with a ridge term so a singular
// scatter matrix degrades to something usable instead of producing garbage.
//
// This is what makes the split oblique. The axis-aligned alternative would
// test one column at a time and need a staircase of splits wherever the true
// boundary runs diagonally.
Eigen::VectorXd fisherDirection(const Eigen::MatrixXd& data,
                                const std::vector<Eigen::Index>& rows,
                                const std::vector<int>& binary) {
    const Eigen::Index d = data.cols();
    Eigen::VectorXd meanA = Eigen::VectorXd::Zero(d);
    Eigen::VectorXd meanB = Eigen::VectorXd::Zero(d);
    std::size_t countA = 0, countB = 0;
    for (std::size_t i = 0; i < rows.size(); ++i) {
        if (binary[i] == 0) { meanA += data.row(rows[i]).transpose(); ++countA; }
        else { meanB += data.row(rows[i]).transpose(); ++countB; }
    }
    if (countA == 0 || countB == 0) return Eigen::VectorXd::Zero(d);
    meanA /= static_cast<double>(countA);
    meanB /= static_cast<double>(countB);

    Eigen::MatrixXd scatter = Eigen::MatrixXd::Zero(d, d);
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const Eigen::VectorXd centred =
            data.row(rows[i]).transpose() - (binary[i] == 0 ? meanA : meanB);
        scatter += centred * centred.transpose();
    }
    scatter /= static_cast<double>(rows.size());
    scatter.diagonal().array() += 1e-6;  // keeps the solve well posed

    const Eigen::VectorXd direction = scatter.ldlt().solve(meanB - meanA);
    if (!direction.allFinite() || direction.norm() < 1e-12) {
        return Eigen::VectorXd::Zero(d);
    }
    return direction.normalized();
}

// Splits the rows at a node into two provisional groups so that a
// discriminant direction can be computed between them. The classes at the
// node are ordered by how far their centroid sits along the node's leading
// principal component, then cut where that ordering divides most evenly -
// which keeps the resulting rule a statement about the data rather than about
// arbitrary class numbering.
std::vector<int> provisionalGroups(const Eigen::MatrixXd& data,
                                   const std::vector<Eigen::Index>& rows,
                                   const std::vector<int>& labels,
                                   const std::vector<int>& present) {
    std::map<int, Eigen::VectorXd> centroids;
    std::map<int, std::size_t> counts;
    for (const Eigen::Index row : rows) {
        const int label = labels[static_cast<std::size_t>(row)];
        if (!centroids.count(label)) centroids[label] = Eigen::VectorXd::Zero(data.cols());
        centroids[label] += data.row(row).transpose();
        ++counts[label];
    }
    for (auto& entry : centroids) {
        entry.second /= static_cast<double>(counts[entry.first]);
    }

    // Leading principal component of the class centroids.
    Eigen::MatrixXd stacked(static_cast<Eigen::Index>(centroids.size()), data.cols());
    Eigen::Index at = 0;
    std::vector<int> order;
    for (const auto& entry : centroids) {
        stacked.row(at++) = entry.second.transpose();
        order.push_back(entry.first);
    }
    const Eigen::VectorXd mean = stacked.colwise().mean();
    Eigen::MatrixXd centred = stacked.rowwise() - mean.transpose();
    Eigen::JacobiSVD<Eigen::MatrixXd> svd(centred, Eigen::ComputeThinV);
    Eigen::VectorXd axis = svd.matrixV().cols() > 0
        ? Eigen::VectorXd(svd.matrixV().col(0))
        : Eigen::VectorXd::Ones(data.cols()).normalized();

    std::vector<std::pair<double, int>> projected;
    for (std::size_t i = 0; i < order.size(); ++i) {
        projected.emplace_back(centred.row(static_cast<Eigen::Index>(i)).dot(axis), order[i]);
    }
    std::sort(projected.begin(), projected.end());

    // Cut the ordered classes where the two sides are closest in size.
    std::size_t bestCut = 1;
    double bestBalance = std::numeric_limits<double>::max();
    std::size_t running = 0;
    const std::size_t total = rows.size();
    for (std::size_t cut = 1; cut < projected.size(); ++cut) {
        running += counts[projected[cut - 1].second];
        const double balance = std::fabs(static_cast<double>(running) -
                                         static_cast<double>(total - running));
        if (balance < bestBalance) { bestBalance = balance; bestCut = cut; }
    }
    std::set<int> leftClasses;
    for (std::size_t i = 0; i < bestCut; ++i) leftClasses.insert(projected[i].second);

    std::vector<int> binary(rows.size(), 0);
    for (std::size_t i = 0; i < rows.size(); ++i) {
        binary[i] = leftClasses.count(labels[static_cast<std::size_t>(rows[i])]) ? 0 : 1;
    }
    (void)present;
    return binary;
}

} // namespace

ObliqueTree obliqueTree(const FeatureMatrix& matrix,
                        const std::vector<int>& labels,
                        const std::vector<std::string>& classNames) {
    ObliqueTree tree;
    tree.classNames = classNames;
    if (labels.size() != matrix.rowCount() || matrix.rowCount() < kMinRecordsToSplit) {
        tree.reason = "needs at least " + std::to_string(kMinRecordsToSplit) +
            " labelled records";
        return tree;
    }
    const Prepared prepared = prepare(matrix);
    if (prepared.columns.size() < 2) {
        tree.reason = "needs at least two fields that vary across records";
        return tree;
    }
    tree.columns = prepared.columns;
    const Eigen::MatrixXd& data = prepared.data;
    const std::size_t classCount = classNames.size();

    // Grown breadth-first with an explicit stack so the recursion depth is
    // bounded by construction rather than by hoping the data behaves.
    struct Pending {
        std::size_t node;
        std::vector<Eigen::Index> rows;
        int depth;
    };
    std::vector<Pending> pending;

    auto makeNode = [&](const std::vector<Eigen::Index>& rows, int depth) {
        ObliqueTreeNode node;
        node.records = rows.size();
        node.depth = depth;
        node.classCounts.assign(classCount, 0);
        for (const Eigen::Index row : rows) {
            const int label = labels[static_cast<std::size_t>(row)];
            if (label >= 0 && static_cast<std::size_t>(label) < classCount) {
                ++node.classCounts[static_cast<std::size_t>(label)];
            }
        }
        node.majorityClass = 0;
        std::size_t best = 0;
        for (std::size_t c = 0; c < node.classCounts.size(); ++c) {
            if (node.classCounts[c] > best) { best = node.classCounts[c]; node.majorityClass = static_cast<int>(c); }
        }
        node.purity = rows.empty() ? 0.0
            : static_cast<double>(best) / static_cast<double>(rows.size());
        tree.nodes.push_back(std::move(node));
        return tree.nodes.size() - 1;
    };

    std::vector<Eigen::Index> allRows(static_cast<std::size_t>(data.rows()));
    std::iota(allRows.begin(), allRows.end(), 0);
    pending.push_back(Pending{makeNode(allRows, 0), allRows, 0});

    std::vector<int> present;
    while (!pending.empty()) {
        const Pending current = pending.back();
        pending.pop_back();
        ObliqueTreeNode& node = tree.nodes[current.node];

        if (current.depth >= kMaxTreeDepth) continue;
        if (current.rows.size() < kMinRecordsToSplit) continue;
        if (node.purity > 0.995) continue;  // already one class

        const std::vector<int> binary =
            provisionalGroups(data, current.rows, labels, present);
        const Eigen::VectorXd direction = fisherDirection(data, current.rows, binary);
        if (direction.norm() < 1e-12) continue;

        // Threshold chosen by scanning the projected values and taking the cut
        // with the largest impurity drop, which is the standard criterion -
        // only applied to a projection rather than to a raw column.
        std::vector<std::pair<double, int>> projected;
        projected.reserve(current.rows.size());
        for (const Eigen::Index row : current.rows) {
            projected.emplace_back(data.row(row).dot(direction),
                                   labels[static_cast<std::size_t>(row)]);
        }
        std::sort(projected.begin(), projected.end(),
                  [](const auto& a, const auto& b) { return a.first < b.first; });

        const double parentImpurity = gini(node.classCounts, node.records);
        std::vector<std::size_t> leftCounts(classCount, 0);
        std::vector<std::size_t> rightCounts = node.classCounts;
        double bestGain = 0;
        double bestThreshold = 0;
        std::size_t bestLeft = 0;
        for (std::size_t i = 0; i + 1 < projected.size(); ++i) {
            const int label = projected[i].second;
            if (label >= 0 && static_cast<std::size_t>(label) < classCount) {
                ++leftCounts[static_cast<std::size_t>(label)];
                --rightCounts[static_cast<std::size_t>(label)];
            }
            const std::size_t leftSize = i + 1;
            const std::size_t rightSize = projected.size() - leftSize;
            if (leftSize < kMinLeafRecords || rightSize < kMinLeafRecords) continue;
            // Ties in the projection cannot be separated by a threshold.
            if (projected[i].first == projected[i + 1].first) continue;
            const double weighted =
                (static_cast<double>(leftSize) * gini(leftCounts, leftSize) +
                 static_cast<double>(rightSize) * gini(rightCounts, rightSize)) /
                static_cast<double>(projected.size());
            const double gain = parentImpurity - weighted;
            if (gain > bestGain) {
                bestGain = gain;
                bestThreshold = (projected[i].first + projected[i + 1].first) / 2.0;
                bestLeft = leftSize;
            }
        }
        if (bestGain <= 1e-9) continue;  // no split separates anything

        std::vector<Eigen::Index> leftRows, rightRows;
        for (const Eigen::Index row : current.rows) {
            if (data.row(row).dot(direction) <= bestThreshold) leftRows.push_back(row);
            else rightRows.push_back(row);
        }
        if (leftRows.size() < kMinLeafRecords || rightRows.size() < kMinLeafRecords) continue;

        ObliqueSplit split;
        split.weights.assign(direction.data(), direction.data() + direction.size());
        split.threshold = bestThreshold;
        split.gain = bestGain;
        split.leftShare = static_cast<double>(bestLeft) /
            static_cast<double>(current.rows.size());

        // Only the terms that carry the direction go into the description; a
        // rule listing eleven near-zero coefficients is not a readable rule.
        std::vector<std::pair<double, std::string>> terms;
        for (Eigen::Index c = 0; c < direction.size(); ++c) {
            terms.emplace_back(direction(c), prepared.columns[static_cast<std::size_t>(c)]);
        }
        std::sort(terms.begin(), terms.end(), [](const auto& a, const auto& b) {
            return std::fabs(a.first) > std::fabs(b.first);
        });
        std::ostringstream description;
        std::size_t shown = 0;
        for (const auto& term : terms) {
            if (std::fabs(term.first) < 0.15 || shown >= 3) break;
            if (shown) description << (term.first >= 0 ? " + " : " - ");
            else if (term.first < 0) description << "-";
            description << trimNumber(std::fabs(term.first)) << "x" << term.second;
            ++shown;
        }
        if (shown == 0) continue;
        description << " <= " << trimNumber(bestThreshold);
        split.description = description.str();

        // The node stops being a leaf only once a usable split exists.
        const std::size_t leftIndex = makeNode(leftRows, current.depth + 1);
        const std::size_t rightIndex = makeNode(rightRows, current.depth + 1);
        // makeNode may have reallocated the node vector.
        tree.nodes[current.node].leaf = false;
        tree.nodes[current.node].split = std::move(split);
        tree.nodes[current.node].left = leftIndex;
        tree.nodes[current.node].right = rightIndex;

        pending.push_back(Pending{leftIndex, leftRows, current.depth + 1});
        pending.push_back(Pending{rightIndex, rightRows, current.depth + 1});
    }

    if (tree.nodes.size() < 3) {
        tree.reason = "no combination of fields separates the groups";
        return tree;
    }

    // Readable rules, one per leaf, by walking down from the root.
    std::vector<std::pair<std::size_t, std::string>> walk;
    walk.emplace_back(0, "");
    std::size_t correct = 0;
    while (!walk.empty()) {
        const auto current = walk.back();
        walk.pop_back();
        const ObliqueTreeNode& node = tree.nodes[current.first];
        if (node.leaf) {
            correct += node.classCounts.empty()
                ? 0
                : node.classCounts[static_cast<std::size_t>(node.majorityClass)];
            std::ostringstream rule;
            rule << (current.second.empty() ? "(all records)" : current.second) << "  ->  "
                 << (static_cast<std::size_t>(node.majorityClass) < classNames.size()
                        ? classNames[static_cast<std::size_t>(node.majorityClass)]
                        : "group " + std::to_string(node.majorityClass))
                 << " (" << node.classCounts[static_cast<std::size_t>(node.majorityClass)]
                 << " of " << node.records << " records)";
            tree.rules.push_back(rule.str());
            continue;
        }
        const std::string base = current.second.empty() ? "" : current.second + " and ";
        walk.emplace_back(node.left, base + node.split.description);
        // The right branch is the negation, written as such rather than
        // repeating the whole expression with the comparison flipped.
        std::string negated = node.split.description;
        const std::size_t at = negated.rfind(" <= ");
        if (at != std::string::npos) negated.replace(at, 4, " > ");
        walk.emplace_back(node.right, base + negated);
    }
    std::sort(tree.rules.begin(), tree.rules.end());
    tree.accuracy = static_cast<double>(correct) / static_cast<double>(matrix.rowCount());
    tree.valid = true;
    return tree;
}

std::vector<std::size_t> seriate(const std::vector<std::vector<double>>& similarity) {
    const std::size_t n = similarity.size();
    std::vector<std::size_t> order(n);
    std::iota(order.begin(), order.end(), 0);
    if (n < 3) return order;
    for (const auto& row : similarity) {
        if (row.size() != n) return order;  // not square; refuse rather than guess
    }

    // Graph Laplacian of the similarity matrix, using |similarity| as the edge
    // weight: two fields that move strongly opposite each other are as related
    // as two that move together, and both belong in the same block.
    Eigen::MatrixXd laplacian = Eigen::MatrixXd::Zero(
        static_cast<Eigen::Index>(n), static_cast<Eigen::Index>(n));
    for (std::size_t i = 0; i < n; ++i) {
        double degree = 0;
        for (std::size_t j = 0; j < n; ++j) {
            if (i == j) continue;
            const double weight = std::fabs(similarity[i][j]);
            if (!std::isfinite(weight)) continue;
            laplacian(static_cast<Eigen::Index>(i), static_cast<Eigen::Index>(j)) = -weight;
            degree += weight;
        }
        laplacian(static_cast<Eigen::Index>(i), static_cast<Eigen::Index>(i)) = degree;
    }

    Eigen::SelfAdjointEigenSolver<Eigen::MatrixXd> solver(laplacian);
    if (solver.info() != Eigen::Success) return order;
    // Eigenvalues ascend; the first is ~0 with a constant eigenvector, so the
    // Fiedler vector is the second.
    if (solver.eigenvectors().cols() < 2) return order;
    const Eigen::VectorXd fiedler = solver.eigenvectors().col(1);
    if (!fiedler.allFinite()) return order;

    std::sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
        const double left = fiedler(static_cast<Eigen::Index>(a));
        const double right = fiedler(static_cast<Eigen::Index>(b));
        if (left != right) return left < right;
        return a < b;  // stable, so the output stays diffable
    });
    return order;
}

std::vector<SpectralPoint> spectralLayout(
    std::size_t nodeCount,
    const std::vector<std::pair<std::size_t, std::size_t>>& edges) {
    std::vector<SpectralPoint> points(nodeCount);
    if (nodeCount < 3) {
        for (std::size_t i = 0; i < nodeCount; ++i) {
            points[i].x = nodeCount < 2 ? 0.5 : static_cast<double>(i);
            points[i].y = 0.5;
        }
        return points;
    }

    Eigen::MatrixXd laplacian = Eigen::MatrixXd::Zero(
        static_cast<Eigen::Index>(nodeCount), static_cast<Eigen::Index>(nodeCount));
    std::vector<double> degree(nodeCount, 0.0);
    for (const auto& edge : edges) {
        if (edge.first >= nodeCount || edge.second >= nodeCount) continue;
        if (edge.first == edge.second) continue;
        const Eigen::Index a = static_cast<Eigen::Index>(edge.first);
        const Eigen::Index b = static_cast<Eigen::Index>(edge.second);
        // Undirected for layout purposes: which way a call points does not
        // change how close the two ought to be drawn.
        laplacian(a, b) -= 1.0;
        laplacian(b, a) -= 1.0;
        degree[edge.first] += 1.0;
        degree[edge.second] += 1.0;
    }
    for (std::size_t i = 0; i < nodeCount; ++i) {
        // An isolated node has no eigenvector preference; a small self-weight
        // keeps the Laplacian from being singular in a way that strands it at
        // the origin on top of every other isolated node.
        laplacian(static_cast<Eigen::Index>(i), static_cast<Eigen::Index>(i)) =
            degree[i] > 0 ? degree[i] : 1e-3;
    }

    Eigen::SelfAdjointEigenSolver<Eigen::MatrixXd> solver(laplacian);
    if (solver.info() != Eigen::Success || solver.eigenvectors().cols() < 3) {
        for (std::size_t i = 0; i < nodeCount; ++i) {
            points[i].x = static_cast<double>(i) / static_cast<double>(nodeCount);
            points[i].y = 0.5;
        }
        return points;
    }
    // Skip the trivial constant eigenvector at index 0.
    Eigen::VectorXd first = solver.eigenvectors().col(1);
    Eigen::VectorXd second = solver.eigenvectors().col(2);

    auto normalise = [&](Eigen::VectorXd& vector) {
        const double low = vector.minCoeff();
        const double high = vector.maxCoeff();
        const double span = high - low;
        if (span > 1e-12) vector = (vector.array() - low) / span;
        else vector.setConstant(0.5);
    };
    normalise(first);
    normalise(second);

    for (std::size_t i = 0; i < nodeCount; ++i) {
        points[i].x = first(static_cast<Eigen::Index>(i));
        points[i].y = second(static_cast<Eigen::Index>(i));
    }
    return points;
}

CorrespondenceMap correspondenceMap(const FactProfile& profile,
                                    const FieldStats& rows,
                                    const FieldStats& columns) {
    CorrespondenceMap map;
    map.rowField = rows.name;
    map.columnField = columns.name;
    if (rows.name == columns.name) {
        map.reason = "a field cannot be cross-tabulated against itself";
        return map;
    }
    for (const FieldStats* field : {&rows, &columns}) {
        if (field->type != FieldType::Categorical) {
            map.reason = field->name + " is not a label field";
            return map;
        }
        if (field->distinct < kMinCorrespondenceLevels) {
            map.reason = field->name + " has only one value, so it separates nothing";
            return map;
        }
        if (field->distinct > kMaxCorrespondenceLevels) {
            map.reason = field->name + " has " + std::to_string(field->distinct) +
                " values; beyond " + std::to_string(kMaxCorrespondenceLevels) +
                " the map is unreadable";
            return map;
        }
    }

    // Contingency table, built only from records carrying both fields: a
    // record missing either cannot speak to how they relate.
    std::map<std::string, std::size_t> rowIndex;
    std::map<std::string, std::size_t> columnIndex;
    std::vector<std::string> rowNames;
    std::vector<std::string> columnNames;
    std::vector<std::array<std::size_t, 2>> pairs;
    for (const auto& record : profile.samples) {
        const auto a = record.find(rows.name);
        const auto b = record.find(columns.name);
        if (a == record.end() || b == record.end()) continue;
        if (a->second.empty() || b->second.empty()) continue;
        if (rowIndex.find(a->second) == rowIndex.end()) {
            rowIndex[a->second] = rowNames.size();
            rowNames.push_back(a->second);
        }
        if (columnIndex.find(b->second) == columnIndex.end()) {
            columnIndex[b->second] = columnNames.size();
            columnNames.push_back(b->second);
        }
        pairs.push_back({rowIndex[a->second], columnIndex[b->second]});
    }
    if (rowNames.size() < kMinCorrespondenceLevels ||
        columnNames.size() < kMinCorrespondenceLevels) {
        map.reason = "too few records carry both fields to cross-tabulate them";
        return map;
    }

    const Eigen::Index r = static_cast<Eigen::Index>(rowNames.size());
    const Eigen::Index c = static_cast<Eigen::Index>(columnNames.size());
    Eigen::MatrixXd table = Eigen::MatrixXd::Zero(r, c);
    for (const auto& pair : pairs) {
        table(static_cast<Eigen::Index>(pair[0]), static_cast<Eigen::Index>(pair[1])) += 1.0;
    }
    const double total = table.sum();
    if (!(total > 0)) {
        map.reason = "the cross-tabulation is empty";
        return map;
    }

    // Correspondence analysis proper. P is the table as proportions; the row
    // and column masses are its margins; and the matrix that gets factorised
    // is the standardised residual from independence,
    //
    //     S = Dr^-1/2 (P - r c') Dc^-1/2
    //
    // whose squared singular values are the principal inertias. This is the
    // step that makes the geometry meaningful: a large entry in S means that
    // cell happens far more (or far less) often than independent fields would
    // produce, and it is scaled by how common the categories are, so a rare
    // category is not automatically an outlier.
    const Eigen::MatrixXd proportions = table / total;
    const Eigen::VectorXd rowMass = proportions.rowwise().sum();
    const Eigen::VectorXd columnMass = proportions.colwise().sum().transpose();

    Eigen::MatrixXd residual(r, c);
    for (Eigen::Index i = 0; i < r; ++i) {
        for (Eigen::Index j = 0; j < c; ++j) {
            const double expected = rowMass(i) * columnMass(j);
            // A zero margin cannot happen here - every retained level was
            // observed at least once - but dividing by a square root demands
            // the guard anyway.
            residual(i, j) = expected > 0
                ? (proportions(i, j) - expected) / std::sqrt(expected)
                : 0.0;
        }
    }

    Eigen::JacobiSVD<Eigen::MatrixXd> svd(residual,
                                          Eigen::ComputeThinU | Eigen::ComputeThinV);
    const Eigen::VectorXd singular = svd.singularValues();
    if (singular.size() < 2) {
        map.reason = "the table has only one dimension of association to show";
        return map;
    }
    const double inertia = singular.squaredNorm();
    if (!(inertia > 1e-12)) {
        map.reason = "the two fields are independent: no cell departs from what chance "
                     "alone would produce";
        return map;
    }
    map.explained = (singular(0) * singular(0) + singular(1) * singular(1)) / inertia;

    // Cramer's V over the same table, so the map carries the strength of the
    // association it draws rather than leaving a reader to judge it by eye.
    const double chiSquare = inertia * total;
    const double smallerSide = static_cast<double>(std::min(r, c) - 1);
    map.association = smallerSide > 0
        ? std::sqrt(chiSquare / (total * smallerSide))
        : 0.0;

    // Principal coordinates: standard coordinates scaled by the singular
    // values, which puts rows and columns in one space where proximity is
    // readable directly. Symmetric scaling on both sides, so neither field is
    // privileged.
    auto place = [&](const Eigen::MatrixXd& vectors, const Eigen::VectorXd& mass,
                     const std::vector<std::string>& names, bool isRow) {
        for (Eigen::Index i = 0; i < static_cast<Eigen::Index>(names.size()); ++i) {
            const double weight = mass(i) > 0 ? std::sqrt(mass(i)) : 1.0;
            CorrespondencePoint point;
            point.value = names[static_cast<std::size_t>(i)];
            point.isRow = isRow;
            point.x = vectors(i, 0) * singular(0) / weight;
            point.y = vectors(i, 1) * singular(1) / weight;
            point.count = static_cast<std::size_t>(std::llround(mass(i) * total));
            // Cos^2 quality: the share of this point's distance from the
            // origin that the two drawn axes account for. Without it a point
            // whose real structure lies on a third axis is drawn near the
            // centre and reads as "typical" when it is simply not shown.
            const double full = vectors.row(i).squaredNorm();
            const double shown = vectors(i, 0) * vectors(i, 0) + vectors(i, 1) * vectors(i, 1);
            point.quality = full > 1e-12 ? shown / full : 0.0;
            map.points.push_back(std::move(point));
        }
    };
    place(svd.matrixU(), rowMass, rowNames, true);
    place(svd.matrixV(), columnMass, columnNames, false);

    map.valid = true;
    return map;
}

namespace {

// Words plus character trigrams. Words carry meaning where values are phrases
// ("north yard"); trigrams carry it where they are compounds or codes
// ("north-yard", "NY-014"), which fact data is full of. Together they cover
// both without needing a dictionary or a language guess.
//
// Nothing about any particular corpus is written down here, and that is the
// point. There is no stopword list: a term carried by most values is
// suppressed by its own IDF weight, which is computed from the facts in front
// of it, so the mechanism that removes "the" from an English corpus removes
// "SA" from a corpus of South African site codes without being told either
// word exists. There is no language setting for the same reason - the split
// is on "alphanumeric or not", so the vocabulary of a Felidae program is
// whatever its own literals turn out to contain.
std::vector<std::string> terms(const std::string& value) {
    std::vector<std::string> out;
    std::string lowered;
    lowered.reserve(value.size());
    for (const char ch : value) {
        lowered += static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    std::string word;
    for (const char ch : lowered) {
        if (std::isalnum(static_cast<unsigned char>(ch))) {
            word += ch;
        } else if (!word.empty()) {
            out.push_back("w:" + word);
            word.clear();
        }
    }
    if (!word.empty()) out.push_back("w:" + word);
    for (std::size_t i = 0; i + 3 <= lowered.size(); ++i) {
        out.push_back("g:" + lowered.substr(i, 3));
    }
    return out;
}

} // namespace

std::vector<VocabularyTerm> textVocabulary(const FactProfile& profile,
                                           const FieldStats& field,
                                           std::size_t limit) {
    std::vector<VocabularyTerm> out;
    if (field.type != FieldType::Categorical && field.type != FieldType::Identifier) return out;

    // Counted per distinct value as well as per record, because the two say
    // different things: "islands" appearing in 20 distinct country names is
    // structure in the vocabulary, while a term appearing in 20 records that
    // all share one value is just that value being frequent.
    std::map<std::string, std::size_t> recordCount;
    std::map<std::string, std::set<std::string>> valuesWith;
    for (const auto& record : profile.samples) {
        const auto found = record.find(field.name);
        if (found == record.end() || found->second.empty()) continue;
        std::set<std::string> unique;
        for (const auto& token : terms(found->second)) {
            // Words only. Trigrams earn their place inside the factorisation,
            // where overlapping fragments genuinely relate compounds, but
            // "isl"/"sla"/"lan" listed to a reader is noise.
            if (token.compare(0, 2, "w:") != 0) continue;
            unique.insert(token.substr(2));
        }
        for (const auto& token : unique) {
            ++recordCount[token];
            valuesWith[token].insert(found->second);
        }
    }

    const double corpus = static_cast<double>(
        field.distinct > 0 ? field.distinct : valuesWith.size());
    for (const auto& entry : valuesWith) {
        if (entry.second.size() < kMinTermValues) continue;
        // A term present in every value distinguishes nothing.
        if (field.distinct > 0 && entry.second.size() >= field.distinct) continue;
        VocabularyTerm term;
        term.term = entry.first;
        term.values = entry.second.size();
        term.records = recordCount[entry.first];
        // Ranked by TF-IDF mass rather than by raw frequency, which is the
        // same weighting the factorisation uses - so the terms listed to a
        // reader are the terms that actually shaped the axes, not a different
        // ordering of the same words. It demotes from both ends: a term in
        // almost every value carries no information about which value, and a
        // term in two values relates only those two.
        term.weight = static_cast<double>(term.values) *
            std::log(corpus / static_cast<double>(term.values));
        out.push_back(std::move(term));
    }
    std::sort(out.begin(), out.end(), [](const VocabularyTerm& a, const VocabularyTerm& b) {
        if (a.weight != b.weight) return a.weight > b.weight;
        if (a.values != b.values) return a.values > b.values;
        return a.term < b.term;
    });
    if (out.size() > limit) out.resize(limit);
    return out;
}

SemanticMap semanticMap(const FactProfile& profile, const FieldStats& field) {
    SemanticMap map;
    map.field = field.name;
    if (field.type != FieldType::Categorical && field.type != FieldType::Identifier) {
        map.reason = "only text fields have vocabulary to compare";
        return map;
    }

    std::map<std::string, std::size_t> counts;
    for (const auto& record : profile.samples) {
        const auto found = record.find(field.name);
        if (found == record.end() || found->second.empty()) continue;
        ++counts[found->second];
    }
    if (counts.size() < kMinSemanticValues) {
        map.reason = "needs at least " + std::to_string(kMinSemanticValues) +
            " distinct values; this field has " + std::to_string(counts.size());
        return map;
    }
    map.corpusValues = counts.size();
    if (counts.size() > kMaxSemanticValues) {
        // Keep the most frequent; the tail would be unreadable on a scatter
        // regardless of how well it was placed.
        std::vector<std::pair<std::string, std::size_t>> ordered(counts.begin(), counts.end());
        std::sort(ordered.begin(), ordered.end(), [](const auto& a, const auto& b) {
            if (a.second != b.second) return a.second > b.second;
            return a.first < b.first;
        });

        // Frequency ranking decides nothing when the frequencies are equal,
        // and in fact data they very often are: a field converted from a CSV
        // column of names has every value appearing exactly once. Truncating
        // that ranking keeps the first 120 *alphabetically*, which is not a
        // sample of the corpus - it is the letters A to K. Half the countries
        // in converted_csv_country.fx vanished this way, and nothing in the
        // output said so; the families it found were real, but they were
        // families of the first half of the alphabet.
        //
        // Where the cut falls inside a band of tied values, that band is
        // sampled at an even stride instead, so the retained values are
        // spread across the whole corpus.
        const std::size_t cutCount = ordered[kMaxSemanticValues - 1].second;
        std::vector<std::pair<std::string, std::size_t>> kept;
        std::vector<std::pair<std::string, std::size_t>> tied;
        for (const auto& entry : ordered) {
            if (entry.second > cutCount) kept.push_back(entry);
            else if (entry.second == cutCount) tied.push_back(entry);
        }
        const std::size_t room = kMaxSemanticValues - kept.size();
        if (room > 0 && !tied.empty()) {
            for (std::size_t i = 0; i < room; ++i) {
                kept.push_back(tied[i * tied.size() / room]);
            }
        }
        counts.clear();
        for (const auto& entry : kept) counts.insert(entry);
    }

    std::vector<std::string> values;
    std::vector<std::vector<std::string>> tokenised;
    std::map<std::string, std::size_t> documentFrequency;
    for (const auto& entry : counts) {
        values.push_back(entry.first);
        std::vector<std::string> tokens = terms(entry.first);
        std::set<std::string> unique(tokens.begin(), tokens.end());
        for (const auto& token : unique) ++documentFrequency[token];
        tokenised.push_back(std::move(tokens));
    }

    // A term appearing in every value distinguishes nothing, and one appearing
    // in a single value cannot relate it to anything else.
    std::vector<std::string> vocabulary;
    for (const auto& entry : documentFrequency) {
        if (entry.second < 2 || entry.second >= values.size()) continue;
        vocabulary.push_back(entry.first);
    }
    if (vocabulary.size() < 2) {
        map.reason = "the values share too little vocabulary to relate them";
        return map;
    }
    std::map<std::string, Eigen::Index> columnOf;
    for (std::size_t i = 0; i < vocabulary.size(); ++i) {
        columnOf[vocabulary[i]] = static_cast<Eigen::Index>(i);
    }

    // Values carrying none of the shared vocabulary are removed before the
    // factorisation rather than plotted.
    //
    // Such a value is the zero vector: it has no position, and the SVD
    // faithfully places it at the origin. On a list of country names that is
    // roughly two hundred of them - "Israel", "Japan", "Kenya" share no word
    // with anything - and the result was a dense blob at (0,0) that k-means
    // then coloured as a family, reading as "these two hundred countries
    // belong together" when the truth is the exact opposite: they are the
    // ones with nothing in common. Worse, the blob dominated the clustering,
    // so the real families had to be found in what was left.
    //
    // They are counted and reported instead, which is the honest form of the
    // same information.
    {
        std::vector<std::string> related;
        std::vector<std::vector<std::string>> relatedTokens;
        for (std::size_t i = 0; i < values.size(); ++i) {
            const bool carries = std::any_of(
                tokenised[i].begin(), tokenised[i].end(),
                [&](const std::string& token) { return columnOf.count(token) > 0; });
            if (!carries) continue;
            related.push_back(values[i]);
            relatedTokens.push_back(tokenised[i]);
        }
        map.unrelated = values.size() - related.size();
        values = std::move(related);
        tokenised = std::move(relatedTokens);
    }
    if (values.size() < kMinSemanticValues) {
        map.reason = "only " + std::to_string(values.size()) +
            " values share any vocabulary with another; the rest are unrelated strings";
        return map;
    }

    // TF-IDF. The IDF weighting is what stops a term shared by most values
    // from dominating the geometry.
    Eigen::MatrixXd tfidf = Eigen::MatrixXd::Zero(
        static_cast<Eigen::Index>(values.size()), static_cast<Eigen::Index>(vocabulary.size()));
    for (std::size_t r = 0; r < tokenised.size(); ++r) {
        std::map<std::string, std::size_t> termFrequency;
        for (const auto& token : tokenised[r]) ++termFrequency[token];
        for (const auto& entry : termFrequency) {
            const auto column = columnOf.find(entry.first);
            if (column == columnOf.end()) continue;
            const double tf = 1.0 + std::log(static_cast<double>(entry.second));
            const double idf = std::log(static_cast<double>(values.size()) /
                                        static_cast<double>(documentFrequency[entry.first]));
            tfidf(static_cast<Eigen::Index>(r), column->second) = tf * idf;
        }
        const double norm = tfidf.row(static_cast<Eigen::Index>(r)).norm();
        if (norm > 1e-12) tfidf.row(static_cast<Eigen::Index>(r)) /= norm;
    }

    // Latent semantic analysis: the truncated SVD of the TF-IDF matrix. The
    // leading right singular vectors are the latent dimensions; projecting
    // onto the first two places each value on a plane by what it shares.
    Eigen::JacobiSVD<Eigen::MatrixXd> svd(tfidf, Eigen::ComputeThinU | Eigen::ComputeThinV);
    if (svd.singularValues().size() < 2) {
        map.reason = "the value vocabulary has no second dimension to plot";
        return map;
    }
    const Eigen::VectorXd singular = svd.singularValues();
    const double energy = singular.squaredNorm();
    map.explained = energy > 0
        ? (singular(0) * singular(0) + singular(1) * singular(1)) / energy
        : 0.0;

    const Eigen::MatrixXd projected = svd.matrixU().leftCols(2) *
        singular.head(2).asDiagonal();
    for (std::size_t i = 0; i < values.size(); ++i) {
        SemanticPoint point;
        point.value = values[i];
        point.count = counts[values[i]];
        point.x = projected(static_cast<Eigen::Index>(i), 0);
        point.y = projected(static_cast<Eigen::Index>(i), 1);
        map.points.push_back(std::move(point));
    }

    // Group the projection by clustering it, not by which quadrant a point
    // fell in. Quadrant membership is an artefact of where the SVD happened
    // to put the origin: a family of values straddling an axis is split in
    // two, and two unrelated families on the same side are merged - and both
    // errors are invisible, because the colours look like findings either way.
    //
    // Reusing clusterRecords here also means the grouping is chosen by
    // silhouette rather than fixed at four, so a field with three families of
    // value is coloured with three colours.
    {
        FeatureMatrix latent;
        latent.columns = {"lsa1", "lsa2"};
        // Enough components to cluster on, but the picture is still the plane
        // above; a third latent dimension informs the grouping without
        // claiming a position no axis shows.
        const Eigen::Index depth = std::min<Eigen::Index>(3, singular.size());
        if (depth > 2) latent.columns.push_back("lsa3");
        const Eigen::MatrixXd deep = svd.matrixU().leftCols(depth) *
            singular.head(depth).asDiagonal();
        for (std::size_t i = 0; i < values.size(); ++i) {
            std::vector<double> row;
            for (Eigen::Index c = 0; c < depth; ++c) {
                row.push_back(deep(static_cast<Eigen::Index>(i), c));
            }
            latent.rows.push_back(std::move(row));
            latent.rowLabels.push_back(values[i]);
            latent.rowSamples.push_back(i);
        }
        const ClusterResult grouped = clusterRecords(latent);
        if (grouped.valid && grouped.silhouette >= kMinSemanticSilhouette) {
            for (std::size_t i = 0; i < map.points.size() && i < grouped.assignment.size(); ++i) {
                map.points[i].group = grouped.assignment[i];
            }
            map.groups = grouped.k;
        } else {
            // No group structure worth colouring. One group is the honest
            // answer, and the scatter still shows the geometry.
            map.groups = 1;
        }
    }

    // Terms that most define an axis, for labelling it honestly.
    //
    // Words are preferred over trigrams for the label even where a trigram
    // loads higher. Trigrams earn their place in the factorisation - they are
    // what relates "Virgin Islands, British" to "Virgin Islands, U.S." when
    // the punctuation differs - but an axis labelled "lan / and / is" tells a
    // reader nothing, while the same axis labelled "islands / saint" tells
    // them what the picture is about. Trigrams are used only when no word
    // loads on the axis at all, which happens on corpora of unspaced codes,
    // where a fragment genuinely is the most meaningful unit available.
    auto axisTerms = [&](Eigen::Index component) {
        std::vector<std::pair<double, std::string>> words;
        std::vector<std::pair<double, std::string>> fragments;
        for (std::size_t i = 0; i < vocabulary.size(); ++i) {
            const double loading =
                std::fabs(svd.matrixV()(static_cast<Eigen::Index>(i), component));
            auto& bucket = vocabulary[i].compare(0, 2, "w:") == 0 ? words : fragments;
            bucket.emplace_back(loading, vocabulary[i].substr(2));
        }
        auto strongestFirst = [](const auto& a, const auto& b) {
            if (a.first != b.first) return a.first > b.first;
            return a.second < b.second;
        };
        std::sort(words.begin(), words.end(), strongestFirst);
        std::sort(fragments.begin(), fragments.end(), strongestFirst);
        const auto& chosen = words.empty() ? fragments : words;
        std::vector<std::string> top;
        for (std::size_t i = 0; i < chosen.size() && i < 3; ++i) top.push_back(chosen[i].second);
        return top;
    };
    map.axisTermsX = axisTerms(0);
    map.axisTermsY = axisTerms(1);
    map.valid = true;
    return map;
}

} // namespace Felidae::Celidae
