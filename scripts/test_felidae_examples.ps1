param(
    [string]$Exe = "build\felidae.exe",
    [string]$CelidaeExe = "build\celidae.exe",
    [string]$DebugExe = "build\felidae_debug.exe",
    [switch]$ReadOnly
)

$ErrorActionPreference = "Continue"

if (-not (Test-Path -LiteralPath $Exe)) {
    Write-Error "Missing $Exe. Build first, for example: .\build.cmd"
}

$smokeSource = "native_modules\smoke\NativeSmoke.cpp"
$smokeLibrary = "native_modules\smoke\felidae_smoke.dll"
if ((Test-Path -LiteralPath $smokeSource) -and (Get-Command clang++ -ErrorAction SilentlyContinue)) {
    $needsSmokeBuild = -not (Test-Path -LiteralPath $smokeLibrary)
    if (-not $needsSmokeBuild) {
        $needsSmokeBuild = (Get-Item -LiteralPath $smokeSource).LastWriteTimeUtc -gt
            (Get-Item -LiteralPath $smokeLibrary).LastWriteTimeUtc
    }
    if ($needsSmokeBuild) {
        & clang++ -std=c++17 -shared $smokeSource -o $smokeLibrary 2>&1 | Out-Null
    }
}

$tests = @(
    @{ Name = "family parent"; File = "examples\family.fx"; Query = "? Parent(parent: Parent, child: Child)"; Expect = @("Parent = `"Alice`"", "Child = `"Bob`"") },
    @{ Name = "member access manager"; File = "examples\member_access_list.fx"; Query = "? EmployeeAt(0, e), HasManager(e, name)"; Expect = @("name = `"Alice`"") },
    @{ Name = "positional typed method expression"; File = "examples\member_access_list.fx"; Query = "? sum(1, 2) == 3"; Expect = @("true") },
    @{ Name = "member access nil"; File = "examples\member_access_list.fx"; Query = "? EmployeeAt(1, e), NoManager(e, name)"; Expect = @("name = `"Carol`"") },
    @{ Name = "anonymous named field"; File = "examples\anonymous_and_arrays.fx"; Query = "? EmployeeSea(name: Name)"; Expect = @("Name = `"Alice`"") },
    @{ Name = "array assignment"; File = "examples\anonymous_and_arrays.fx"; Query = "? ArrayAssigned(value: value)"; Expect = @("value = 3") },
    @{ Name = "tuple"; File = "examples\structures.fx"; Query = "? TupleTest(value: value)"; Expect = @("value = fn:tuple(value: `"hello`", value: `"world`", value: `"!`")") },
    @{ Name = "nested map access"; File = "examples\structures.fx"; Query = "? StructureExtractionTest(x: x, w: w)"; Expect = @("x = 1", "w = `"hello`"") },
    @{ Name = "json nested access"; File = "examples\structures.fx"; Query = "? JsonNestedTest(value: value)"; Expect = @("value = `"hello`"") },
    @{ Name = "complex nested fact declaration"; File = "examples\complex_facts.fx"; Query = "? EmployeePlace(value: value)"; Expect = @("value = `"ABC location`"") },
    @{ Name = "multiline nested fact declaration"; File = "examples\multiline_nested_fact.fx"; Query = "? User(id: id, name: name, region: region, status: status)"; Expect = @("id = 1", "name = `"Alice`"", "region = {name: `"south`", region_id: 20}", "status = `"active`"") },
    @{ Name = "global binding in fact declaration"; File = "examples\complex_facts.fx"; Query = "? AddressRadiusUnit(value: value)"; Expect = @("value = `"cm`"") },
    @{ Name = "head variables declared"; File = "examples\head_vars.fx"; Query = "? HeadManagerName(name: name)"; Expect = @("name = `"Alice`"") },
    @{ Name = "omitted head output binds in method body"; File = "examples\head_vars.fx"; Query = ""; Expect = @("Hello, World!", 'has_manager: "Alice"') },
    @{ Name = "type concrete name"; File = "examples\types.fx"; Query = "? SampleTypeName(name: name)"; Expect = @("name = `"Employee`"") },
    @{ Name = "instanceof parent"; File = "examples\types.fx"; Query = "? SampleEmployeeIsPerson(name: name)"; Expect = @("name = `"Ravi`"") },
    @{ Name = "instanceof self"; File = "examples\types.fx"; Query = "? SampleEmployeeIsEmployee(name: name)"; Expect = @("name = `"Ravi`"") },
    @{ Name = "lazy multi import"; File = "examples\audit_imports.fx"; Query = "? PrettyPrint(result: result)"; Expect = @("result = fn:tuple(value: `"printed`", value: `"stdout`", value: `"ok`")") },
    @{ Name = "multi-file function call"; File = "examples\function_caller.fx"; Query = "? CallerResult(name: name, role: role)"; Expect = @("name = `"Anu`"", "role = `"Student`"") },
    @{ Name = "or operator"; File = "examples\operators.fx"; Query = "? TechnicalOrManager(name: name)"; Expect = @("name = `"Alice`"", "name = `"Bob`"") },
    @{ Name = "grouped or and"; File = "examples\operators.fx"; Query = "? TechnicalArchitectManager(name: name)"; Expect = @("name = `"Alice`"", "name = `"Bob`"") },
    @{ Name = "fallback first branch"; File = "examples\operators.fx"; Query = "? FallbackEngineer(name: name, role: role)"; Expect = @("name = `"Alice`"", "role = `"Engineer`"") },
    @{ Name = "fallback later branch"; File = "examples\operators.fx"; Query = "? FallbackArchitect(name: name, role: role)"; Expect = @("name = `"Bob`"", "role = `"Architect`"") },
    @{ Name = "fallback default branch"; File = "examples\operators.fx"; Query = "? FallbackUnsupported(name: name, role: role)"; Expect = @("name = `"Carol`"", "role = `"Unsupported`"") },
    @{ Name = "typed exception result recovery"; File = "examples\exceptions.fx"; Query = ""; Expect = @('ok: true', 'quotient: 4', 'quotient: 0', 'recovered: true', 'Choose a supported calculator operation') },
    @{ Name = "lambda method return adult"; File = "examples\method_style.fx"; Query = "? Adults(value: value)"; Expect = @("value = [{name: `"Ravi`"}]") },
    @{ Name = "method rejects incompatible type"; File = "examples\method_style.fx"; Query = "? isAdult(input: {__type: `"Animal`", name: `"Tiger`", age: 5}, name: name)"; Expect = @("false") },
    @{ Name = "method rejects untyped map"; File = "examples\method_style.fx"; Query = "? isAdult(input: {name: `"Tiger`", age: 5}, name: name)"; Expect = @("false") },
    @{ Name = "lambda fact map"; File = "examples\method_style.fx"; Query = "? Names(value: value)"; Expect = @("value = [`"Default`", `"Ravi`", `"Anu`"]") },
    @{ Name = "lambda array map"; File = "examples\method_style.fx"; Query = "? ParsedDocs(result: result)"; Expect = @("result = [`"Parsed: Primary`", `"Parsed: Secondary`"]") },
    @{ Name = "typed builtin number method"; File = "examples\typed_methods.fx"; Query = "? TypedAdd(value: value)"; Expect = @("value = 5") },
    @{ Name = "typed builtin decimal method"; File = "examples\typed_methods.fx"; Query = "? TypedDecimalAdd(value: value)"; Expect = @("value = 3") },
    @{ Name = "typed fact method"; File = "examples\typed_methods.fx"; Query = "? TypedEmployeeName(value: value)"; Expect = @("value = `"Alice`"") },
    @{ Name = "int typed mixed access"; File = "examples\access_interchange.fx"; Query = "? ReadAccess(input: SampleAccess(nested: {left: {right: {value: 42}}}), value: value)"; Expect = @("value = 42") },
    @{ Name = "native ABI goal output"; File = "examples\native_abi_success.fx"; Query = "? NativeEchoGoal(value: GoalInput, access: access)"; Expect = @("access = `"goal ok`"") },
    @{ Name = "backtracking cartesian choices"; File = "examples\backtracking_unification.fx"; Query = "? Choice(color: color, shape: shape)"; Expect = @("Solution 1: color = `"red`", shape = `"circle`"", "Solution 4: color = `"blue`", shape = `"square`"") },
    @{ Name = "unification repeated variable"; File = "examples\backtracking_unification.fx"; Query = "? SamePair(value: value)"; Expect = @("value = `"red`"", "value = `"green`"") },
    @{ Name = "unification anonymous variable"; File = "examples\backtracking_unification.fx"; Query = "? AnyEmployee(name: name)"; Expect = @("name = `"Alice`"", "name = `"Bob`"") },
    @{ Name = "unification nested map pattern"; File = "examples\backtracking_unification.fx"; Query = "? NestedEmployee(name: name, role: role, office: office)"; Expect = @("name = `"Alice`", role = `"Engineer`", office = `"SEA`"", "name = `"Bob`", role = `"Manager`", office = `"LAX`"") },
    @{ Name = "method truth tuple values"; File = "examples\method_truth_tuple.fx"; Query = ""; Expect = @('manager_true: "Alice"', 'manager_false: fn:tuple(value: true, value: false)', 'technical_manager: fn:tuple(value: true, value: true)', 'ancestor_true: fn:tuple(value: true)', 'nested_false: fn:tuple(value: false)', 'print_once: fn:tuple(value: true)', 'lambda_result: ["Alice", "Carol"]', 'status: "ok"', 'explicit_true: true') },
    @{ Name = "tuple destructuring assignment"; File = "examples\tuple_assignment.fx"; Query = ""; Expect = @('name: "Alice"', 'active: true', 'score: 2.5', 'flags: fn:tuple(value: true, value: true, value: true)', 'raw_flags: fn:tuple(value: true, value: true, value: true)') },
    @{ Name = "thread snapshot worker"; File = "examples\thread_snapshot_test.fx"; Query = ""; Expect = @("started: `"started`"", "status: `"finished`"", "result: `"{status: \`"done\`"}`"") }
    @{ Name = "stratified negation as failure"; File = "examples\negation_as_failure.fx"; Query = ""; Expect = @('proved by absence of a contrary fact') }
)

$negativeTests = @(
    @{ Name = "reject undeclared body variable"; File = "examples\invalid\undeclared_body_var.fx"; Query = "? EngineerInSEA(name: Name)"; Expect = "Variable 'e' is used before declaration" },
    @{ Name = "reject member access in head"; File = "examples\invalid\member_access_head.fx"; Query = "? HasManager(name: Name)"; Expect = "Rule head fields cannot use member access" },
    @{ Name = "reject invocation through callable reference"; File = "examples\invalid\double_colon_call.fx"; Query = "? Bad(value: value)"; Expect = "'::' is only valid for a two-part callable reference" },
    @{ Name = "reject immutable reassignment"; File = "examples\invalid\reassign_immutable.fx"; Query = "? Bad(value: value)"; Expect = "already assigned and immutable" },
    @{ Name = "reject method immutable reassignment"; File = "examples\invalid\method_reassign.fx"; Query = "? BadResult(value: value)"; Expect = "already assigned and immutable" },
    @{ Name = "reject unknown extended parent"; File = "examples\invalid\method_unknown_parent.fx"; Query = "? Employee(name: name)"; Expect = "Unknown parent fact/type" },
    @{ Name = "reject multiple inheritance cycle"; File = "examples\invalid\multiple_inheritance_cycle.fx"; Query = ""; Expect = "Inheritance cycle: 'Beta' cannot extend 'Alpha'" },
    @{ Name = "reject ambiguous multiple inheritance field"; File = "examples\invalid\multiple_inheritance_ambiguous_field.fx"; Query = ""; Expect = "Ambiguous inherited field 'domain' for ConflictedFact" },
    @{ Name = "reject unknown call field"; File = "examples\invalid\unknown_call_field.fx"; Query = "? Bad(value: value)"; Expect = "Unknown field 'key' for Employee" },
    @{ Name = "reject unknown head type"; File = "examples\invalid\unknown_head_type.fx"; Query = "? Bad(value: value)"; Expect = "Unknown type annotation 'Name'" },
    @{ Name = "reject stray return after terminator"; File = "examples\invalid\stray_return.fx"; Query = ""; Expect = "'return' is only valid inside a method body" },
    @{ Name = "reject missing source or native module"; File = "examples\invalid\missing_module.fx"; Query = ""; Expect = "Module 'missing_native_or_source_module' not found" },
    @{ Name = "reject dangling else"; File = "examples\invalid\dangling_else.fx"; Query = "? Bad(value: value)"; Expect = "'else' is only valid inside method fallback branches" },
    @{ Name = "reject if without then keyword"; File = "examples\invalid\if_missing_then.fx"; Query = ""; Expect = "Expected 'then' after if condition" },
    @{ Name = "reject implicit inline return"; File = "examples\invalid\implicit_inline_return.fx"; Query = ""; Expect = "Expected comparison operator in goal" },
    @{ Name = "reject unreachable goal after throw"; File = "examples\invalid\unreachable_after_throw.fx"; Query = ""; Expect = "Unreachable goal after throw" },
    @{ Name = "reject string exception handler target"; File = "examples\invalid\throw_string_target.fx"; Query = ""; Expect = "throw target must be a callable reference such as someFunction::Function" },
    @{ Name = "reject exception without kind"; File = "examples\invalid\throw_missing_kind.fx"; Query = ""; Expect = "throw exception must be an object with a string 'kind' field" },
    @{ Name = "reject native type mismatch"; File = "examples\invalid\native_type_mismatch.fx"; Query = ""; Expect = "expects argument 'value' to be string" },
    @{ Name = "reject native reported failure"; File = "examples\invalid\native_abi_failure.fx"; Query = ""; Expect = "Native function 'smoke:fail' failed: expected native failure" },
    @{ Name = "reject native invalid json"; File = "examples\invalid\native_abi_invalid_json.fx"; Query = ""; Expect = "Native function 'smoke:invalidJson' returned invalid JSON" },
    @{ Name = "reject division by zero"; File = "examples\invalid\division_by_zero.fx"; Query = ""; Expect = "Division by zero in constant expression" },
    @{ Name = "reject unbounded non-tail recursion"; File = "examples\invalid\non_tail_recursion_limit.fx"; Query = ""; Expect = "Maximum non-tail method recursion depth reached" },
    @{ Name = "reject string plus concat"; File = "examples\invalid\string_plus_concat.fx"; Query = ""; Expect = "Operator '+' expects numeric operands" },
    @{ Name = "reject system print arity"; File = "examples\invalid\system_print_arity.fx"; Query = ""; Expect = "system:print expects one value" },
    @{ Name = "reject escaped method local"; File = "examples\invalid\local_scope_escape.fx"; Query = ""; Expect = "Variable 'temp' is used before declaration" },
    @{ Name = "reject tuple assignment type mismatch"; File = "examples\invalid\tuple_assignment_type_mismatch.fx"; Query = ""; Expect = "ProgrammingError: tuple assignment target 'active' expects number" },
    @{ Name = "reject tuple assignment arity mismatch"; File = "examples\invalid\tuple_assignment_arity_mismatch.fx"; Query = ""; Expect = "ProgrammingError: tuple assignment expected 2 value(s), got 3" },
    @{ Name = "reject tuple assignment duplicate target"; File = "examples\invalid\tuple_assignment_duplicate_target.fx"; Query = ""; Expect = "Duplicate tuple assignment target 'name'" },
    @{ Name = "reject system.result outside then"; File = "examples\invalid\system_result_outside_pipeline.fx"; Query = ""; Expect = "system.result is only available inside the right side of a then pipeline" },
    @{ Name = "reject system.result assignment"; File = "examples\invalid\system_result_assignment.fx"; Query = ""; Expect = "system.result is read-only" },
    @{ Name = "reject system.result in lambda body"; File = "examples\invalid\system_result_in_lambda.fx"; Query = ""; Expect = "system.result is only available inside the right side of a then pipeline" },
    @{ Name = "reject system.result in method body"; File = "examples\invalid\system_result_in_method_body.fx"; Query = ""; Expect = "system.result is only available inside the right side of a then pipeline" },
    @{ Name = "reject statement-level then"; File = "examples\invalid\then_statement_prefix.fx"; Query = ""; Expect = "Expected expression" },
    @{ Name = "reject dot-then syntax"; File = "examples\invalid\dot_then_pipeline.fx"; Query = ""; Expect = "Unexpected token after statement terminator" },
    @{ Name = "reject deprecated import list without commas"; File = "examples\invalid\import_list_without_commas.fx"; Query = ""; Expect = "Deprecated import list syntax. Use comma-separated imports" },
    @{ Name = "reject imported private operator"; File = "v2_examples\invalid\invalid_private_operator_import.fx"; Query = ""; Expect = "Unknown or inaccessible operator 'secretBlend'" },
    @{ Name = "reject matcher runtime guard"; File = "v2_examples\invalid\invalid_matcher_runtime_guard.fx"; Query = ""; Expect = "@matcher where guards may only compare immutable context or ExpressionRef metadata" },
    @{ Name = "reject unknown annotation method"; File = "v2_examples\invalid\invalid_unknown_annotation.fx"; Query = ""; Expect = "Annotation method 'missingAnnotation' is not declared before 'decorated'" },
    @{ Name = "reject impure operator overload"; File = "v2_examples\invalid\invalid_impure_operator_overload.fx"; Query = ""; Expect = "is not transitively pure" },
    @{ Name = "reject operator call before declaration"; File = "v2_examples\invalid\invalid_operator_call_before_declaration.fx"; Query = ""; Expect = "No overload implementation is registered for operator pattern" },
    @{ Name = "reject factor and factors together"; File = "v2_examples\invalid\invalid_factor_and_factors.fx"; Query = ""; Expect = "cannot use both 'factor' and 'factors'" },
    @{ Name = "reject duplicate Requirement types"; File = "v2_examples\invalid\invalid_duplicate_requirement_types.fx"; Query = ""; Expect = "Duplicate factor type 'BoundaryRequirement'" },
    @{ Name = "reject matcher without RequirementMatch"; File = "v2_examples\invalid\invalid_matcher_wrapper.fx"; Query = ""; Expect = "must return the RequirementMatch wrapper" },
    @{ Name = "reject conflicting imported public operators"; File = "v2_examples\invalid\invalid_conflicting_public_operators.fx"; Query = ""; Expect = "Ambiguous operator syntax at 'conflictMerge'" },
    @{ Name = "reject protected operator overload"; File = "v2_examples\invalid\invalid_protected_operator.fx"; Query = ""; Expect = "Operator '==' is protected and cannot be overloaded" },
    @{ Name = "reject protected logical operator overload"; File = "v2_examples\invalid\invalid_protected_logical_operator.fx"; Query = ""; Expect = "Operator 'and' is protected and cannot be overloaded" },
    @{ Name = "reject operator fixity mismatch"; File = "v2_examples\invalid\invalid_operator_fixity.fx"; Query = ""; Expect = "Operator pattern shape does not match its declared type" },
    @{ Name = "reject missing cardinality one result"; File = "v2_examples\invalid\invalid_operator_cardinality_one.fx"; Query = ""; Expect = "cardinality 'one' requires exactly one result" },
    @{ Name = "reject matcher visibility above pattern"; File = "v2_examples\invalid\invalid_matcher_visibility.fx"; Query = ""; Expect = "Public operator matcher requires public operator syntax" },
    @{ Name = "reject explicit operator method parameters"; File = "v2_examples\invalid\invalid_operator_explicit_parameters.fx"; Query = ""; Expect = "receive captures implicitly and cannot declare parameters" },
    @{ Name = "reject invalid fact algorithm"; File = "examples\invalid\fact_invalid_algorithm.fx"; Query = ""; Expect = "FactConfigError: unsupported algorithm 'approximate_magic' before calling native library" },
    @{ Name = "reject invalid fact threshold"; File = "examples\invalid\fact_invalid_threshold.fx"; Query = ""; Expect = "FactConfigError: 'threshold' must be between 0 and 1 before calling native library" },
    @{ Name = "reject invalid fact analysis required fields"; File = "examples\invalid\fact_analysis_invalid_required_fields.fx"; Query = ""; Expect = "expects argument 'required_fields' to be array" },
    @{ Name = "reject scalar contextual membership"; File = "examples\invalid\relation_scalar_membership.fx"; Query = ""; Expect = "membership method must return a key-value micro-fact or nil" },
    @{ Name = "reject comparison fact truthiness"; File = "examples\invalid\relation_comparison_truthiness.fx"; Query = ""; Expect = "Expected comparison operator after where expression" },
    @{ Name = "reject unstored comparison fact"; File = "examples\invalid\relation_unknown_fact.fx"; Query = ""; Expect = "left fact is not present in the fact store" },
    @{ Name = "reject ambiguous structurally equal fact"; File = "examples\invalid\relation_ambiguous_fact.fx"; Query = ""; Expect = "structurally ambiguous" },
    @{ Name = "reject hard dependency cycle"; File = "examples\invalid\relation_dependency_cycle.fx"; Query = ""; Expect = "detected a hard dependency cycle" },
    @{ Name = "reject untyped relationship metadata"; File = "examples\invalid\relation_invalid_descriptor.fx"; Query = ""; Expect = "Relationship(...) metadata" },
    @{ Name = "reject fact comparison non object"; File = "examples\invalid\fact_compare_non_object.fx"; Query = ""; Expect = "fact-shaped object values" }
    @{ Name = "reject unbound negation variable"; File = "examples\invalid\negation_unbound_variable.fx"; Query = ""; Expect = "must be bound by preceding positive goals" }
    @{ Name = "reject unstratified negative cycle"; File = "examples\invalid\negation_unstratified_cycle.fx"; Query = ""; Expect = "Unstratified negative dependency cycle" }
    @{ Name = "reject reference on unstored receiver"; File = "examples\invalid\reference_unstored_receiver.fx"; Query = ""; Expect = "Cannot evaluate fact field 'by' for references" }
    @{ Name = "reject invalid reference result"; File = "examples\invalid\reference_invalid_result.fx"; Query = ""; Expect = "must return ReferenceResult(result: TypedFact(...))" }
    @{ Name = "reject impure referenced method"; File = "examples\invalid\reference_impure_method.fx"; Query = ""; Expect = "is not pure: uses impure builtin 'system:print'" }
    @{ Name = "reject invalid reasoning grade"; File = "examples\invalid\reasoning_invalid_grade.fx"; Query = ""; Expect = "Reasoning grades and reliability must be finite values between 0 and 1" }
)

$falseTests = @(
    @{ Name = "method call does not implicitly iterate"; File = "examples\method_style.fx"; Query = "? isAdult(name: name)"; Expect = "false" },
    @{ Name = "empty declaration returns no result"; File = "examples\empty_declaration.fx"; Query = "? DocOnly(input: `"x`")"; Expect = "false" },
    @{ Name = "empty brace declaration returns no result"; File = "examples\empty_brace_declaration.fx"; Query = "? DocBrace(input: `"x`")"; Expect = "false" },
    @{ Name = "single positional method call remains valid"; File = "examples\empty_declaration.fx"; Query = "? DocOnly(1)"; Expect = "false" },
    @{ Name = "typed builtin method rejects wrong input"; File = "examples\typed_methods.fx"; Query = "? add(x: `"two`", y: 3, output: value)"; Expect = "false" },
    @{ Name = "fact import keeps ordinary unification exact"; File = "examples\fact_import_keeps_exact_unification.fx"; Query = "? Person(name: `"Alice`")"; Expect = "false" }
)

$directTests = @(
    @{ Name = "direct main execution"; Args = @("examples\direct_main.fx", "one", "two"); Expect = @("count: 3", 'names: ["Ravi", "Sita", "Ramesh"]', 'args: ["one", "two"]') },
    @{ Name = "main executes after complete module publication"; Args = @("examples\module_publication_main.fx"); Expect = @('"complete module published"') },
    @{ Name = "direct imported method call"; Args = @("examples\function_caller.fx"); Expect = @("RemoteRole called with name: Anu, role: Student", 'result: fn:tuple(value: true)') },
    @{ Name = "direct backtracking unification"; Args = @("examples\backtracking_unification.fx"); Expect = @("choice_count: 4", "same_pair_count: 2", "employee_count: 2", "nested_count: 2") },
    @{ Name = "recursive multi-clause method"; Args = @("examples\recursive_ancestor.fx", "? AncestorOf(descendant: descendant, ancestor: ancestor)"); Expect = @('descendant = "kitten", ancestor = "cat"', 'descendant = "kitten", ancestor = "organism"', 'descendant = "cat", ancestor = "organism"') },
    @{ Name = "facts to csv and json"; Args = @("examples\facts_to_csv_json.fx"); Expect = @("row_count: 3", "sea_engineer_count: 2", 'csv_text: "name,role,office', 'SeaEngineer(name: \"Alice\"', 'json_text: "[{\"name\":\"Alice\"', 'parsed_rows: [{name: "Alice"') },
    @{ Name = "fact db create"; Args = @("examples\fact_db_create.fx"); Expect = @("source_count: 4", "inserted_count: 3", 'Customer(name: \"Alice\"', 'Customer(name: \"Dana\"') },
    @{ Name = "fact db update"; Args = @("examples\fact_db_update.fx"); Expect = @("updated_count: 3", "gold_count: 2", 'tier: \"gold\"') },
    @{ Name = "fact db delete"; Args = @("examples\fact_db_delete.fx"); Expect = @("kept_count: 2", "deleted_count: 1", 'reason: "inactive"', 'Customer(name: \"Dana\"') },
    @{ Name = "lazy indexed fact selection"; Args = @("examples\fact_selection.fx"); Expect = @('selection: {__type: "FactSelection"', "count: 1", "set_count: 1", 'name: "India"') },
    @{ Name = "fact selection snapshot release"; Args = @("examples\fact_selection_release.fx"); Expect = @("count: 1", "released: true") },
    @{ Name = "unified directional fact comparison"; Args = @("examples\expert_system_acceptance.fx"); Expect = @('mammal: Comparison(', 'state: "animal-interpreted"', 'judgment: "target-owned"', 'relationshipCount: 1', 'matchedAncestor: "Mammal"', 'ancestorDistance: 1', 'ancestorSimilarity: 0.8', 'previousState: "animal-interpreted"', 'pipelined: Comparison(', 'resolved_fields: Comparison(', 'state: "unresolved"', 'reason: "required dependency is not satisfied"', 'state: "exact-member"') },
    @{ Name = "fact comparison core contract"; Args = @("examples\fact_comparison_contract.fx"); Expect = @('state: "exact-member"', 'state: "relationship-interpreted"', 'relationshipCount: 1', 'name: "compatible"', 'degree: 0.8', 'confidence: 0.9', 'state: "incomparable"', 'state: "unresolved"', 'reason: "required dependency is not satisfied"', 'state: "partial-member"', 'conflictingFields: ["label"]', 'dependency_satisfied: true') },
    @{ Name = "fact comparison normal method resolution"; Args = @("examples\fact_comparison_method_resolution.fx"); Expect = @('state: "exact-member"', 'membership: {category: "base"}', 'matchedAncestor: nil') },
    @{ Name = "fact comparison built-in normal chain membership"; Args = @("examples\fact_comparison_default_chain.fx"); Expect = @('state: "default-chain-interpreted"', 'previousState: "exact-member"', 'previousFamily: "FirstTarget"', 'previousMembership: {category: "base"}') },
    @{ Name = "fact comparison stable identity and aliases"; Args = @("examples\fact_comparison_identity.fx"); Expect = @("first_count: 0", "second_count: 1", 'state: "identity-aware"') },
    @{ Name = "in-memory expert intelligence system"; Args = @("examples\expert_intelligence_system.fx"); Expect = @('=== Loan policy decision report ===', 'Applicant: Ava', 'Decision: approved', 'Rule: high_credit_with_verified_income', 'Applicant: Mira', 'Decision: unresolved', 'Reason: required dependency is not satisfied', 'Applicant: Arun', 'Decision: declined', 'Rule: low_credit', 'decision report complete') },
    @{ Name = "dynamic fact references"; Args = @("examples\dynamic_fact_references.fx"); Expect = @('ReferenceResult(', 'Velocity(value: 5)', 'WaveProjection(value: 20)', 'velocity_at_five: [ReferenceResult(result: Velocity(value: 2)') },
    @{ Name = "nested fact database transaction"; Args = @("examples\db_workflow\query.fx"); Expect = @("region_count: 4", 'name: "Mira"', 'region: {name: "south", region_id: 21}', 'name: "Cup Cake"', "quantity: 2", 'name: "Chocolate Pudding"', "quantity: 3", "total: 26", 'status: "created"') },
    @{ Name = "multi-model fact database"; Args = @("examples\db_workflow\multi_model_database.fx"); Expect = @('models: ["cart", "product", "shop"]', 'model: "product"', 'model: "cart"', "product_count: 1", "cart_count: 1", "shop_count: 1", 'product(', 'price: 6', 'cart(', 'quantity: 2', 'shop(', 'South Bakery') },
    @{ Name = "pizza delivery separate and shared databases"; Args = @("examples\db_workflow\pizza_delivery.fx"); Expect = @('matched: 1, modified: 1', 'status: "delivered"', 'matched: 1, modified: 0', 'kind: "DeliveredOrderCannotBeCancelled"', 'user_count: 1', 'region_count: 1', 'product_count: 2', 'order_count: 3', 'corrected_request: {quantity: 1, recovered: true', 'kind: "InvalidPizzaQuantity"', 'status: "resubmitted"', 'status: "preparing"', 'total: 36', 'status: "cancelled"', 'cancellationReason: "customer_changed_mind"') },
    @{ Name = "connection database identity and stale-write safety"; Args = @("examples\db_workflow\db_connection_safety.fx"); Expect = @('inserted: {matched: 0, modified: 0, inserted: 1', 'duplicate: {matched: 1, modified: 0, inserted: 0', 'kind: "DuplicateKey"', 'source: "db"', 'handled_library_exception: "DuplicateKey"', 'updated: {matched: 1, modified: 1', 'stale_update: {matched: 0, modified: 0', 'kind: "NotFound"', 'name: "original"', 'status: "confirmed"', 'preserved: "yes"') },
    @{ Name = "csv to generated felidae facts"; Args = @("examples\csv_school.fx"); Expect = @('export: "ok"', '__type: "School"', 'student: "John"', 'class: "10c"', 'student: "Maya"') },
    @{ Name = "query generated csv facts"; Args = @("examples\data\converted_csv_school.fx", '? School(student: student, subject: subject)'); Expect = @('student = "John"', 'subject = "physics"') },
    @{ Name = "file operations reusable test"; Args = @("examples\file_operations_test.fx"); Expect = @('write: "ok"', 'append: "ok"', 'lines: ["alpha", "beta", "gamma"]', 'text: "alpha\nbeta\ngamma\n"') },
    @{ Name = "streaming file reader smoke"; Args = @("examples\large_file_stream_test.fx"); Expect = @('write: "ok"', 'line_count: 5', 'third: "gamma"', 'has_epsilon: "true"') },
    @{ Name = "stdlib utilities"; Args = @("examples\stdlib_utilities.fx"); Expect = @('trimmed: "Alice,Engineer,SEA"', 'parts: ["Alice", "Engineer", "SEA"]', 'normalized: "Alice,Engineer,NYC"', 'has_engineer: "true"', 'starts_alice: "true"', 'ends_sea: "true"', 'has_office: "true"', 'office: "SEA"', 'sea_count: 2', 'deleted_count: 2', 'write_status: "ok"', 'second_line: "Engineer"', 'line_count: 3') },
    @{ Name = "insertion sort with typed exception handling"; Args = @("examples\insertion_sort.fx"); Expect = @('sorted: {ok: true, data: [1, 3, 3, 5, 7, 9], error: nil}', 'rejected: {ok: false, data: [], error: {__type: "Exception"', 'kind: "SortLimitExceeded"', 'source: "insertion_sort"') },
    @{ Name = "standard linear and binary search"; Args = @("v2_examples\standard_search_algorithms.fx"); Expect = @("linear_last: 63", "binary_last: 63", "missing: {linear: -1, binary: -1}") },
    @{ Name = "v2 operator expression inventory"; Args = @("v2_examples\operator_expression_inventory.fx"); Expect = @("precedence: 14", "grouped: 20", "unary: -7", "ordered: true", "unequal: true", "eligible: true", 'member: "Ava"', "mapped: [13, 23, 33]", "pipeline: 28") },
    @{ Name = "v2 custom operator overload"; Args = @("v2_examples\custom_operator_overload.fx"); Expect = @("combined: 5", "chained: 7", "next_chained: 100") },
    @{ Name = "v2 typed builtin operator overload"; Args = @("v2_examples\typed_builtin_operator_overload.fx"); Expect = @('Element(name: "combined", x: 7, y: 10)', "numeric: 5") },
    @{ Name = "v2 mixfix operator overload"; Args = @("v2_examples\mixfix_operator_overload.fx"); Expect = "{result: 14, nested: 8}" },
    @{ Name = "v2 prefix Requirement operator composition"; Args = @("v2_examples\prefix_requirement_operator.fx"); Expect = "7" },
    @{ Name = "v2 leading mixfix operator"; Args = @("v2_examples\leading_mixfix_operator.fx"); Expect = "5" },
    @{ Name = "v2 symbolic operator overload"; Args = @("v2_examples\symbolic_operator_overload.fx"); Expect = "9" },
    @{ Name = "v2 ordering operator overload"; Args = @("v2_examples\ordering_operator_overload.fx"); Expect = @("custom: true", "builtin: false") },
    @{ Name = "v2 operator cardinality"; Args = @("v2_examples\operator_cardinality.fx"); Expect = @("many: [1, 2]", "some: 7", "none: nil") },
    @{ Name = "v2 logical operator expressions"; Args = @("v2_examples\logical_operator_expression.fx"); Expect = @("conjunction: true", "disjunction: true", "short_and: false", "short_or: true") },
    @{ Name = "v2 animal fact similarity evidence"; Args = @("v2_examples\animal_fact_similarity_evidence.fx"); Expect = @('evidence: [AnimalSimilarityEvidence(', 'left_type: "TigerFemale"', 'right_type: "CatFemale"', 'common_ancestor: "Mammal"', 'score: 0.612372435695794', 'score: 0.433012701892219', 'common_ancestor: "Tiger"', 'ancestor_similarity: 0.5', 'ancestor_similarity: 0.75', 'property_similarity: 0.75', 'matched_properties: ["legs", "warm_blooded", "diet"', 'differing_properties: ["species", "habitat", "sex", "produces_milk", "nurtures_young"]', 'ancestor_evidence: [AncestorComparisonEvidence(', 'ancestor: "Mammal"', 'ancestor: "Animal"', 'property_evidence: [PropertyComparisonEvidence(', 'status: "matched"', 'status: "conflicting"', 'decisions: {female_tiger_to_female_cat: true', 'male_tiger_to_female_cat: false', 'male_tiger_to_male_cat: true', 'male_tiger_to_female_tiger: true') },
    @{ Name = "v2 fact similarity requires common ancestry"; Args = @("v2_examples\fact_similarity_requires_ancestry.fx"); Expect = '{property_similarity: 1, ancestor_similarity: 0, similarity: 0, common_ancestor: nil}' },
    @{ Name = "v2 contextual fact intelligence"; Args = @("v2_examples\contextual_fact_intelligence.fx"); Expect = @('felidae: ContextualAnswer(query: "felidae", state: "resolved", learned: true, confidence: 1, crisp: true', 'unlearned: ContextualAnswer(query: "quantum", state: "unknown", learned: false, confidence: 0, crisp: false', 'ambiguous_bank: ContextualAnswer(query: "bank", state: "ambiguous", learned: true, confidence: 0.25, crisp: false', 'financial_bank: ContextualAnswer(query: "bank", state: "context-resolved"', 'article: "a"', 'answer: FinancialBank(', 'sony_legs: QuantityAnswer(query: "how-many", state: "resolved", learned: true, confidence: 1, crisp: true', 'subject: SonyKnowledge(', 'property: "legs", value: 4', 'evidence: [ResolutionEvidence(') },
    @{ Name = "v2 AST typed methods and postfix operator"; Args = @("v2_examples\ast_typed_methods.fx"); Expect = @('function_call: "call"', 'logical: "logical"', 'arithmetic: "arithmetic"', 'postfix_logical: "logical"', 'annotation: "decorated"') },
    @{ Name = "v2 imported public operator"; Args = @("v2_examples\imported_public_operator.fx"); Expect = "{blended: 12, difference: 10}" },
    @{ Name = "v2 private operator specializes public operator"; Args = @("v2_examples\private_operator_specialization.fx"); Expect = '"private"' },
    @{ Name = "v2 operator type specificity"; Args = @("v2_examples\operator_type_specificity.fx"); Expect = @('exact: "child"', 'inherited: "parent"') },
    @{ Name = "v2 user-defined operator capture hierarchy"; Args = @("v2_examples\user_defined_operator_types.fx"); Expect = @('"Ava"', '"Noah"', '"Mia"') },
    @{ Name = "v2 Requirement matcher operator"; Args = @("v2_examples\requirement_matcher_operator.fx"); Expect = @('SimilarityResult(', 'sameX: true') },
    @{ Name = "v2 Requirement matcher metadata guard"; Args = @("v2_examples\requirement_matcher_guard.fx"); Expect = "5" },
    @{ Name = "v2 Requirement hierarchy specificity"; Args = @("v2_examples\requirement_hierarchy_specificity.fx"); Expect = '"exact"' },
    @{ Name = "v2 user method annotation"; Args = @("v2_examples\method_annotation.fx"); Expect = '"annotation-applied"' },
    @{ Name = "v2 threaded custom operator"; Args = @("v2_examples\thread_custom_operator.fx"); Expect = '{started: "started", result: "42"}' },
    @{ Name = "v2 method fact binding value"; Args = @("v2_examples\method_fact_binding_value.fx"); Expect = '"name: Anu, role: Student"' },
    @{ Name = "explicit logical transformations"; Args = @("examples\logical_transformations.fx"); Expect = @('transformation: "converse"', 'transformation: "contrapositive"', 'Negation(', 'ContradictionEvidence(') },
    @{ Name = "fact set-theory inclusion exclusion proof"; Args = @("examples\fact_set_theory_proof.fx"); Expect = @('union_cardinality: 4', 'right_hand_side: 4', 'proved: true', 'v2_in_intersection: true', 'a_is_subset_of_union: true', 'union_is_superset_of_a: true') },
    @{ Name = "generic nested subfact analysis"; Args = @("examples\nested_subfact_analysis.fx"); Expect = @('found: {found: true, depth: 4, path: ["regions", "[1]", "members", "[0]"]}', 'missing: {found: false, depth: nil, path: []}', 'bounded: {found: false, depth: nil, path: []}') },
    @{ Name = "nested typed fact neighbours"; Args = @("examples\nested_fact_neighbors.fx"); Expect = @('nearest: {count: 3', '__type: "Person", name: "Ravi"', '__type: "Profile", region: "south"', '__type: "Person", name: "Leela"', 'direct: {count: 2') },
    @{ Name = "multiple inheritance hierarchy selection"; Args = @("examples\multiple_inheritance_hierarchy.fx"); Expect = @('government_count: 3', 'people_count: 3', 'politician_count: 2', 'politician_public_domain: "public"', 'politician_people_category: "being"', 'Politician(name: "Ravi"', 'Policeman(service: "community"') },
    @{ Name = "multiple inheritance fact analysis"; Args = @("examples\multiple_inheritance_fact_analysis.fx"); Expect = @('people_is_ancestor: {matched: "true"', 'government_is_ancestor: {matched: "true"', 'people_is_direct_parent: {matched: "true"') },
    @{ Name = "fact relation symmetry and asymmetry"; Args = @("examples\fact_relation_properties.fx"); Expect = @('mutual_respect: {pair_count: 2, node_count: 2, reflexive: false, symmetric: true, asymmetric: false, transitive: false}', 'hierarchy: {pair_count: 3, node_count: 3, reflexive: false, symmetric: false, asymmetric: true, transitive: true}') },
    @{ Name = "input plus knowledge fact derivation"; Args = @("examples\input_knowledge_fact_derivation.fx"); Expect = @('EligibilityDecision(', 'applicant: Applicant(name: "Ravi", region: "south")', 'status: "approved"', 'score: 82', 'policy: "standard"', 'evidence_count: 3', 'rationale: "score satisfies stored policy"') },
    @{ Name = "native set and finite group packages"; Args = @("v2_examples\set_group_native.fx"); Expect = @("union_count: 5", "common_taste_count: 4", "fruit_only_taste_count: 1", "exact_disjoint: true", "has_sweet: true", "same_tastes: true", "fruit_subset: true", "valid: true", "closure: true", "associative: true", "identity: true", "inverse: true", "abelian: true", "commutative: true") },
    @{ Name = "web server reusable integration test"; Args = @("examples\web_server_test.fx"); Expect = @('ready: "ok"', 'get: "Hello World"', 'post: "Hello World"', 'put: "Hello World"', 'delete: "Hello World"') },
    @{ Name = "fx self analysis line classifier"; Args = @("examples\fx_self_analysis.fx"); Expect = @('line_count: 23', 'http_call_count: 1', 'missing_language_primitives:', 'string.split', 'method body builtin output binding') },
    @{ Name = "data structure stress"; Args = @("examples\data_structure_stress.fx"); Expect = @('count: 2', 'generated: "ok"', 'name: "Alice"', 'known_gap: "expression array:get is not evaluated yet"') },
    @{ Name = "arithmetic and boolean methods"; Args = @("examples\arithmetic_and_boolean.fx"); Expect = @('first_has_manager: fn:tuple(value: true, value: true)', 'add: 5', 'sub: 6', 'mul: 42', 'div: 4', 'precedence: 14', 'grouped: 20', 'negativeLiteral: -2.5', 'negativeProduct: -6', 'negativeGrouped: -5') },
    @{ Name = "neuron negative numbers"; Args = @("examples\my_nuron.fx"); Expect = @('input: "sample-a"', 'raw: 236.39', 'activatedInput: [3.2, 236.39]', 'finalOutput: [244.79, 115.995]', 'negativeLiteral: -200') },
    @{ Name = "native ABI smoke"; Args = @("examples\native_module_smoke.fx"); Expect = @('"native module loaded"') },
    @{ Name = "native ABI in thread"; Args = @("examples\native_thread_smoke.fx"); Expect = @('started: "started"', 'thread native ok') },
    @{ Name = "native ABI expression"; Args = @("examples\native_abi_success.fx"); Expect = @('expression: "expression ok"') },
    @{ Name = "colon dot interchangeable access"; Args = @("examples\access_interchange.fx"); Expect = @("dot: 42", "colon: 42", "mixed: 42", "direct: 42", "checked: {dot: 42, colon: 42, mixed: 42}") },
    @{ Name = "method false tuple value"; Args = @("examples\invalid\method_value_no_result.fx"); Expect = @('result: fn:tuple(value: false)') },
    @{ Name = "native stdlib execution"; Args = @("examples\native_stdlib.fx"); Expect = @("writeStatus: `"ok`"", "readBack: `"Felidae IO`"", "exists: true", "root: 9", "powered: 256", "activation: 0.5", "dot: 32", "mse: 1.33333333333333") },
    @{ Name = "native WordNet semantic algorithms"; Args = @("examples\wordnet_native_demo.fx"); Expect = @('synset: "cat.n.01"', 'pathSimilarity: {score: 0.333333', 'wuPalmer: {score: 0.666667', 'resnik: {score:', 'factSimilarity: {score: 0.333333', 'score: 4', 'text: "gato"') },
    @{ Name = "native WordNet command-line query"; Args = @("examples\wordnet_native_demo.fx", "? WordNetCliSmoke(result: Result)"); Expect = @('Result = {score: 0.333333', 'lcs: "animal.n.01"') },
    @{ Name = "native fact semantic phase 1"; Args = @("examples\fact_semantic_phase1.fx"); Expect = @('projection: {kind: "fact"', 'node_count:', 'similarity: {score:', 'algorithm: "semantic_recursive"', 'lexical_algorithm: "wu_palmer"', 'difference: {mode: "property_exact"', 'near: {matched:', 'wordnet_internal_service_pending_for_phase_2') },
    @{ Name = "fact semantic unify is explicit"; Args = @("examples\fact_import_keeps_exact_unification.fx"); Expect = @('semantic: {unified:', 'threshold:', 'wordnet_internal_service_pending_for_phase_2') },
    @{ Name = "minimal fact similarity"; Args = @("examples\fact_similarity_minimal.fx"); Expect = @('similar: {score:', 'lexical_algorithm: "leacock_chodorow"', 'propertyMatch: {score:', 'difference: {mode: "property_exact"', 'nearest: {count: 1', 'name: "Ravi"') },
    @{ Name = "fact reasoning and analysis phase 2"; Args = @("examples\fact_reasoning_phase2.fx"); Expect = @('propertyComparison: {score:', 'mode: "property_exact"', 'semanticSimilarity: {score:', 'lexical_algorithm: "leacock_chodorow"', 'common: {ancestor_type: "Person"', 'path: {reachable:', 'distance: 2', 'evidence: DerivationResult(', 'truth_status: "unknown"', 'fuzzy_degree: 0.8', 'profile: ReasoningProfile(', 'nearest: {count: 1', 'next: {prediction: {__type: "ClimatePrediction"', 'method: "numeric_gini_stump"', 'split: {feature: "humidity"', 'model_fx: "DecisionTreeModel(', 'applied: {prediction: "rainy"', 'decision_route: ["numeric_split_right"]', 'evaluated: {model_type: "decision_tree"', 'accuracy: 1', 'savedModel: "ok"') },
    @{ Name = "fact data science boundaries"; Args = @("examples\fact_data_science_boundaries.fx"); Expect = @('floatArithmetic: {sum: 0.3', 'numericStats: {meanWeight:', 'profile: {method: "numeric_fact_profile"', 'clusters: {model_type: "k_means"', 'scaling: "standardized"', 'classified: {prediction: "livestock"', 'predictedWeight: {prediction:', 'goatSheep: {score:', 'method: "fact_property_ancestor"', 'property_score:', 'common_ancestor: "Ruminant"', 'goatWolf: {score:', 'common_ancestor: "CommonFact"', 'nearestLivestock: {count: 2', 'rejected_count: 1', 'generatedSynset: {__type: "RuminantSynset"', 'ancestor: "Ruminant"') },
    @{ Name = "fact comparison scenarios"; Args = @("examples\fact_comparison_scenarios.fx"); Expect = @('employeeSibling: {score:', 'method: "fact_property_ancestor"', 'common_ancestor: "Employee"', 'employeeContractor: {score:', 'common_ancestor: "CommonFact"', 'sensorSibling: {score:', 'common_ancestor: "Sensor"', 'sauceSibling: {score:', 'common_ancestor: "Sauce"', 'personnelRanking: {count: 2', 'rejected_count: 1', 'EmployeeCapabilitySynset', 'SauceFlavorSynset', 'scenarioConfidence:') },
    @{ Name = "fact comparison edge cases"; Args = @("examples\fact_comparison_edge_cases.fx"); Expect = @('propertyNoAncestor: {score:', 'common_ancestor: "CommonFact"', 'ancestorPoorProperties: {score:', 'common_ancestor: "Sensor"', 'missingFieldComparison: {score:', 'exactPropertyComparison: {score:', 'constrained: {count: 2', 'matched_candidate_count: 2', 'rejected_count: 2') },
    @{ Name = "fact graph wordnet style algorithms"; Args = @("examples\fact_graph_wordnet_algorithms.fx"); Expect = @('direct: {matched: "true"', 'ancestors: {fact: "Goat"', 'facts: ["Ruminant", "Mammal", "Animal", "LivingThing"]', 'descendants: {fact: "Mammal"', 'lowestCommonAncestor: {ancestor_type: "Ruminant"', 'shortestPath: {reachable: "true", distance: 4', 'pathSimilarity: {score: 0.2', 'algorithm: "path"', 'wuPalmer: {score: 0.8', 'algorithm: "wu_palmer"', 'resnik: {score:', 'algorithm: "resnik"', 'lin: {score:', 'algorithm: "lin"', 'frequencyStats: {method: "fact_frequency_statistics"', 'normalized: {input: "Black Sheep", normalized: "black_sheep"', 'clusters: {model_type: "k_means"', 'classification: {goatGraphScore: {score: 0.8') },
    @{ Name = "advanced mortality fact reasoning"; Args = @("examples\advanced_mortality_fact_reasoning.fx"); Expect = @('=== Mortality reasoning report ===', '--- Ancestry proof ---', 'Socrates', '--- Similarity proof ---', 'Ram', 'Property similarity score', 'Hierarchy similarity score', '--- Negative comparison ---', '--- Declared relationships ---', '--- Shared hierarchy ---', 'mortality report complete') },
    @{ Name = "plot gtk qt visualization bridge"; Args = @("examples\fact_plot_visualization.fx"); Expect = @('animalFacts: [{__type: "AnimalMetric"', 'scatter: {__type: "Plot", kind: "scatter"', 'dashboard: {__type: "PlotDashboard"', 'gtkPlot: {__type: "gtk.plot"', 'gtkRender: {__type: "gtk.render"', 'qtPlot: {__type: "qt.plot"', 'qtRender: {__type: "qt.render"', 'gtkSaved: "ok"', 'qtSaved: "ok"') },
    @{ Name = "gtk fact graphics"; Args = @("examples\gtk_fact_graphics.fx"); Expect = @('__type: "gtk.h1"', '__type: "gtk.button"', '__type: "gtk.radio"', '__type: "gtk.checkbox"', '__type: "gtk.pieSectors"', 'count: 4', 'name: "North"', '__type: "gtk.render"', 'saved: "ok"') },
    @{ Name = "qt fact graphics"; Args = @("examples\qt_fact_graphics.fx"); Expect = @('__type: "qt.h1"', '__type: "qt.circle"', '__type: "qt.pieSectors"', 'name: "CPU"', '__type: "qt.render"', 'pieRender: {__type: "qt.render"', 'saved: "ok"') },
    @{ Name = "gtk complex shapes graph"; Args = @("examples\gtk_complex_shapes_graph.fx"); Expect = @('__type: "gtk.h1"', '__type: "gtk.line"', '__type: "gtk.circle"', 'Complex fact-shaped renderer', 'Shapes and graph edges are generated from Felidae facts.', '__type: "gtk.render"', 'saved: "ok"') },
    @{ Name = "qt fact relationship graph"; Args = @("examples\qt_fact_relationship_graph.fx"); Expect = @('__type: "qt.graph"', 'node_count: 5', 'edge_count: 5', 'name: "Felidae->Facts"', 'content: "stores"', 'Fact relationship graph', '__type: "qt.render"', 'saved: "ok"') },
    @{ Name = "ml fact mining"; Args = @("examples\ml_fact_mining.fx"); Expect = @('profile: {method: "numeric_fact_profile"', 'spendVisitCorrelation: {method: "pearson_fact_correlation"', 'clusters: {model_type: "k_means"', 'method: "k_means_numeric_facts"', 'trained_count: 4', 'associations: {method: "key_value_association_mining"', 'region=south', 'segment=starter', 'model: {model_type: "decision_tree"', 'spendModel: {model_type: "linear_regression"', 'split: {feature: "age"', 'predicted: {prediction: "premium"', 'predictedSpend: {prediction:', 'factPrediction: {prediction: {prediction: "starter"', 'numericPrediction: {prediction: {prediction:', 'savedModel: "ok"', 'savedRegression: "ok"') },
    @{ Name = "ml sensor fact mining"; Args = @("examples\ml_sensor_fact_mining.fx"); Expect = @('profile: {method: "numeric_fact_profile"', 'clusters: {model_type: "k_means"', 'correlation: {method: "pearson_fact_correlation"', 'stateModel: {model_type: "decision_tree"', 'split: {feature: "vibration"', 'newReading: {prediction: "risk"') },
    @{ Name = "ml sales forecast"; Args = @("examples\ml_sales_forecast.fx"); Expect = @('profile: {method: "numeric_fact_profile"', 'visitorRevenue: {method: "pearson_fact_correlation"', 'revenueModel: {model_type: "linear_regression"', 'feature: "visitors"', 'forecast: {prediction:', 'generated: {prediction: {prediction:', 'saved: "ok"') },
    @{ Name = "cooking expert system"; Args = @("examples\cooking_expert_system.fx"); Expect = @('proposal: {request:', 'predictedDishType: {prediction: "starter"', 'rankedIngredients: {count: 1', 'matched_candidate_count: 1', 'rejected_count: 7', 'required_fields: ["category", "role"]', 'name: "Tomato"', 'important_differences:', 'ingredientFitnessExample: {ingredient: "Tomato"', 'ingredientClusters: {model_type: "k_means"', 'ingredientProfile: {method: "numeric_fact_profile"', 'dishAssociations: {method: "key_value_association_mining"', 'dishType=starter', 'evidence: DerivationResult(', 'profile: ReasoningProfile(', 'truth_status: "unknown"', 'justification: "The proposal keeps similarity, prediction confidence, and graded evidence explicit and auditable."') },
    @{ Name = "cache import thread stress"; Args = @("examples\cache_thread_import_stress.fx"); Expect = @('start1: "started"', 'start2: "started"', 'start3: "started"', 'count: 12', 'Eve', 'Engineer') },
    @{ Name = "then pipeline direct execution"; Args = @("examples\then_pipeline.fx"); Expect = @('result: 4', 'direct: {seen: 4, tag: "wrapped"}', 'returned: 8', 'compactReturned: 10', 'methodThen: 10', 'methodIfThen: 12', 'methodElseThen: 1', 'nested: {seen: 13, tag: "wrapped"}', 'stopped: nil', 'arithmeticPrecedence: 10', 'conditional: "condition-ok"') },
    @{ Name = "then pipeline command line query"; Args = @("examples\then_pipeline.fx", "? Increment(value: 1) then Double(value: system.result) == 4"); Expect = @("true") },
    @{ Name = "auto system print"; Args = @("examples\system_print.fx"); Expect = @("Felidae system running!", "{}") },
    @{ Name = "system printf formatting"; Args = @("examples\printf_formatting.fx"); Expect = @("Hello Felidae", "Decision: Ava is approved", "Score: 0.92", '"printf complete"') },
    @{ Name = "four-valued exact and graded reasoning"; Args = @("examples\four_valued_reasoning.fx"); Expect = @('truth_status: "both"', 'truth_status: "proved"', 'truth_status: "disproved"', 'truth_status: "unknown"', 'fuzzy_degree: 0.72', 'opposition_degree: 0.45') },
    @{ Name = "tabled recursive contradiction reasoning"; Args = @("examples\tabled_exact_reasoning.fx"); Expect = @('truth_status: "both"', 'supporting_rules: ["Reachable"]', 'opposing_rules: ["NotReachable"]', 'fuzzy_degree: 0.736', 'opposition_degree: 0.675') },
    @{ Name = "main returns status value"; Args = @("examples\main_comment_return.fx"); Expect = @("Felidae system running!", '"ok"') },
    @{ Name = "direct no main"; Args = @("examples\family.fx"); Expect = @("Program loaded successfully. No main() method found.", "Use a query argument, add a zero-argument entry call, or run with --repl.") },
    @{ Name = "help"; Args = @("--help"); Expect = @("Felidae Logic Programming Language v0.1.0", ".fx", "Total commands supported: 7", "felidae --repl examples/main.fx", "functional logic language", "______") },
    @{ Name = "debug flag"; Args = @("examples\system_print.fx", "--debug"); Expect = @("Felidae debug mode enabled", "Felidae system running!", "{}") },
    @{ Name = "version"; Args = @("--version"); Expect = @("Felidae Logic Programming Language v0.1.0") }
)

$debugCheckTests = @(
    @{ Name = "debug AST check accepts native declaration import"; Args = @("examples\native_module_smoke.fx", "--check"); Expect = @("FELIDAE_CHECK_OK") },
    @{ Name = "debug AST warns about discarded raw expression"; Args = @("examples\warnings\discarded_expression.fx", "--check"); Expect = @("severity=warning", "Raw expression '10 == 1'", "has no result consumer") },
    @{ Name = "debug exposes prefix operator metadata"; Args = @("v2_examples\prefix_requirement_operator.fx", "--operators-json"); Expect = @('"protocol":"felidae.operator-metadata"', '"fixity":"prefix"', '"method":"incrementRequirement"') },
    @{ Name = "debug resolves imported operator metadata"; Args = @("v2_examples\imported_public_operator.fx", "--operators-json", "--load-imports"); Expect = @('"pattern":"{left} blend {right}"', '"visibility":"public"', '"method":"blendNumbers"') }
)

$celidaeTests = @(
    # Asserted against the structured "metrics" object rather than the human-
    # readable "detail" prose. Celidae emits both; detail is a tooltip string
    # whose wording is free to change, while metrics is the contract every
    # renderer reads. These used to match on detail, so rewording a tooltip
    # broke the test while an actually-wrong record count would not have.
    # --json/--inspect-graph/--type are retired: Celidae's only output now is
    # HTML, with each view's JSON embedded verbatim in a
    # <script type="application/json" id="data-<view>"> element.
    # --template=<name> narrows a run to one view, and the same substrings
    # these tests always checked for still appear inside that element -
    # nothing here changes except how the view is selected.
    @{ Name = "celidae profiles country fact db"; Args = @("examples\data\converted_csv_country.fx", "--template=schema"); Expect = @('"label":"Country","kind":"fact"', '"records":249', '"fields":3', '"coverage":100', '"missing":0') },
    # The ER view is entities and the relationships between them. It used to
    # emit a node per field as well, which made it the schema view under a
    # different name on any program without `extend` - so `"kind":"field"` is
    # no longer expected here, and its absence is asserted by the schema test
    # below still requiring it. What this test has always been for - that the
    # ER view carries no methods or globals - is unchanged.
    @{ Name = "celidae ER diagram excludes execution nodes"; Args = @("examples\data\converted_csv_country.fx", "--template=er"); Expect = @('"mode":"er"', '"label":"Country","kind":"fact"', '"keys=alpha_2, country_code, name'); Reject = @('"kind":"method"', '"kind":"global"', '"kind":"field"') },
    @{ Name = "celidae schema diagram still declares every field"; Args = @("examples\data\converted_csv_country.fx", "--template=schema"); Expect = @('"mode":"schema"', '"kind":"field"', '"label":"alpha_2"') },
    @{ Name = "celidae ER diagram preserves every direct parent"; Args = @("examples\multiple_inheritance_hierarchy.fx", "--template=er"); Expect = @('"from":"fact:Politician","to":"fact:Government","label":"extends"', '"from":"fact:Politician","to":"fact:People","label":"extends"') },
    @{ Name = "celidae dependency graph excludes ER fields"; Args = @("examples\country_query.fx", "--template=graph", "--load-imports"); Expect = @('"mode":"graph"', '"label":"IndiaCountry","kind":"method"', '"kind":"library"') },
    @{ Name = "celidae expert graph records fact attachments"; Args = @("examples\expert_intelligence_system.fx", "--template=graph"); Expect = @('"label":"Fact:depends","kind":"library"', '"label":"Fact:relate","kind":"library"', '"label":"attaches dependency"', '"label":"attaches relationship"') },
    @{ Name = "celidae graph records dynamic reference callables"; Args = @("examples\dynamic_fact_references.fx", "--template=graph"); Expect = @('"label":"Fact:references","kind":"library"', '"label":"Physics:velocity","kind":"method"', '"label":"references"') },
    @{ Name = "celidae viewer loads imported country schema"; Args = @("examples\country_query.fx", "--template=schema", "--load-imports"); Expect = @('"mode":"schema"', '"label":"Country","kind":"fact"', '"records":249', '"truncated":false') },
    @{ Name = "celidae viewer html loads imported country fact db"; Args = @("examples\country_query.fx", "--visualize-data-html", "--load-imports"); Expect = @("<!doctype html>", "Celidae Visualizer", '"label":"Country","kind":"fact"', '"records":249') }
)

$directInputTests = @(
    @{ Name = "console input number true branch"; File = "examples\console_input_branch.fx"; Input = "10`n"; Expect = @('enter x:', 'x: 10', 'matched: true', 'message: "x is ten"') },
    @{ Name = "console input number false branch"; File = "examples\console_input_branch.fx"; Input = "7`n"; Expect = @('enter x:', 'x: 7', 'matched: false', 'message: "x is not ten"') }
)

$replTests = @(
    @{ Name = "repl query global builtin"; File = "examples\direct_main.fx"; Input = "help`nversion`nAdults`ncount(Adults)`n? Person(name: x)`nexit`n"; Expect = @("REPL commands:", "Felidae Logic Programming Language v0.1.0", "Ravi", "1", 'x = "Default"', 'x = "Ravi"', 'x = "Anu"') }
)

if ($ReadOnly) {
    # Database/export examples intentionally mutate source-backed fixtures.
    # Keep a deterministic read-only regression mode for dirty worktrees and
    # CI review jobs that must not rewrite user data.
    $directTests = @($directTests | Where-Object {
        $_.Name -notmatch 'database|fact db|pizza|connection|csv|file operations|streaming file|stdlib utilities'
    })
}

$failed = 0

foreach ($test in $tests) {
    $output = & $Exe $test.File $test.Query 2>&1
    $text = ($output | Out-String).Trim()
    $ok = $LASTEXITCODE -eq 0

    foreach ($expected in $test.Expect) {
        if (-not $text.Contains($expected)) {
            $ok = $false
            break
        }
    }

    if ($ok) {
        Write-Host "[PASS] $($test.Name)"
    } else {
        $failed++
        Write-Host "[FAIL] $($test.Name)"
        Write-Host "  File:  $($test.File)"
        Write-Host "  Query: $($test.Query)"
        Write-Host "  Exit:  $LASTEXITCODE"
        Write-Host "  Output:"
        Write-Host $text
    }
}

foreach ($test in $negativeTests) {
    $output = & $Exe $test.File $test.Query 2>&1
    $text = ($output | Out-String).Trim()
    $ok = $LASTEXITCODE -ne 0 -and $text -like "*$($test.Expect)*"

    if ($ok) {
        Write-Host "[PASS] $($test.Name)"
    } else {
        $failed++
        Write-Host "[FAIL] $($test.Name)"
        Write-Host "  File:  $($test.File)"
        Write-Host "  Query: $($test.Query)"
        Write-Host "  Exit:  $LASTEXITCODE"
        Write-Host "  Output:"
        Write-Host $text
    }
}

foreach ($test in $falseTests) {
    $output = & $Exe $test.File $test.Query 2>&1
    $text = ($output | Out-String).Trim()
    $ok = $LASTEXITCODE -eq 0 -and $text.Contains($test.Expect)

    if ($ok) {
        Write-Host "[PASS] $($test.Name)"
    } else {
        $failed++
        Write-Host "[FAIL] $($test.Name)"
        Write-Host "  File:  $($test.File)"
        Write-Host "  Query: $($test.Query)"
        Write-Host "  Exit:  $LASTEXITCODE"
        Write-Host "  Output:"
        Write-Host $text
    }
}

foreach ($test in $directTests) {
    $output = & $Exe @($test.Args) 2>&1
    $text = ($output | Out-String).Trim()
    $ok = $LASTEXITCODE -eq 0

    foreach ($expected in $test.Expect) {
        if (-not $text.Contains($expected)) {
            $ok = $false
            break
        }
    }

    if ($ok) {
        Write-Host "[PASS] $($test.Name)"
    } else {
        $failed++
        Write-Host "[FAIL] $($test.Name)"
        Write-Host "  Args:  $($test.Args -join ' ')"
        Write-Host "  Exit:  $LASTEXITCODE"
        Write-Host "  Output:"
        Write-Host $text
    }
}

if (Test-Path -LiteralPath $DebugExe) {
    foreach ($test in $debugCheckTests) {
        $output = & $DebugExe @($test.Args) 2>&1
        $text = ($output | Out-String).Trim()
        $ok = $LASTEXITCODE -eq 0

        foreach ($expected in $test.Expect) {
            if (-not $text.Contains($expected)) {
                $ok = $false
                break
            }
        }

        if ($ok) {
            Write-Host "[PASS] $($test.Name)"
        } else {
            $failed++
            Write-Host "[FAIL] $($test.Name)"
            Write-Host "  Args:  $($test.Args -join ' ')"
            Write-Host "  Exit:  $LASTEXITCODE"
            Write-Host "  Output:"
            Write-Host $text
        }
    }
} else {
    Write-Host "[SKIP] debugger AST checks (missing $DebugExe)"
}

if (Test-Path -LiteralPath $CelidaeExe) {
    foreach ($test in $celidaeTests) {
        $output = & $CelidaeExe @($test.Args) 2>&1
        $text = ($output | Out-String).Trim()
        $ok = $LASTEXITCODE -eq 0
        foreach ($expected in $test.Expect) {
            if (-not $text.Contains($expected)) {
                $ok = $false
                break
            }
        }
        # Optional. Some of these views are defined as much by what they must
        # not contain as by what they must: the ER view is only distinct from
        # the schema view because it carries no field nodes, and an
        # Expect-only harness cannot state that.
        foreach ($rejected in $test.Reject) {
            if ($text.Contains($rejected)) {
                $ok = $false
                Write-Host "  Unexpected: $rejected"
                break
            }
        }
        if ($ok) {
            Write-Host "[PASS] $($test.Name)"
        } else {
            $failed++
            Write-Host "[FAIL] $($test.Name)"
            Write-Host "  Args:  $($test.Args -join ' ')"
            Write-Host "  Exit:  $LASTEXITCODE"
            Write-Host "  Output:"
            Write-Host $text
        }
    }
} else {
    Write-Host "[SKIP] Celidae visualization checks (missing $CelidaeExe)"
}

foreach ($test in $directInputTests) {
    $output = $test.Input | & $Exe $test.File 2>&1
    $text = ($output | Out-String).Trim()
    $ok = $LASTEXITCODE -eq 0

    foreach ($expected in $test.Expect) {
        if (-not $text.Contains($expected)) {
            $ok = $false
            break
        }
    }

    if ($ok) {
        Write-Host "[PASS] $($test.Name)"
    } else {
        $failed++
        Write-Host "[FAIL] $($test.Name)"
        Write-Host "  File:  $($test.File)"
        Write-Host "  Exit:  $LASTEXITCODE"
        Write-Host "  Output:"
        Write-Host $text
    }
}

foreach ($test in $replTests) {
    $output = $test.Input | & $Exe "--repl" $test.File 2>&1
    $text = ($output | Out-String).Trim()
    $ok = $LASTEXITCODE -eq 0

    foreach ($expected in $test.Expect) {
        if (-not $text.Contains($expected)) {
            $ok = $false
            break
        }
    }

    if ($ok) {
        Write-Host "[PASS] $($test.Name)"
    } else {
        $failed++
        Write-Host "[FAIL] $($test.Name)"
        Write-Host "  File:  $($test.File)"
        Write-Host "  Exit:  $LASTEXITCODE"
        Write-Host "  Output:"
        Write-Host $text
    }
}

if ($failed -gt 0) {
    Write-Error "$failed Felidae example test(s) failed."
    exit 1
}

Write-Host "All Felidae example tests passed."
