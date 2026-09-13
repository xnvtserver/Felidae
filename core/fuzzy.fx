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
# Kept in their own module regardless, separate from logic.fx's Logic.negate
# and its neighbors: those group explicit rule transformations under a
# shared `Logic.` namespace prefix, a different organizing concern from the
# fixed degree formulas here. (A dotted, capitalized declaration head like
# `def Logic.negate(...)`, and a call to it in expression position, both
# used to be unreachable - IntegerParser.cpp's consumeQualifiedName refused
# to continue a dotted name past a capitalized first segment, and the
# Object:invokeMember fallback a call fell through to only ever resolves a
# receiver that evaluates to a runtime fact/class value, which a bare
# namespace prefix like "Logic" never does. Both are fixed: see
# consumeQualifiedName's `allowCapitalizedDotted` and evalBuiltinTerm's
# kMemberInvokeTerm retry.) Every name below stays plain (undotted) on its
# own merits - these are meant to read as ordinary function calls, not as
# `Fuzzy.` namespace members.

import "numeric".

def fuzzyClamp(value: number) =>
    return clamp(value: value, low: 0, high: 1)

# Standard fuzzy negation: not(x) = 1 - x.
def fuzzyNot(degree: number) =>
    return 1 - fuzzyClamp(value: degree)

# Zadeh conjunction/disjunction: and(a, b) = min(a, b), or(a, b) = max(a, b).
def fuzzyAnd(a: number, b: number) =>
    return min([fuzzyClamp(value: a), fuzzyClamp(value: b)])

def fuzzyOr(a: number, b: number) =>
    return max([fuzzyClamp(value: a), fuzzyClamp(value: b)])

# Linear ramp from degree 0 at `from` to degree 1 at `to`, via the same
# lerp the removed VM used - clamped since a score outside [from, to] must
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

# Trapezoidal membership: 0 at or below `a`, rising linearly to 1 across
# [a, b], flat at 1 across [b, c], falling linearly back to 0 across [c, d],
# 0 at or beyond `d`. Degenerate edges collapse correctly: b == c gives the
# triangular shape membership() below builds on, and a == b (or c == d)
# gives a vertical edge instead of a ramp (fuzzyRise/fuzzyFall already
# return a saturated 0 or 1 for a zero-width span, not a division by zero).
#
# The two ramps are independent and already saturate outside their own
# span (fuzzyRise is 1 once score >= b; fuzzyFall is 1 while score <= c),
# so their fuzzyAnd (minimum) traces the whole trapezoid without a
# separate branch per region - the flat top is simply where both ramps
# have already saturated to 1.
def trapezoid(score: number, a: number, b: number, c: number, d: number) =>
    return fuzzyAnd(
        a: fuzzyRise(score: score, from: a, to: b),
        b: fuzzyFall(score: score, from: c, to: d))

# Triangular membership of `score` in a RatingProfile{peak, fades_in,
# fades_out}: degree 0 at or below fades_in, rising linearly to 1 at peak,
# falling linearly back to 0 at fades_out, 0 at or beyond fades_out. A
# profile whose fades_in already equals peak (e.g. an "extreme" rating with
# no rising edge) is fully member (degree 1) for every score up to peak.
# A triangle is a trapezoid whose flat top has collapsed to one point.
def membership(score: number, profile: any) =>
    return trapezoid(
        score: score, a: profile.fades_in, b: profile.peak,
        c: profile.peak, d: profile.fades_out)

# General-purpose numeric closeness degree: 1 when a == b, falling off with
# their relative difference, clamped to [0, 1]. Deterministic and symmetric.
def similarity(a: number, b: number) =>
    spread := max([math.abs(value: a), math.abs(value: b), 1])
    return fuzzyClamp(value: 1 - (diff(a: a, b: b) / spread))
end

# --- N-ary combinators -------------------------------------------------------
#
# fuzzyAnd/fuzzyOr take exactly two degrees because every mixfix and operator
# call site wants a fixed arity; these take the same Zadeh conjunction/
# disjunction across a whole array at once (min([...])/max([...]) already do
# this - these exist so a caller building up a list of rule degrees does not
# have to fold fuzzyAnd/fuzzyOr by hand).
def fuzzyAndAll(degrees: array) =>
    return min(degrees)

def fuzzyOrAll(degrees: array) =>
    return max(degrees)

# --- Derived connectives ------------------------------------------------------
#
# Each of these is a fixed formula over fuzzyAnd/fuzzyOr/fuzzyNot - no new
# combinator logic, just the standard boolean identities carried over to
# graded truth so De Morgan's laws keep holding at any degree.
def fuzzyNand(a: number, b: number) =>
    return fuzzyNot(degree: fuzzyAnd(a: a, b: b))

def fuzzyNor(a: number, b: number) =>
    return fuzzyNot(degree: fuzzyOr(a: a, b: b))

# Symmetric difference: how much a and b disagree. 0 when equal, 1 when
# exactly opposite (e.g. 0 and 1).
def fuzzyXor(a: number, b: number) =>
    return diff(a: fuzzyClamp(value: a), b: fuzzyClamp(value: b))

# Lukasiewicz implication: "a implies b" as a degree, 1 whenever b is at
# least as true as a, falling off only when b is less true than a.
def fuzzyImplication(a: number, b: number) =>
    return fuzzyClamp(value: 1 - fuzzyClamp(value: a) + fuzzyClamp(value: b))

# --- Alternative conjunction/disjunction pairs --------------------------------
#
# fuzzyAnd/fuzzyOr (min/max, the Zadeh pair) are the default combinators used
# throughout this module. These are the two other standard t-norm/t-conorm
# pairs from fuzzy logic, each satisfying the same De Morgan duality with its
# own fuzzyNot - offered because different domains conventionally pick
# different pairs (e.g. algebraic product/sum for independent probabilities),
# not because Zadeh's pair is wrong.
#
# Algebraic product/sum: as if a and b were independent probabilities.
def fuzzyAlgebraicAnd(a: number, b: number) =>
    return fuzzyClamp(value: a) * fuzzyClamp(value: b)

def fuzzyAlgebraicOr(a: number, b: number) =>
    x := fuzzyClamp(value: a)
    y := fuzzyClamp(value: b)
    return x + y - x * y

# Lukasiewicz product/sum ("bounded" conjunction/disjunction): stricter than
# both Zadeh and algebraic - degrees must overlap by more before the
# conjunction rises above 0, and less before the disjunction saturates at 1.
def fuzzyBoundedAnd(a: number, b: number) =>
    return clamp(value: fuzzyClamp(value: a) + fuzzyClamp(value: b) - 1, low: 0, high: 1)

def fuzzyBoundedOr(a: number, b: number) =>
    return clamp(value: fuzzyClamp(value: a) + fuzzyClamp(value: b), low: 0, high: 1)

# --- Linguistic hedges ---------------------------------------------------------
#
# Concentration/dilation, the standard formulas for hedges like "very" and
# "somewhat" modifying a fuzzy adjective (Zadeh 1972): squaring pulls a degree
# down (very tall is harder to satisfy than tall), the square root pulls it up
# (somewhat tall is easier). Both are identity at 0 and 1 and fixed points
# only there, so repeated hedging keeps moving in the same direction.
def fuzzyVery(degree: number) =>
    return square(value: fuzzyClamp(value: degree))

def fuzzySomewhat(degree: number) =>
    return math.sqrt(value: fuzzyClamp(value: degree))
