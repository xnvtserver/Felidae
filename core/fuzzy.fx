# Deterministic fuzzy-logic primitives.
#
# A "degree" is a plain number in [0, 1]. Every function below is a fixed,
# repeatable formula over its inputs (clamping, minimum, maximum, linear
# interpolation) — never a learned weight, a trained model, or a sampled
# value — so the same inputs always produce exactly the same degree. This
# formalizes, as callable functions, the conjunction=minimum /
# disjunction=maximum / negation=one_minus policy that ReasoningProfile
# already fixes internally for Reasoning.grade (see ReasoningRuntime.cpp),
# and builds on core/numeric.fx's clamp/lerp/diff (themselves ported from the
# removed VM's NumericOperation set) instead of reimplementing them.
#
# Kept in their own module, separate from logic.fx's Logic.negate and its
# neighbors: those are declared with a dotted, capitalized name
# ("Logic.negate"), which IntegerParser.cpp's clause-head and call parsing
# does not resolve to a plain declared function outside a fixed fact-fluent
# whitelist (where/insert/get/all/count) or a registered native BuiltinId
# (math.*, json.*, ...). Every existing Logic.* helper is unreachable via its
# own name today, and — confirmed while building this module — loading
# logic.fx alongside unrelated identifiers can also corrupt parsing of the
# surrounding program entirely, not just fail the call. That is a
# pre-existing, load-order-sensitive parser bug in the dotted-declaration
# path; fixing it belongs with the interpreter rewamp's duplicate
# call-dispatch cleanup, not here. Every name below is plain (undotted) to
# stay well clear of it.

import "numeric".

fuzzyClamp(value: number) =>
    return clamp(value: value, low: 0, high: 1)

# Standard fuzzy negation: not(x) = 1 - x.
fuzzyNot(degree: number) =>
    return 1 - fuzzyClamp(value: degree)

# Zadeh conjunction/disjunction: and(a, b) = min(a, b), or(a, b) = max(a, b).
fuzzyAnd(a: number, b: number) =>
    return min([fuzzyClamp(value: a), fuzzyClamp(value: b)])

fuzzyOr(a: number, b: number) =>
    return max([fuzzyClamp(value: a), fuzzyClamp(value: b)])

# Linear ramp from degree 0 at `from` to degree 1 at `to`, via the same
# lerp the removed VM used - clamped since a score outside [from, to] must
# still land in [0, 1] rather than overshoot. A degenerate, zero-width ramp
# (from >= to) is already fully risen.
fuzzyRise(score: number, from: number, to: number) =>
    if from >= to then
        return 1
    else
        return clamp(value: (score - from) / (to - from), low: 0, high: 1)
    end
end

# Linear ramp from degree 1 at `from` down to degree 0 at `to`.
fuzzyFall(score: number, from: number, to: number) =>
    if from >= to then
        return 1
    else
        return clamp(value: (to - score) / (to - from), low: 0, high: 1)
    end
end

# Triangular membership of `score` in a RatingProfile{peak, fades_in,
# fades_out}: degree 0 at or below fades_in, rising linearly to 1 at peak,
# falling linearly back to 0 at fades_out, 0 at or beyond fades_out. A
# profile whose fades_in already equals peak (e.g. an "extreme" rating with
# no rising edge) is fully member (degree 1) for every score up to peak.
membership(score: number, profile: any) =>
    if score <= profile.fades_in then
        return fuzzyRise(score: score, from: profile.fades_in, to: profile.peak)
    elif score < profile.peak then
        return fuzzyRise(score: score, from: profile.fades_in, to: profile.peak)
    elif score == profile.peak then
        return 1
    elif score < profile.fades_out then
        return fuzzyFall(score: score, from: profile.peak, to: profile.fades_out)
    else
        return 0
    end
end

# General-purpose numeric closeness degree: 1 when a == b, falling off with
# their relative difference, clamped to [0, 1]. Deterministic and symmetric.
similarity(a: number, b: number) =>
    spread := max([math.abs(value: a), math.abs(value: b), 1])
    return fuzzyClamp(value: 1 - (diff(a: a, b: b) / spread))
end
