# Deterministic fuzzy-logic primitives.
#
# A "degree" is a plain number in [0, 1]. Every function below is a fixed,
# repeatable formula over its inputs (clamping, minimum, maximum, linear
# interpolation) — never a learned weight, a trained model, or a sampled
# value — so the same inputs always produce exactly the same degree. This
# exposes fuzzy behavior only as ordinary callable library functions and
# builds on core/numeric.fx's clamp/lerp/diff instead of reimplementing them.
# Interpreter conditions and solver success remain strictly boolean.
#
# Library functions use global undotted names. Dotted names identify class
# members or registered native libraries; Felidae has no namespace syntax.

import "numeric".

def fuzzyClamp(value: number) =>
    return clamp(value: value, low: 0, high: 1)
end

# Standard fuzzy negation: not(x) = 1 - x.
def fuzzyNot(degree: number) =>
    return 1 - fuzzyClamp(value: degree)
end

# Zadeh conjunction/disjunction: and(a, b) = min(a, b), or(a, b) = max(a, b).
def fuzzyAnd(a: number, b: number) =>
    return min([fuzzyClamp(value: a), fuzzyClamp(value: b)])
end

def fuzzyOr(a: number, b: number) =>
    return max([fuzzyClamp(value: a), fuzzyClamp(value: b)])
end

# Graded expert-system evidence is deliberately a library value, not solver
# truth. Callers must compare its numeric degrees explicitly before branching.
def fuzzyRecommendation(support: number, opposition: number) =>
    if support > opposition then
        return "recommend"
    elif opposition > support then
        return "reject"
    else
        return "undetermined"
    end
end

def fuzzyEvidence(support: number, opposition: number, reliability: number) =>
    supportDegree := fuzzyAnd(a: support, b: reliability)
    oppositionDegree := fuzzyAnd(a: opposition, b: reliability)
    return GradedEvidence(
        support_degree: supportDegree,
        opposition_degree: oppositionDegree,
        confidence: fuzzyClamp(value: reliability),
        contradictory: supportDegree > 0 and oppositionDegree > 0,
        recommendation: fuzzyRecommendation(
            support: supportDegree,
            opposition: oppositionDegree
        )
    )
end

# Linear ramp from degree 0 at `from` to degree 1 at `to`, via the same
# linear interpolation, clamped since a score outside [from, to] must
# still land in [0, 1] rather than overshoot. A degenerate, zero-width ramp
# (from >= to) is already fully risen.
def fuzzyRise(score: number, from: number, to: number) =>
    if from >= to then
        return 1
    else
        return clamp(value: (score - from) / (to - from), low: 0, high: 1)
    end
end

# Linear ramp from degree 1 at `from` down to degree 0 at `to`.
def fuzzyFall(score: number, from: number, to: number) =>
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
def membership(score: number, profile: any) =>
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
def similarity(a: number, b: number) =>
    spread := max([math.abs(value: a), math.abs(value: b), 1])
    return fuzzyClamp(value: 1 - (diff(a: a, b: b) / spread))
end
