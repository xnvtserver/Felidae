# A reconstructed map cannot select one of two structurally equal rows for a
# relationship/dependency attachment or comparison.  Fetch the intended fact
# through Fact.all/select so the hidden runtime identity is retained.

Twin(label: "same")
Twin(label: "same")
Target(name: "target")

main() =>
    ambiguous := Twin(label: "same")
    target := Target(name: "target")
    return Relation.compare(left: ambiguous, right: target)
