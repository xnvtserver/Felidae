# Focused executable contract for the interpreter-owned comparison path.
# No db import is required: all facts below live in Felidae's fact memory.

Source(category: "base")
PlainTarget(category: "base")
RelationshipTarget(category: "base")
Silent(category: "silent")
Deployment(id: "deploy-1", category: "base")
Application(id: "app-1")
BrokenDeployment(id: "deploy-broken", category: "base")
PartialSource(category: "base", label: "source")
PartialTarget(category: "base", label: "target")

# This is the normal inherited base membership method. It is selected for
# Source because Source has no more-specific membership declaration.
Fact.membership(input: Fact, against: Fact) =>
    return {category: input.category}

Silent.membership(input: Silent, against: Fact) =>
    return nil

PartialSource.membership(input: PartialSource, against: Fact) =>
    return {category: input.category, label: input.label}

RelationshipTarget.compareMembership(context: Fact) =>
    direct := Relation.find(input: context.relationships, name: "compatible")
    if direct != nil then
        return {
            state: "relationship-interpreted",
            relationshipCount: count(context.relationships),
            relationship: direct,
            evidence: context.structuralEvidence
        }
    else
        return {state: "relationship-missing", evidence: context.structuralEvidence}

main() =>
    source := Source(category: "base")
    plain := PlainTarget(category: "base")
    related := RelationshipTarget(category: "base")
    silent := Silent(category: "silent")
    deployment := Deployment(id: "deploy-1", category: "base")
    app := Application(id: "app-1")
    broken := BrokenDeployment(id: "deploy-broken", category: "base")
    partialSource := PartialSource(category: "base", label: "source")
    partialTarget := PartialTarget(category: "base", label: "target")
    deployment.depends(on: app)
    broken.depends(on: Application(id: "missing-app"))
    source.relate(to: related, as: Relationship(name: "compatible"), degree: 0.8, confidence: 0.9)
    exact := Relation.compare(left: source, right: plain)
    relationship := Relation.compare(left: source, right: related)
    incomparable := Relation.compare(left: silent, right: plain)
    dependencyOk := Relation.compare(left: deployment, right: plain)
    dependencyMissing := Relation.compare(left: source, right: broken)
    partial := Relation.compare(left: partialSource, right: partialTarget)
    dependencySatisfied := Dependency.satisfied(input: deployment)
    return {
        exact: exact,
        relationship: relationship,
        incomparable: incomparable,
        dependency_ok: dependencyOk,
        dependency_missing: dependencyMissing,
        partial: partial,
        dependency_satisfied: dependencySatisfied
    }
