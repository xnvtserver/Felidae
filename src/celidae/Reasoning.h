#pragma once

// Reasoning layer: the algorithms that turn measurements into explanations.
//
// Analytics.h holds the primitives - what a field is, how values are
// distributed, which columns correlate, where the principal components point.
// Everything here consumes those primitives and produces something a person
// can act on: a readable rule that separates two segments, an ordering that
// makes a matrix legible, a layout that puts related facts near each other, a
// map of which text values mean similar things.
//
// The split matters because the two halves fail differently. A bug in
// Analytics gives a wrong number. A bug here gives a number that is right and
// an explanation that is wrong, which is worse - so this half is kept small,
// separately testable, and honest about its own confidence.
//
// Every algorithm below is an eigenproblem or a factorisation, and all of them
// run on Eigen:
//
//   obliqueTree     Fisher's linear discriminant (generalised eigenproblem)
//                   for split directions that are not axis-aligned.
//   seriate         Fiedler vector of a similarity Laplacian, to order rows
//                   and columns so related ones sit adjacent.
//   spectralLayout  Laplacian eigenmaps, for a graph layout that reflects
//                   connectivity instead of declaration order.
//   semanticMap     Truncated SVD of a TF-IDF matrix - latent semantic
//                   analysis - to place text values by what they share.

