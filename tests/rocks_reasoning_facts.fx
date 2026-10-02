class Observation
    key(id)
    id: string
end

Observation(id: "observation-1")

def eligible(id: x) =>
    Observation(id: x)
end

def main() =>
    proof := reasoning.prove(query: eligible(id: "observation-1"))
    return (
        truth: proof.truth_status,
        support: proof.support_count,
        provenance: proof.supporting_facts.len(),
        rules: proof.supporting_rules.len()
    )
end
