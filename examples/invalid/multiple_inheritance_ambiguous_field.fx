PublicDomain(domain: "public")
PrivateDomain(domain: "private")

# A child must choose a value when two direct parents expose incompatible
# fields.  Silent primary-parent precedence would make a fact's knowledge
# depend on declaration order.
ConflictedFact extend PublicDomain, PrivateDomain(name: "Ravi")
