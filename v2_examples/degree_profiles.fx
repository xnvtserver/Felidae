# Deterministic fuzzy primitives. `Degree` results remain values in facts and
# maps; only the explicit threshold condition below produces a branch.

RatingProfile(name: "", peak: 0, fades_in: 0, fades_out: 0)
DegreeReport(subject: "", similarity: 0, membership: 0, confidence: 0, truth_degree: 0)

critical() => return RatingProfile(name: "Critical / Strongly Disagree", peak: 0, fades_in: 0, fades_out: 30)
subpar() => return RatingProfile(name: "Subpar / Disagree", peak: 30, fades_in: 10, fades_out: 50)
acceptable() => return RatingProfile(name: "Acceptable / Neutral", peak: 50, fades_in: 30, fades_out: 70)
strong() => return RatingProfile(name: "Strong / Agree", peak: 75, fades_in: 50, fades_out: 90)
exceptional() => return RatingProfile(name: "Exceptional / Strongly Agree", peak: 100, fades_in: 75, fades_out: 100)

threshold(degree: number) =>
    if degree >= 75% then
        return "met"
    else
        return "not-met"

main() =>
    score := 68
    profile := strong()
    membershipDegree := membership(score, profile)
    closenessDegree := similarity(score, 75)
    report := DegreeReport(subject: "quality", similarity: closenessDegree, membership: membershipDegree, confidence: 0.82, truth_degree: membershipDegree)
    state := threshold(membershipDegree)
    return {rating: profile, report: report, threshold: state}
