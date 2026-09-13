# Fact iteration takes one stable RatingProfile snapshot. The callback returns
# a Degree for every fact; facts added after this call cannot enter this loop.
# The profiles are declared as facts, not local values, precisely so
# `lambda(RatingProfile, ...)` below - which iterates the fact store, not
# local bindings - actually sees them.
import "fuzzy"

RatingProfile(name: "Strong", peak: 75, fades_in: 50, fades_out: 90)
RatingProfile(name: "Acceptable", peak: 50, fades_in: 30, fades_out: 70)

def degreeFor(profile: RatingProfile) =>
    return membership(score: 68, profile: profile)

def main() =>
    profiles := lambda(RatingProfile, profile => profile)
    degrees := lambda(RatingProfile, profile => degreeFor(profile: profile))
    return {profiles: profiles, degrees: degrees}
