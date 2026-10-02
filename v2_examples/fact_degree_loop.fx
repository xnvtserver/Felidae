# Fact iteration takes one stable RatingProfile snapshot. The callback returns
# a Degree for every fact; facts added after this call cannot enter this loop.

import "fuzzy".

RatingProfile(name: "", peak: 0, fades_in: 0, fades_out: 0)
RatingProfile(name: "Strong", peak: 75, fades_in: 50, fades_out: 90)
RatingProfile(name: "Acceptable", peak: 50, fades_in: 30, fades_out: 70)

def degreeFor(profile: RatingProfile) =>
    return membership(68, profile)
end

def main() =>
    profiles := RatingProfile.all()
    degrees := lambda(profiles, profile => degreeFor(profile: profile))
    return {profiles: profiles, degrees: degrees}
end
