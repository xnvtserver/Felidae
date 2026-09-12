# Deterministic fuzzy logic: syntax reference and worked example.
#
# `core/fuzzy.fx` adds callable fuzzy-logic primitives on top of the
# existing degree/confidence conventions (FactRelationship.degree/confidence
# in Memory.h, ReasoningProfile's conjunction=minimum / disjunction=maximum
# / negation=one_minus policy in ReasoningRuntime.cpp). Every primitive is a
# fixed formula over its inputs — clamp, min, max, linear interpolation —
# so the same inputs always produce exactly the same degree. Nothing here
# samples, learns, or depends on run order.

import "fuzzy".

# --- Combinators: fuzzyAnd/fuzzyOr/fuzzyNot ------------------------------
#
# fuzzyAnd(a, b) = min(a, b)   -- Zadeh conjunction
# fuzzyOr(a, b)  = max(a, b)   -- Zadeh disjunction
# fuzzyNot(d)    = 1 - d       -- standard fuzzy negation
combinators() =>
    warm := 0.8
    humid := 0.3
    return {
        muggy: fuzzyAnd(a: warm, b: humid),
        uncomfortable: fuzzyOr(a: warm, b: humid),
        cool: fuzzyNot(degree: warm)
    }

# --- Membership: a triangular fuzzy set -----------------------------------
#
# membership(score, profile) reads a {peak, fades_in, fades_out} map and
# returns the triangular degree of `score` in that set: 0 at or below
# fades_in, rising to 1 at peak, falling back to 0 at fades_out.
temperatureReadings() =>
    profile := {peak: 30, fades_in: 20, fades_out: 40}
    return {
        cold_reading: membership(score: 15, profile: profile),
        rising_reading: membership(score: 25, profile: profile),
        peak_reading: membership(score: 30, profile: profile),
        falling_reading: membership(score: 35, profile: profile),
        hot_reading: membership(score: 45, profile: profile)
    }

# --- Similarity: a general-purpose closeness degree -----------------------
#
# similarity(a, b) is 1 when a == b and falls off with their relative
# difference, clamped to [0, 1].
closenessChecks() =>
    return {
        identical: similarity(a: 70, b: 70),
        close: similarity(a: 70, b: 75),
        far: similarity(a: 70, b: 10)
    }

# --- Composing combinators with membership --------------------------------
#
# A rule can combine two membership degrees deterministically instead of
# branching: "comfortable" only where warm and NOT humid overlap.
comfortLevel(warm: number, humid: number) =>
    warmProfile := {peak: 30, fades_in: 15, fades_out: 40}
    dryProfile := {peak: 0, fades_in: 0, fades_out: 60}
    warmDegree := membership(score: warm, profile: warmProfile)
    dryDegree := membership(score: humid, profile: dryProfile)
    return fuzzyAnd(a: warmDegree, b: dryDegree)

main() =>
    return {
        combinators: combinators(),
        temperature: temperatureReadings(),
        closeness: closenessChecks(),
        comfort_dry_day: comfortLevel(warm: 28, humid: 10),
        comfort_muggy_day: comfortLevel(warm: 28, humid: 55)
    }