#include "celidae/Analytics.h"

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace Felidae::Celidae {

// ---------------------------------------------------------------------------
// Oblique decision tree
// ---------------------------------------------------------------------------

// An axis-aligned tree can only say "total > 120". When the boundary between
// two groups runs diagonally - which it does whenever two fields trade off
// against each other - an axis-aligned tree approximates it with a staircase
// of a dozen splits, and the explanation becomes unreadable precisely where it
// matters most.
//
// An oblique split tests a weighted combination instead: "0.8 x total + 0.6 x
// items > 1.2". One such test can capture a boundary that would take a dozen
// axis-aligned ones, so the rule that comes out is short enough to read.
struct ObliqueSplit {
    // Weight per feature-matrix column; the test is dot(weights, row) <= threshold.
    std::vector<double> weights;
    double threshold = 0;
    // The same test written for a person, e.g.
    // "0.81 x total + 0.58 x items <= 1.24".
    std::string description;
    // Share of records at this node that the split sends left.
    double leftShare = 0;
    // Drop in Gini impurity. Zero means the split separates nothing.
    double gain = 0;
};

struct ObliqueTreeNode {
    bool leaf = true;
    ObliqueSplit split;
    std::size_t left = 0;   // index into ObliqueTree::nodes
    std::size_t right = 0;
    // Rows reaching this node, and how they are distributed across classes.
    std::size_t records = 0;
    std::vector<std::size_t> classCounts;
    int majorityClass = 0;
    double purity = 0;      // share of `records` belonging to majorityClass
    int depth = 0;
};

struct ObliqueTree {
    bool valid = false;
    std::string reason;
    std::vector<ObliqueTreeNode> nodes;   // nodes[0] is the root
    std::vector<std::string> classNames;
    std::vector<std::string> columns;
    // Share of rows the tree classifies correctly. Measured on the same rows
    // it was fitted to, so it is an upper bound and is labelled as such
    // wherever it is shown.
    double accuracy = 0;
    // One readable rule per leaf, e.g.
    // "0.81 x total + 0.58 x items > 1.24  ->  segment 1 (12 of 12 records)".
    std::vector<std::string> rules;
};

// Depth is capped low on purpose: the tree exists to be read, not to win a
// benchmark, and a rule with six clauses in it explains nothing.
constexpr int kMaxTreeDepth = 3;
constexpr std::size_t kMinRecordsToSplit = 6;
constexpr std::size_t kMinLeafRecords = 2;

// Fits an oblique tree that separates `labels` (typically cluster
// assignments). Returns an invalid tree, with a reason, when the data cannot
// support one.
ObliqueTree obliqueTree(const FeatureMatrix& matrix,
                        const std::vector<int>& labels,
                        const std::vector<std::string>& classNames);

// ---------------------------------------------------------------------------
// Spectral ordering (seriation)
// ---------------------------------------------------------------------------

// A correlation matrix in declaration order is a confetti of colour. The same
// matrix with related fields placed adjacent shows its block structure at a
// glance, and no data changed - only the order.
//
// The ordering is the Fiedler vector of the similarity graph's Laplacian: the
// eigenvector of the second-smallest eigenvalue, which is the classic
// continuous relaxation of "arrange these so similar ones are close".
//
// Returns a permutation of 0..n-1, or the identity when the input is too small
// or the solve does not converge.
std::vector<std::size_t> seriate(const std::vector<std::vector<double>>& similarity);

// ---------------------------------------------------------------------------
// Spectral graph layout
// ---------------------------------------------------------------------------

// Laplacian eigenmaps: place each node at its coordinate in the second and
// third eigenvectors of the graph Laplacian. Connected nodes end up near each
// other because that is precisely the quantity those eigenvectors minimise.
//
// This replaces sorting nodes into columns by kind, which conveys nothing
// about how they are actually connected. Coordinates are normalised to 0..1.
struct SpectralPoint {
    double x = 0;
    double y = 0;
};

std::vector<SpectralPoint> spectralLayout(
    std::size_t nodeCount,
    const std::vector<std::pair<std::size_t, std::size_t>>& edges);

// ---------------------------------------------------------------------------
// Correspondence analysis
// ---------------------------------------------------------------------------

// PCA for two categorical fields.
//
// The gap this fills: PCA and k-means need continuous measures, and a great
// deal of fact data has none. A file converted from a CSV of regions, tiers,
// statuses and codes reaches the segmentation view, gets one-hot encoded, and
// is either declined or - worse - clustered on indicator columns, where
// "distance" between two categories is an artefact of the encoding rather
// than anything about the data.
//
// Correspondence analysis is the technique built for exactly this. It
// operates on the contingency table of two categorical fields, and the
// coordinates come from the SVD of that table after chi-square
// standardisation, so "close together" means "these categories occur with
// each other more than independence would predict" - a statement about the
// data that needs no notion of numeric distance at all.
//
// Both fields land in one shared space, which is the property that makes it
// readable: a row category sitting near a column category is the finding, and
// no legend or cross-reference is needed to see it. Cramer's V already tells
// a reader *whether* two labels are associated; this shows *which values*
// drive the association, which is the question they ask next.
struct CorrespondencePoint {
    std::string value;
    // Which field this point came from, so the two can be drawn distinctly.
    bool isRow = true;
    double x = 0;
    double y = 0;
    std::size_t count = 0;
    // Share of this point's own variation the two drawn axes capture. A point
    // with a low value is badly represented by the picture, and saying so is
    // the difference between a map and a misleading one.
    double quality = 0;
};

struct CorrespondenceMap {
    bool valid = false;
    std::string reason;
    std::string rowField;
    std::string columnField;
    std::vector<CorrespondencePoint> points;
    // Share of the table's total inertia (its chi-square statistic over the
    // grand total) the two axes carry.
    double explained = 0;
    // Cramer's V for the same table, so the map is labelled with the strength
    // of the association it is drawing.
    double association = 0;
};

// A table needs at least two levels on each side to have any structure, and
// few enough on both that the result is readable.
constexpr std::size_t kMinCorrespondenceLevels = 2;
constexpr std::size_t kMaxCorrespondenceLevels = 20;

// Cells a contingency table must have before a correspondence map is drawn
// instead of the table itself. Below it the grid is small enough to read
// directly and a map of a handful of points adds nothing; above it the reader
// is scanning rows and columns to find the pairing that matters, which is the
// work this analysis does for them.
constexpr std::size_t kMinCellsForCorrespondence = 12;

CorrespondenceMap correspondenceMap(const FactProfile& profile,
                                    const FieldStats& rows,
                                    const FieldStats& columns);

// ---------------------------------------------------------------------------
// Latent semantic analysis over text values
// ---------------------------------------------------------------------------

// Celidae has no language model and should not pretend to. What it does have
// is a corpus: every string literal a program declares. Latent semantic
// analysis over that corpus is a real, well-understood technique that needs
// nothing but a factorisation - build a TF-IDF matrix of values against the
// character n-grams and words they contain, take its truncated SVD, and read
// the leading components as coordinates.
//
// Values that share vocabulary land near each other, which surfaces the
// structure inside a text field that a bar chart of counts cannot: that
// "north-yard" and "north-depot" belong together, or that a status field has
// three families of value rather than eleven unrelated ones.
struct SemanticPoint {
    std::string value;
    std::size_t count = 0;
    double x = 0;
    double y = 0;
    int group = 0;
};

struct SemanticMap {
    bool valid = false;
    std::string reason;
    std::string field;
    std::vector<SemanticPoint> points;
    // Terms that most define each axis, for labelling them honestly.
    std::vector<std::string> axisTermsX;
    std::vector<std::string> axisTermsY;
    // Share of the corpus's variance the two axes carry.
    double explained = 0;
    int groups = 0;
    // Values sharing no vocabulary with any other, so having no position to
    // plot. Reported rather than drawn - see the note in the implementation.
    std::size_t unrelated = 0;
    // Distinct values in the field before any sampling, so a view can say
    // what share of the corpus its picture covers instead of implying all of
    // it is on screen.
    std::size_t corpusValues = 0;
};

// Needs enough distinct values for a factorisation to mean anything, and few
// enough that the result is still a readable scatter.
constexpr std::size_t kMinSemanticValues = 6;
constexpr std::size_t kMaxSemanticValues = 120;

// Below this mean silhouette the latent space has no group structure, and the
// points are left ungrouped rather than coloured into families that are not
// there. Lower than the numeric threshold on purpose: a TF-IDF space is
// sparse and high-dimensional, so real families separate less cleanly there
// than points on a handful of continuous measures do.
constexpr double kMinSemanticSilhouette = 0.18;

SemanticMap semanticMap(const FactProfile& profile, const FieldStats& field);

// The words a text field's values are built from, most frequent first, with
// the number of distinct values each appears in.
//
// This is the readable companion to semanticMap: the map shows *that* values
// group, this says what the groups are made of. On a list of country names it
// reports that 20 of them contain "islands" and 8 contain "republic", which
// is a fact about the corpus that no chart of a near-unique field could
// otherwise show - and near-unique text fields are extremely common in fact
// data converted from CSV.
//
// Shares semanticMap's tokeniser rather than splitting on spaces again, so
// the terms named here are exactly the terms the latent axes were built from.
struct VocabularyTerm {
    std::string term;
    std::size_t values = 0;  // distinct field values containing it
    std::size_t records = 0; // records whose value contains it
    // TF-IDF mass: how much this term contributes to telling the corpus's
    // values apart. Terms are ranked by it rather than by `values`, so the
    // list matches the weighting the factorisation actually used.
    double weight = 0;
};

// Terms appearing in fewer than this many distinct values say nothing about
// how the corpus is structured.
constexpr std::size_t kMinTermValues = 2;

std::vector<VocabularyTerm> textVocabulary(const FactProfile& profile,
                                           const FieldStats& field,
                                           std::size_t limit = 12);

} // namespace Felidae::Celidae
