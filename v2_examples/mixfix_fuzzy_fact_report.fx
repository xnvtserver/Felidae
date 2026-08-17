# Strict-IR expert-system baseline. The declarations model hierarchy and
# time/priority provenance; the returned values remain numeric Degrees/facts,
# never implicit booleans.

Evidence(name: "", subject: "", fx.observed_at: 0, priority: 0)
Assessment extend Evidence(name: "", subject: "", fx.observed_at: 0, priority: 0, degree: 0)
RatingProfile extend Assessment(
    name: "", subject: "rating", fx.observed_at: 0, priority: 0,
    degree: 0, peak: 0, fades_in: 0, fades_out: 100)

@mixfix(pattern: "{score: number} assessed by {profile: RatingProfile}")
assess(score: number, profile: RatingProfile) =>
    return membership(score, profile)

degreeFor(profile: RatingProfile) =>
    return membership(68, profile)

main() =>
    critical := RatingProfile(name: "Critical", subject: "Strongly Disagree", fx.observed_at: 20260817, priority: 0, degree: 0, peak: 0, fades_in: 0, fades_out: 30)
    subpar := RatingProfile(name: "Subpar", subject: "Disagree", fx.observed_at: 20260817, priority: 30, degree: 0, peak: 30, fades_in: 10, fades_out: 50)
    acceptable := RatingProfile(name: "Acceptable", subject: "Neutral", fx.observed_at: 20260817, priority: 50, degree: 0, peak: 50, fades_in: 30, fades_out: 70)
    strong := RatingProfile(name: "Strong", subject: "Agree", fx.observed_at: 20260817, priority: 75, degree: 0, peak: 75, fades_in: 50, fades_out: 90)
    exceptional := RatingProfile(name: "Exceptional", subject: "Strongly Agree", fx.observed_at: 20260817, priority: 100, degree: 0, peak: 100, fades_in: 75, fades_out: 100)
    mixfix_degree := 68 assessed by strong
    profile_degrees := for_each_fact(RatingProfile, degreeFor)
    return (
        mixfix_degree: mixfix_degree,
        profile_degrees: profile_degrees,
        observed_at: 20260817,
        strongest_profile: exceptional)
