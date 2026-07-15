provenance:BuildFromRecording(rec: rec, store: store, goal: goal, options: options, out: proof) =>
    proof == {status: "ok", source: "recording"}.

provenance:Print(stream: stream, proof: proof, out: result) =>
    result == fn:tuple(first:"printed",second: stream, third: proof.status).
