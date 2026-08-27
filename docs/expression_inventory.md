# Felidae Expression Inventory

This inventory is the compatibility baseline for the unified operator parser.
Each form must retain its grouping, diagnostics, and runtime behavior while
built-in expressions migrate to `OperatorExpression`.

| Form | Current AST/runtime representation | Required compatibility |
|---|---|---|
| Literals | `StringExpr`, `NumberExpr`, `BoolExpr`, `NilExpr` | Preserve literal value and source span. |
| Variables and typed names | `VarExpr` | Preserve `SymbolId` and type-name interpretation. |
| Arrays and maps | `ArrayExpr`, `MapExpr` | Parse nested complete expressions in every value position. |
| Calls and constructors | `TermExpr`, `CallGoal` | Preserve named and positional arguments and declaration-order checks. |
| Member access | `AccessExpr` | Remains protected and non-overloadable. |
| Unary arithmetic | parser-lowered unary `BinaryExpr`/literal | Migrate to protected built-in operator metadata without changing results. |
| Multiplicative arithmetic | `BinaryExpr` | Preserve precedence above additive expressions. |
| Additive arithmetic | `BinaryExpr` | Preserve left associativity and numeric-only `+`. |
| Ordering | `BinaryExpr`/`BinaryGoal` | Unified AST; overloadable for typed captures. |
| Strict equality | `BinaryExpr`/`BinaryGoal` | Unified AST but permanently protected. |
| Logical goals | grouped `Goal` nodes | Unified operator identity where possible; preserve logic and short-circuit semantics. |
| Assignment | `AssignGoal`, `MultiAssignGoal` | Structural statement syntax; never overloadable. |
| `where`, `return`, `else` | dedicated goal nodes | Structural method syntax; never overloadable. |
| Lambda | `LambdaExpr` | Its source, body, and optional comparison remain complete expressions. |
| `then` pipeline | `PipelineExpr` | Unified protected operator with ordered `system.result` semantics. |
| Fact selection | `FactSelectionExpr` | Runtime-only value; not custom operator syntax. |
| Rule implication | `ClauseStmt` | Structural declaration syntax; never overloadable. |

Primary regression fixtures are
`v2_examples/operator_expression_inventory.fx`, the current
`v2_examples/invalid_*.fx` rejection cases, and the inline C++ parser and
pipeline regression cases.
