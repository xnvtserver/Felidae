# Felidae training datasets

This directory contains reusable, versioned training inputs only. Generated
model weights and manifests belong in `models/`; temporary probes and build
outputs belong in `build/`.

- `compiler/` contains the deterministic mixfix corpus extracted from valid
  examples and a separate invalid-source rejection-evaluation corpus.
- `vm/` contains operation-level identity and fact/hierarchy-context teachers.
  Broader `SSM_PROCESS` teachers remain a later corpus.

Corpus builders must write a replacement only after validating every record.
They must never append to an older schema or use invalid Felidae programs as
IR targets.
