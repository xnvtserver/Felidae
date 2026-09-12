# Short, commonly-used names for the native ancestor-hierarchy analysis
# (commonAncestors/lowestCommonAncestor/highestCommonAncestor,
# Interpreter.cpp:evalAncestorAnalysis) plus a deterministic relatedness
# degree built from it. These operate on the `extend` hierarchy between fact
# TYPES, not a genealogy of individual fact instances - "left"/"right" are
# two typed fact values, and the analysis finds their nearest/farthest
# shared ancestor TYPE.

def lca(left: any, right: any) =>
    return lowestCommonAncestor(left: left, right: right)

def mca(left: any, right: any) =>
    return highestCommonAncestor(left: left, right: right)

# Deterministic [0, 1] relatedness degree from the LCA's combined hierarchy
# distance: 1.0 for identical types (distance 0), decaying smoothly and
# never reaching exactly 0 (a positive but arbitrarily small combined
# distance still returns a positive degree). No arbitrary max-depth constant
# is needed since 1 / (1 + distance) is already bounded to (0, 1].
def relatedness(left: any, right: any) =>
    found := lca(left: left, right: right)
    where found.status == "none"
    return 0
else
    nearest := found.lowest.get(pos: 0)
    totalDistance := nearest.left_distance + nearest.right_distance
    return 1 / (1 + totalDistance)

# A pair counts as close relatives when their relatedness degree is at or
# above `threshold` (default 0.5, i.e. combined hierarchy distance <= 1).
def isCloseRelative(left: any, right: any, threshold: number) =>
    return relatedness(left: left, right: right) >= threshold

def isDistantRelative(left: any, right: any, threshold: number) =>
    return relatedness(left: left, right: right) < threshold
