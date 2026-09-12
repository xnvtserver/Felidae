# Deterministic fuzzy primitives. `Degree` results remain values in facts and
# maps; only the explicit threshold condition below produces a branch.

import "fuzzy".

RatingProfile(name: "", peak: 0, fades_in: 0, fades_out: 0).
DegreeReport(subject: "", similarity: 0, membership: 0, confidence: 0, truth_degree: 0).

# Same-line `Name() => return EXPR` clause bodies do not parse when another
# clause follows them (confirmed: a minimal `a() => return 1` / `b() =>
# return 2` pair fails the same way, independent of content or the fact
# vocabulary) — a pre-existing IntegerParser.cpp bug, not something to design
# examples around. Each clause body goes on its own indented line instead.
critical() =>
    return RatingProfile(name: "Critical / Strongly Disagree", peak: 0, fades_in: 0, fades_out: 30)

subpar() =>
    return RatingProfile(name: "Subpar / Disagree", peak: 30, fades_in: 10, fades_out: 50)

acceptable() =>
    return RatingProfile(name: "Acceptable / Neutral", peak: 50, fades_in: 30, fades_out: 70)

strong() =>
    return RatingProfile(name: "Strong / Agree", peak: 75, fades_in: 50, fades_out: 90)

exceptional() =>
    return RatingProfile(name: "Exceptional / Strongly Agree", peak: 100, fades_in: 75, fades_out: 100)

# `75%` (a bare numeric percent suffix) is not a supported literal — it is
# not used anywhere else in the codebase and fails to parse ("Expected an
# expression") the moment it is actually exercised; 0.75 is the equivalent
# plain decimal degree.
threshold(degree: number) =>
    if degree >= 0.75 then
        return "met"
    else
        return "not-met"

main() =>
    score := 68
    profile := strong()
    membershipDegree := membership(score: score, profile: profile)
    closenessDegree := similarity(a: score, b: 75)
    report := DegreeReport(subject: "quality", similarity: closenessDegree, membership: membershipDegree, confidence: 0.82, truth_degree: membershipDegree)
    state := threshold(degree: membershipDegree)
    return {rating: profile, report: report, threshold: state}
