# Interpreter performance and scalability plan

## Current verified architecture

- Source files are lexed from bounded buffered input.
- Parser lookahead is demand-driven and consumed statement tokens are released.
- Parsed statements are registered immediately.
- Registered builtins dispatch through `BuiltinId`.
- Native set and group mathematics live outside `Interpreter.cpp`.

## Highest-value next optimizations

### 1. Stop serializing the complete fact store for every native call

`solveNativeCall` currently appends `__facts` to every native request. This makes
an unrelated CSV, string, set, or group call scale with the total database size.

Add native package capabilities to declarations or package metadata:

- `needs_fact_snapshot`
- `pure`
- `thread_safe`
- `supports_batch`

Only fact-aware functions should receive a fact snapshot. Where possible, pass
only requested fact types or indexed candidates.

### 2. Cache stable hashes for facts and immutable values

Set operations currently hash their JSON-stable representation inside the
native package. Add a stable runtime value hash that is computed once and
preserves fact type plus resolved fields. Reuse it for:

- exact set membership
- unification fast rejection
- distinct operations
- native serialization caches
- fact indexes

### 3. Add a generic member-call AST

The parser currently supports field access and `then` pipelines, but not
`value.method(...)`. Implement one generic receiver-call representation:

```text
receiver.method(args) -> method(receiver: receiver, args...)
```

This must be namespace-neutral. Set `.by(...)` should then be a library method,
not parser or interpreter special handling.

### 4. Compile method dispatch plans

Cache per-call-site information after first resolution:

- symbol ID and compatible clause list
- named-argument positions
- native declaration overload
- effect classification
- fact index selection

Invalidate only when a newly registered statement changes that symbol.

### 5. Reduce environment allocation

Profiles still show environment-frame and standardization costs for recursive
logic. Use:

- small-vector/local storage for tiny environments
- copy-on-write bindings by generation
- reusable standardization templates
- iterative execution frames for tail-recursive method calls

### 6. Batch native calls

Native JSON parsing and dynamic-library boundary costs dominate small
operations. Add batch entry points for repeated pure calls and retain parsed
package state between calls.

### 7. Separate load, overlap, and execution metrics

Keep parser time, import time, native-load time, first-main time, and continued
post-main parsing as separate measurements. End-to-end process time remains
useful but is noisy for sub-50 ms workloads.

## Visualization review

The current Celidae HTML is a dependency graph, not a complete ER diagram.

Working:

- fact types and observed fields are discovered
- record counts and field coverage are calculated
- `extend` relationships are represented
- method/global/library dependencies are represented

Missing for a correct ER view:

- entity cards containing their attributes
- primary/foreign-key metadata
- relationship inference or explicit relationship declarations
- one-to-one, one-to-many, and many-to-many cardinality
- visible edge labels and arrow direction
- collision-resistant layout for large schemas
- zoom, pan, search, entity filtering, and accessible details

The ER renderer should be a distinct visualization mode. It should use explicit
schema/relationship facts where available and must not infer foreign keys only
from matching field names.
