param(
    [string]$Exe = "build\felidae.exe",
    [string]$DebugExe = "build\celidae.exe"
)

$ErrorActionPreference = "Continue"

if (-not (Test-Path -LiteralPath $Exe)) {
    Write-Error "Missing $Exe. Build first, for example: .\build.cmd"
}

if (-not (Test-Path -LiteralPath $DebugExe) -and (Test-Path -LiteralPath "build\felidae_debug.exe")) {
    $DebugExe = "build\felidae_debug.exe"
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
    @{ Name = "global binding in fact declaration"; File = "examples\complex_facts.fx"; Query = "? AddressRadiusUnit(value: value)"; Expect = @("value = `"cm`"") },
    @{ Name = "head variables declared"; File = "examples\head_vars.fx"; Query = "? HeadManagerName(name: name)"; Expect = @("name = `"Alice`"") },
    @{ Name = "omitted head output binds in method body"; File = "examples\head_vars.fx"; Query = ""; Expect = @("Hello, World!", "has_manager: fn:tuple(value: `"true`", value: `"true`")") },
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
    @{ Name = "throw division"; File = "examples\exceptions.fx"; Query = "? DivideFailure(error_reason: error_reason)"; Expect = @("error_reason = `"DivisionByZero`"") },
    @{ Name = "throw programming"; File = "examples\exceptions.fx"; Query = "? ProgrammingFailure(error_reason: error_reason)"; Expect = @("error_reason = `"ProgrammingError`"") },
    @{ Name = "throw other"; File = "examples\exceptions.fx"; Query = "? OtherFailure(error_reason: error_reason)"; Expect = @("error_reason = `"UnknownError`"") },
    @{ Name = "throw routed handler"; File = "examples\exceptions.fx"; Query = "? RoutedFailure(msg: msg)"; Expect = @("msg = `"thrown from module a`"") },
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
    @{ Name = "method truth tuple values"; File = "examples\method_truth_tuple.fx"; Query = ""; Expect = @("manager_true: fn:tuple(value: `"true`", value: `"true`")", "manager_false: fn:tuple(value: `"true`", value: `"false`")", "technical_manager: fn:tuple(value: `"true`", value: `"true`")", "ancestor_true: fn:tuple(value: `"true`")", "nested_false: fn:tuple(value: `"false`")", "print_once: fn:tuple(value: `"true`")", 'lambda_result: ["Alice", "Carol"]', 'explicit_true: "true"') },
    @{ Name = "tuple destructuring assignment"; File = "examples\tuple_assignment.fx"; Query = ""; Expect = @('name: "Alice"', 'active: "true"', 'score: 2.5', 'flags: fn:tuple(value: "true", value: "true", value: "true")', 'raw_flags: fn:tuple(value: "true", value: "true", value: "true")') },
    @{ Name = "thread snapshot worker"; File = "examples\thread_snapshot_test.fx"; Query = ""; Expect = @("started: `"started`"", "status: `"finished`"", "result: `"{status: \`"done\`"}`"") }
)

$negativeTests = @(
    @{ Name = "reject undeclared body variable"; File = "examples\invalid\undeclared_body_var.fx"; Query = "? EngineerInSEA(name: Name)"; Expect = "Variable 'e' is used before declaration" },
    @{ Name = "reject member access in head"; File = "examples\invalid\member_access_head.fx"; Query = "? HasManager(name: Name)"; Expect = "Rule head fields cannot use member access" },
    @{ Name = "reject double colon call"; File = "examples\invalid\double_colon_call.fx"; Query = "? Bad(value: value)"; Expect = "'::' is not supported in Felidae" },
    @{ Name = "reject immutable reassignment"; File = "examples\invalid\reassign_immutable.fx"; Query = "? Bad(value: value)"; Expect = "already assigned and immutable" },
    @{ Name = "reject method immutable reassignment"; File = "examples\invalid\method_reassign.fx"; Query = "? BadResult(value: value)"; Expect = "already assigned and immutable" },
    @{ Name = "reject unknown extended parent"; File = "examples\invalid\method_unknown_parent.fx"; Query = "? Employee(name: name)"; Expect = "Unknown parent fact/type" },
    @{ Name = "reject unknown call field"; File = "examples\invalid\unknown_call_field.fx"; Query = "? Bad(value: value)"; Expect = "Unknown field 'key' for Employee" },
    @{ Name = "reject unknown head type"; File = "examples\invalid\unknown_head_type.fx"; Query = "? Bad(value: value)"; Expect = "Unknown type annotation 'Name'" },
    @{ Name = "reject stray return after terminator"; File = "examples\invalid\stray_return.fx"; Query = ""; Expect = "'return' is only valid inside a method body" },
    @{ Name = "reject missing source or native module"; File = "examples\invalid\missing_module.fx"; Query = ""; Expect = "Module 'missing_native_or_source_module' not found" },
    @{ Name = "reject dangling else"; File = "examples\invalid\dangling_else.fx"; Query = "? Bad(value: value)"; Expect = "'else' is only valid inside method fallback branches" },
    @{ Name = "reject native type mismatch"; File = "examples\invalid\native_type_mismatch.fx"; Query = ""; Expect = "expects argument 'value' to be string" },
    @{ Name = "reject native reported failure"; File = "examples\invalid\native_abi_failure.fx"; Query = ""; Expect = "Native function 'smoke:fail' failed: expected native failure" },
    @{ Name = "reject native invalid json"; File = "examples\invalid\native_abi_invalid_json.fx"; Query = ""; Expect = "Native function 'smoke:invalidJson' returned invalid JSON" },
    @{ Name = "reject division by zero"; File = "examples\invalid\division_by_zero.fx"; Query = ""; Expect = "DivisionByZero" },
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
    @{ Name = "reject invalid fact algorithm"; File = "examples\invalid\fact_invalid_algorithm.fx"; Query = ""; Expect = "FactConfigError: unsupported algorithm 'approximate_magic' before calling native library" },
    @{ Name = "reject invalid fact threshold"; File = "examples\invalid\fact_invalid_threshold.fx"; Query = ""; Expect = "FactConfigError: 'threshold' must be between 0 and 1 before calling native library" },
    @{ Name = "reject invalid fact analysis required fields"; File = "examples\invalid\fact_analysis_invalid_required_fields.fx"; Query = ""; Expect = "expects argument 'required_fields' to be array" },
    @{ Name = "reject fact comparison non object"; File = "examples\invalid\fact_compare_non_object.fx"; Query = ""; Expect = "fact-shaped object values" }
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
    @{ Name = "direct imported method call"; Args = @("examples\function_caller.fx"); Expect = @("RemoteRole called with name: Anu, role: Student", 'result: fn:tuple(value: "true")') },
    @{ Name = "direct backtracking unification"; Args = @("examples\backtracking_unification.fx"); Expect = @("choice_count: 4", "same_pair_count: 2", "employee_count: 2", "nested_count: 2") },
    @{ Name = "recursive multi-clause method"; Args = @("examples\recursive_ancestor.fx", "? AncestorOf(descendant: descendant, ancestor: ancestor)"); Expect = @('descendant = "kitten", ancestor = "cat"', 'descendant = "kitten", ancestor = "organism"', 'descendant = "cat", ancestor = "organism"') },
    @{ Name = "interpreter viewer json loads imported country fact db"; Args = @("examples\country_query.fx", "--visualize-data-json", "--load-imports"); Expect = @("FELIDAE_GRAPH_BEGIN", '"label":"Country","kind":"fact","detail":"records=249 fields=4"', '"label":"IndiaCountry","kind":"method"') },
    @{ Name = "interpreter viewer html loads imported country fact db"; Args = @("examples\country_query.fx", "--visualize-data-html", "--load-imports"); Expect = @("<!doctype html>", "Celidae Data Visualization", '"label":"Country","kind":"fact","detail":"records=249 fields=4"') },
    @{ Name = "interpreter programmatic visualization builtins"; Args = @("examples\visualize_programmatic.fx"); Expect = @('json_has_country: "true"', 'json_has_records: "true"', 'html_has_document: "true"', 'wrote_html: "ok"') },
    @{ Name = "facts to csv and json"; Args = @("examples\facts_to_csv_json.fx"); Expect = @("row_count: 3", "sea_engineer_count: 2", 'csv_text: "name,role,office', 'SeaEngineer(name: \"Alice\"', 'json_text: "[{\"name\":\"Alice\"', 'parsed_rows: [{name: "Alice"') },
    @{ Name = "fact db create"; Args = @("examples\fact_db_create.fx"); Expect = @("source_count: 4", "inserted_count: 3", 'Customer(name: \"Alice\"', 'Customer(name: \"Dana\"') },
    @{ Name = "fact db update"; Args = @("examples\fact_db_update.fx"); Expect = @("updated_count: 3", "gold_count: 2", 'tier: \"gold\"') },
    @{ Name = "fact db delete"; Args = @("examples\fact_db_delete.fx"); Expect = @("kept_count: 2", "deleted_count: 1", 'reason: "inactive"', 'Customer(name: \"Dana\"') },
    @{ Name = "csv to generated felidae facts"; Args = @("examples\csv_school.fx"); Expect = @('export: "ok"', '__type: "School"', 'student: "John"', 'class: "10c"', 'student: "Maya"') },
    @{ Name = "query generated csv facts"; Args = @("examples\data\converted_csv_school.fx", '? School(student: student, subject: subject)'); Expect = @('student = "John"', 'subject = "physics"') },
    @{ Name = "file operations reusable test"; Args = @("examples\file_operations_test.fx"); Expect = @('write: "ok"', 'append: "ok"', 'lines: ["alpha", "beta", "gamma"]', 'text: "alpha\nbeta\ngamma\n"') },
    @{ Name = "streaming file reader smoke"; Args = @("examples\large_file_stream_test.fx"); Expect = @('write: "ok"', 'line_count: 5', 'third: "gamma"', 'has_epsilon: "true"') },
    @{ Name = "stdlib utilities"; Args = @("examples\stdlib_utilities.fx"); Expect = @('trimmed: "Alice,Engineer,SEA"', 'parts: ["Alice", "Engineer", "SEA"]', 'normalized: "Alice,Engineer,NYC"', 'has_engineer: "true"', 'starts_alice: "true"', 'ends_sea: "true"', 'has_office: "true"', 'office: "SEA"', 'sea_count: 2', 'deleted_count: 2', 'write_status: "ok"', 'second_line: "Engineer"', 'line_count: 3') },
    @{ Name = "web server reusable integration test"; Args = @("examples\web_server_test.fx"); Expect = @('ready: "ok"', 'get: "Hello World"', 'post: "Hello World"', 'put: "Hello World"', 'delete: "Hello World"') },
    @{ Name = "fx self analysis line classifier"; Args = @("examples\fx_self_analysis.fx"); Expect = @('line_count: 23', 'http_call_count: 1', 'missing_language_primitives:', 'string.split', 'method body builtin output binding') },
    @{ Name = "data structure stress"; Args = @("examples\data_structure_stress.fx"); Expect = @('count: 2', 'generated: "ok"', 'name: "Alice"', 'known_gap: "expression array:get is not evaluated yet"') },
    @{ Name = "arithmetic and boolean methods"; Args = @("examples\arithmetic_and_boolean.fx"); Expect = @('first_has_manager: fn:tuple(value: "true", value: "true")', 'add: 5', 'sub: 6', 'mul: 42', 'div: 4', 'precedence: 14', 'grouped: 20') },
    @{ Name = "native ABI smoke"; Args = @("examples\native_module_smoke.fx"); Expect = @('"native module loaded"') },
    @{ Name = "native ABI in thread"; Args = @("examples\native_thread_smoke.fx"); Expect = @('started: "started"', 'thread native ok') },
    @{ Name = "native ABI expression"; Args = @("examples\native_abi_success.fx"); Expect = @('expression: "expression ok"') },
    @{ Name = "colon dot interchangeable access"; Args = @("examples\access_interchange.fx"); Expect = @("dot: 42", "colon: 42", "mixed: 42", "direct: 42", "checked: {dot: 42, colon: 42, mixed: 42}") },
    @{ Name = "method false tuple value"; Args = @("examples\invalid\method_value_no_result.fx"); Expect = @('result: fn:tuple(value: "false")') },
    @{ Name = "native stdlib execution"; Args = @("examples\native_stdlib.fx"); Expect = @("writeStatus: `"ok`"", "readBack: `"Felidae IO`"", "exists: `"true`"", "root: 9", "powered: 256", "activation: 0.5", "dot: 32", "mse: 1.33333333333333") },
    @{ Name = "native WordNet semantic algorithms"; Args = @("examples\wordnet_native_demo.fx"); Expect = @('synset: "cat.n.01"', 'pathSimilarity: {score: 0.333333', 'wuPalmer: {score: 0.666667', 'resnik: {score:', 'factSimilarity: {score: 0.333333', 'score: 4', 'text: "gato"') },
    @{ Name = "native WordNet command-line query"; Args = @("examples\wordnet_native_demo.fx", "? WordNetCliSmoke(result: Result)"); Expect = @('Result = {score: 0.333333', 'lcs: "animal.n.01"') },
    @{ Name = "native fact semantic phase 1"; Args = @("examples\fact_semantic_phase1.fx"); Expect = @('projection: {kind: "fact"', 'node_count:', 'similarity: {score:', 'algorithm: "semantic_recursive"', 'lexical_algorithm: "wu_palmer"', 'difference: {mode: "property_exact"', 'near: {matched:', 'wordnet_internal_service_pending_for_phase_2') },
    @{ Name = "fact semantic unify is explicit"; Args = @("examples\fact_import_keeps_exact_unification.fx"); Expect = @('semantic: {unified:', 'threshold:', 'wordnet_internal_service_pending_for_phase_2') },
    @{ Name = "minimal fact similarity"; Args = @("examples\fact_similarity_minimal.fx"); Expect = @('similar: {score:', 'lexical_algorithm: "leacock_chodorow"', 'propertyMatch: {score:', 'difference: {mode: "property_exact"', 'nearest: {count: 1', 'name: "Ravi"') },
    @{ Name = "fact reasoning and analysis phase 2"; Args = @("examples\fact_reasoning_phase2.fx"); Expect = @('propertyComparison: {score:', 'mode: "property_exact"', 'semanticSimilarity: {score:', 'lexical_algorithm: "leacock_chodorow"', 'common: {ancestor_type: "Person"', 'path: {reachable:', 'distance: 2', 'evidence: {probability: 0.733333', 'nearest: {count: 1', 'next: {prediction: {__type: "ClimatePrediction"', 'method: "numeric_gini_stump"', 'split: {feature: "humidity"', 'model_fx: "DecisionTreeModel(', 'applied: {prediction: "rainy"', 'decision_route: ["numeric_split_right"]', 'evaluated: {model_type: "decision_tree"', 'accuracy: 1', 'savedModel: "ok"') },
    @{ Name = "fact data science boundaries"; Args = @("examples\fact_data_science_boundaries.fx"); Expect = @('floatArithmetic: {sum: 0.3', 'numericStats: {meanWeight:', 'profile: {method: "numeric_fact_profile"', 'clusters: {model_type: "k_means"', 'scaling: "standardized"', 'classified: {prediction: "livestock"', 'predictedWeight: {prediction:', 'goatSheep: {score:', 'method: "fact_property_ancestor"', 'property_score:', 'common_ancestor: "Ruminant"', 'goatWolf: {score:', 'common_ancestor: "CommonFact"', 'nearestLivestock: {count: 2', 'rejected_count: 1', 'generatedSynset: {__type: "RuminantSynset"', 'ancestor: "Ruminant"') },
    @{ Name = "fact comparison scenarios"; Args = @("examples\fact_comparison_scenarios.fx"); Expect = @('employeeSibling: {score:', 'method: "fact_property_ancestor"', 'common_ancestor: "Employee"', 'employeeContractor: {score:', 'common_ancestor: "CommonFact"', 'sensorSibling: {score:', 'common_ancestor: "Sensor"', 'sauceSibling: {score:', 'common_ancestor: "Sauce"', 'personnelRanking: {count: 2', 'rejected_count: 1', 'EmployeeCapabilitySynset', 'SauceFlavorSynset', 'scenarioConfidence:') },
    @{ Name = "fact comparison edge cases"; Args = @("examples\fact_comparison_edge_cases.fx"); Expect = @('propertyNoAncestor: {score:', 'common_ancestor: "CommonFact"', 'ancestorPoorProperties: {score:', 'common_ancestor: "Sensor"', 'missingFieldComparison: {score:', 'exactPropertyComparison: {score:', 'constrained: {count: 2', 'matched_candidate_count: 2', 'rejected_count: 2') },
    @{ Name = "fact graph wordnet style algorithms"; Args = @("examples\fact_graph_wordnet_algorithms.fx"); Expect = @('direct: {matched: "true"', 'ancestors: {fact: "Goat"', 'facts: ["Ruminant", "Mammal", "Animal", "LivingThing"]', 'descendants: {fact: "Mammal"', 'lowestCommonAncestor: {ancestor_type: "Ruminant"', 'shortestPath: {reachable: "true", distance: 4', 'pathSimilarity: {score: 0.2', 'algorithm: "path"', 'wuPalmer: {score: 0.8', 'algorithm: "wu_palmer"', 'resnik: {score:', 'algorithm: "resnik"', 'lin: {score:', 'algorithm: "lin"', 'frequencyStats: {method: "fact_frequency_statistics"', 'normalized: {input: "Black Sheep", normalized: "black_sheep"', 'clusters: {model_type: "k_means"', 'classification: {goatGraphScore: {score: 0.8') },
    @{ Name = "advanced mortality fact reasoning"; Args = @("examples\advanced_mortality_fact_reasoning.fx"); Expect = @('socratesProof: {subject: "Socrates", mortal: "true"', 'path: ["Socrates", "Man", "Human", "MortalBeing"]', 'ancestors: {fact: "Socrates"', 'facts: ["Man", "Human", "MortalBeing", "LivingThing"]', 'ramProof: {subject: "Ram", mortal: "true"', 'rule: "mortal_by_inherited_fact_similarity_to_proven_mortal"', 'common_ancestor: "Man"', 'graphSimilarity: {score: 0.8', 'statueComparison: {score:', 'statueGraphSimilarity: {score: 0', 'manToHuman: {matched: "true"', 'socratesToMan: {matched: "true"', 'ramToMan: {matched: "true"', 'inheritedFields: {socratesMortality: "true", ramMortality: "true"', 'socratesRamLowestCommonAncestor: {ancestor_type: "Man"', 'socratesRamPath: {reachable: "true", distance: 2') },
    @{ Name = "ml fact mining"; Args = @("examples\ml_fact_mining.fx"); Expect = @('profile: {method: "numeric_fact_profile"', 'spendVisitCorrelation: {method: "pearson_fact_correlation"', 'clusters: {model_type: "k_means"', 'method: "k_means_numeric_facts"', 'trained_count: 4', 'associations: {method: "key_value_association_mining"', 'region=south', 'segment=starter', 'model: {model_type: "decision_tree"', 'spendModel: {model_type: "linear_regression"', 'split: {feature: "age"', 'predicted: {prediction: "premium"', 'predictedSpend: {prediction:', 'factPrediction: {prediction: {prediction: "starter"', 'numericPrediction: {prediction: {prediction:', 'savedModel: "ok"', 'savedRegression: "ok"') },
    @{ Name = "ml sensor fact mining"; Args = @("examples\ml_sensor_fact_mining.fx"); Expect = @('profile: {method: "numeric_fact_profile"', 'clusters: {model_type: "k_means"', 'correlation: {method: "pearson_fact_correlation"', 'stateModel: {model_type: "decision_tree"', 'split: {feature: "vibration"', 'newReading: {prediction: "risk"') },
    @{ Name = "ml sales forecast"; Args = @("examples\ml_sales_forecast.fx"); Expect = @('profile: {method: "numeric_fact_profile"', 'visitorRevenue: {method: "pearson_fact_correlation"', 'revenueModel: {model_type: "linear_regression"', 'feature: "visitors"', 'forecast: {prediction:', 'generated: {prediction: {prediction:', 'saved: "ok"') },
    @{ Name = "cooking expert system"; Args = @("examples\cooking_expert_system.fx"); Expect = @('proposal: {request:', 'predictedDishType: {prediction: "starter"', 'rankedIngredients: {count: 1', 'matched_candidate_count: 1', 'rejected_count: 7', 'required_fields: ["category", "role"]', 'name: "Tomato"', 'important_differences:', 'ingredientFitnessExample: {ingredient: "Tomato"', 'ingredientClusters: {model_type: "k_means"', 'ingredientProfile: {method: "numeric_fact_profile"', 'dishAssociations: {method: "key_value_association_mining"', 'dishType=starter', 'evidence: {probability:', 'justification: "The proposal is based on property similarity') },
    @{ Name = "cache import thread stress"; Args = @("examples\cache_thread_import_stress.fx"); Expect = @('start1: "started"', 'start2: "started"', 'start3: "started"', 'count: 12', 'Eve', 'Engineer') },
    @{ Name = "then pipeline direct execution"; Args = @("examples\then_pipeline.fx"); Expect = @('direct: {seen: 4, tag: "wrapped"}', 'nested: {seen: 13, tag: "wrapped"}', 'stopped: nil', 'arithmeticPrecedence: 10') },
    @{ Name = "then pipeline command line query"; Args = @("examples\then_pipeline.fx", "? Increment(value: 1) then Double(value: system.result) == 4"); Expect = @("true") },
    @{ Name = "auto system print"; Args = @("examples\system_print.fx"); Expect = @("Felidae system running!", "{}") },
    @{ Name = "main returns status value"; Args = @("examples\main_comment_return.fx"); Expect = @("Felidae system running!", '"true"') },
    @{ Name = "direct no main"; Args = @("examples\family.fx"); Expect = @("Program loaded successfully. No main() method found.", "Use a query argument or run with --repl.") },
    @{ Name = "help"; Args = @("--help"); Expect = @("Felidae Logic Programming Language v0.1.0", ".fx", "Total commands supported: 7", "felidae --repl examples/main.fx", "functional logic language", "______") },
    @{ Name = "debug flag"; Args = @("examples\system_print.fx", "--debug"); Expect = @("Felidae debug mode enabled", "Felidae system running!", "{}") },
    @{ Name = "version"; Args = @("--version"); Expect = @("Felidae Logic Programming Language v0.1.0") }
)

$debugCheckTests = @(
    @{ Name = "debug check loads native module"; Args = @("examples\native_module_smoke.fx", "--check"); Expect = @("FELIDAE_CHECK_OK") },
    @{ Name = "celidae queries country fact db through method"; Args = @("examples\country_query.fx", "? IndiaCountry(name: name, alpha2: alpha2, code: code)"); Expect = @('name = "India"', 'alpha2 = "IN"', 'code = "356"') },
    @{ Name = "celidae profiles country fact db"; Args = @("examples\data\converted_csv_country.fx", "--inspect-graph"); Expect = @('"detail":"records=249 fields=4"', '"detail":"present=249 missing=0 coverage=100.0%"') },
    @{ Name = "celidae viewer json loads imported country fact db"; Args = @("examples\country_query.fx", "--visualize-data-json", "--load-imports"); Expect = @("FELIDAE_GRAPH_BEGIN", '"label":"Country","kind":"fact","detail":"records=249 fields=4"', '"label":"IndiaCountry","kind":"method"') },
    @{ Name = "celidae viewer html loads imported country fact db"; Args = @("examples\country_query.fx", "--visualize-data-html", "--load-imports"); Expect = @("<!doctype html>", "Celidae Data Visualization", '"label":"Country","kind":"fact","detail":"records=249 fields=4"') }
)

$directInputTests = @(
    @{ Name = "console input number true branch"; File = "examples\console_input_branch.fx"; Input = "10`n"; Expect = @('enter x:', 'x: 10', 'matched: "true"', 'message: "x is ten"') },
    @{ Name = "console input number false branch"; File = "examples\console_input_branch.fx"; Input = "7`n"; Expect = @('enter x:', 'x: 7', 'matched: "false"', 'message: "x is not ten"') }
)

$replTests = @(
    @{ Name = "repl query global builtin"; File = "examples\direct_main.fx"; Input = "help`nversion`nAdults`ncount(Adults)`n? Person(name: x)`nexit`n"; Expect = @("REPL commands:", "Felidae Logic Programming Language v0.1.0", "Ravi", "1", 'x = "Default"', 'x = "Ravi"', 'x = "Anu"') }
)

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
    Write-Host "[SKIP] debug check loads native module (missing $DebugExe)"
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
