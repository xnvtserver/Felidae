# Ancestor-hierarchy relatedness and value similarity, worked end to end.
#
# Two complementary, both-deterministic notions of "how related are these
# two facts":
#   - Type relatedness (core/ancestry.fx): lca/mca walk the `extend`
#     hierarchy and relatedness()/isCloseRelative()/isDistantRelative() turn
#     the lowest common ancestor's combined distance into a [0, 1] degree.
#   - Value similarity (core/fuzzy.fx): similarity(a, b) compares a shared
#     numeric field directly, independent of type distance entirely - two
#     cousins can still have near-identical weights.
#
# A four-species hierarchy gives every relationship shape: same species
# (distance 0), siblings sharing an immediate parent (Dog/Wolf under Canine,
# distance 2), first cousins sharing a grandparent through different parent
# branches (Dog/Cat: Canine vs Feline under Mammal, distance 4), and
# unrelated branches (Cat/Eagle: Mammal vs Bird, sharing only Animal,
# distance 4 as well - the same combined distance as the cousins, since both
# pairs are two steps from the shared ancestor on each side).

import "ancestry".
import "fuzzy".

Animal(name: "", weight_kg: 0).
Mammal extend Animal(name: "", weight_kg: 0).
Bird extend Animal(name: "", weight_kg: 0).
Canine extend Mammal(name: "", weight_kg: 0).
Feline extend Mammal(name: "", weight_kg: 0).
Dog extend Canine(name: "", weight_kg: 0).
Wolf extend Canine(name: "", weight_kg: 0).
Cat extend Feline(name: "", weight_kg: 0).
Eagle extend Bird(name: "", weight_kg: 0).

# --- Pairwise relationship report -------------------------------------------
#
# `threshold: 0.3` marks anything at or nearer than sibling distance (2, a
# shared immediate parent) as "close": identical-type pairs (distance 0,
# degree 1) and siblings (distance 2, degree 0.333) both clear it; cousins
# and unrelated pairs (both distance 4, degree 0.2) fall under it.
def relationshipReport(left: any, right: any) =>
    return {
        left: left,
        right: right,
        lca: lca(left: left, right: right).selected,
        mca: mca(left: left, right: right).selected,
        type_relatedness: relatedness(left: left, right: right),
        close_relative: isCloseRelative(left: left, right: right, threshold: 0.3),
        distant_relative: isDistantRelative(left: left, right: right, threshold: 0.3),
        weight_similarity: similarity(a: left.weight_kg, b: right.weight_kg)
    }

def main() =>
    rex := Dog(name: "Rex", weight_kg: 30)
    fido := Dog(name: "Fido", weight_kg: 28)
    wolfie := Wolf(name: "Wolfie", weight_kg: 40)
    tom := Cat(name: "Tom", weight_kg: 5)
    aquila := Eagle(name: "Aquila", weight_kg: 6)

    return {
        # Same species: distance 0, degree 1 - always "close" regardless of
        # weight, but weight_similarity still varies independently.
        same_species: relationshipReport(left: rex, right: fido),
        # Siblings sharing an immediate parent (Canine): distance 2, degree
        # 0.333 - still "close" at this threshold.
        siblings: relationshipReport(left: rex, right: wolfie),
        # Cousins: Dog and Cat share grandparent Mammal through different
        # parent branches (Canine vs Feline): distance 4, degree 0.2 - a
        # 30kg vs 5kg weight is not close either, so both measures agree.
        cousins: relationshipReport(left: rex, right: tom),
        # Unrelated branches (Mammal vs Bird, sharing only Animal): distance
        # 4, the same type-relatedness as the cousins above, but weight
        # happens to be nearly identical - the two measures disagree here,
        # which is exactly why they are kept as separate, independent
        # degrees rather than folded into one score.
        unrelated_but_similar_weight: relationshipReport(left: tom, right: aquila)
    }
