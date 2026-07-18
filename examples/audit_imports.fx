import ("audit_modules/provenance.fx", "audit_modules/configs.fx").

AuditProof(proofs: proofs) =>
    proofs := provenance.BuildFromRecording(rec: "rec", store: "store", goal: "goal", options: {}),
    proofs.status == "ok".

PrettyPrint(result: result) =>
    proofs := provenance.BuildFromRecording(rec: "rec", store: "store", goal: "goal", options: {}),
    result := provenance.Print(stream: "stdout", proof: proofs).

ConfigPort(port: port) =>
    ServerConfig(name: "primary", port: port).
