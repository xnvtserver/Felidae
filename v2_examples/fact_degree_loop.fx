# Fact iteration takes one stable RatingProfile snapshot. The callback returns
# a Degree for every fact; facts added after this call cannot enter this loop.

RatingProfile(name: "", peak: 0, fades_in: 0, fades_out: 0)

degreeFor(profile: RatingProfile) =>
    return membership(68, profile)

main() =>
    strong := RatingProfile(name: "Strong", peak: 75, fades_in: 50, fades_out: 90)
    acceptable := RatingProfile(name: "Acceptable", peak: 50, fades_in: 30, fades_out: 70)
    degrees := for_each_fact(RatingProfile, degreeFor)
    return {profiles: [strong, acceptable], degrees: degrees}
