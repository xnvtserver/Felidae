# Built-in normal membership rule for structured Relation.compare results.
# This module is loaded automatically by the interpreter when a Comparison is
# used as the left operand; programs never import a separate comparison engine.

Comparison.membership(input: Comparison, against: Fact) =>
    if input.state == "unresolved" then
        return nil
    else
        if input.state == "incomparable" then
            return nil
        else
            return {
                previousState: input.state,
                previousFamily: input.targetFamily,
                previousMembership: input.membership,
                previousEvidence: input.evidence
            }
