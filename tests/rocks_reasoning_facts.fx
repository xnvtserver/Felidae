class Observation
    key(id).
    def id: string.
end

def Observation(id: "observation-1").

def eligible(id: x) =>
    def Observation(id: x).
end

def main() =>
    def proof := reasoning.prove(query: eligible(id: "observation-1")).
    (
        truth: proof.truth_status,
        support: proof.support_count,
        provenance: proof.supporting_facts.len(),
        rules: proof.supporting_rules.len()
    ).
end
