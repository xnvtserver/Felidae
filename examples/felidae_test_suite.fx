import "process".
import "str".

AssertContains(name: string, output: string, expected: string) =>
    ok := str.contains(data: output, needle: expected),
    where ok == "true",
    return (name).

RunCase(name: string, command: string, expected: string) =>
    output := process.exec(command: command),
    AssertContains(name: name, output: output, expected: expected),
    return (name).

RunCase2(name: string, command: string, expectedA: string, expectedB: string) =>
    output := process.exec(command: command),
    AssertContains(name: name, output: output, expected: expectedA),
    AssertContains(name: name, output: output, expected: expectedB),
    return (name).

RunCase3(name: string, command: string, expectedA: string, expectedB: string, expectedC: string) =>
    output := process.exec(command: command),
    AssertContains(name: name, output: output, expected: expectedA),
    AssertContains(name: name, output: output, expected: expectedB),
    AssertContains(name: name, output: output, expected: expectedC),
    return (name).

RunNegative(name: string, command: string, expected: string) =>
    output := process.exec(command: command),
    AssertContains(name: name, output: output, expected: expected),
    return (name).

main() =>
    exe := "build\\felidae.exe",

    family := RunCase2(
        name: "family query",
        command: "build\\felidae.exe examples\\family.fx \"? Parent(parent: Parent, child: Child)\"",
        expectedA: "Parent = \"Alice\"",
        expectedB: "Child = \"Bob\""
    ),
    memberAccess := RunCase(
        name: "member access",
        command: "build\\felidae.exe examples\\member_access_list.fx \"? EmployeeAt(0, e), HasManager(e, name)\"",
        expected: "name = \"Alice\""
    ),
    functionImport := RunCase2(
        name: "multi-file import",
        command: "build\\felidae.exe examples\\function_caller.fx",
        expectedA: "RemoteRole called with name: Anu, role: Student",
        expectedB: "result: fn:tuple(value: \"true\")"
    ),
    tupleAssignment := RunCase3(
        name: "tuple destructuring",
        command: "build\\felidae.exe examples\\tuple_assignment.fx",
        expectedA: "name: \"Alice\"",
        expectedB: "active: \"true\"",
        expectedC: "raw_flags: fn:tuple(value: \"true\", value: \"true\", value: \"true\")"
    ),
    methodTruth := RunCase3(
        name: "method truth tuple",
        command: "build\\felidae.exe examples\\method_truth_tuple.fx",
        expectedA: "manager_true: fn:tuple(value: \"true\", value: \"true\")",
        expectedB: "manager_false: fn:tuple(value: \"true\", value: \"false\")",
        expectedC: "lambda_result: [\"Alice\", \"Carol\"]"
    ),
    stdlib := RunCase3(
        name: "stdlib utilities",
        command: "build\\felidae.exe examples\\stdlib_utilities.fx",
        expectedA: "trimmed: \"Alice,Engineer,SEA\"",
        expectedB: "sea_count: 2",
        expectedC: "line_count: 3"
    ),
    fileOps := RunCase3(
        name: "file operations",
        command: "build\\felidae.exe examples\\file_operations_test.fx",
        expectedA: "write: \"ok\"",
        expectedB: "append: \"ok\"",
        expectedC: "lines: [\"alpha\", \"beta\", \"gamma\"]"
    ),
    backtracking := RunCase3(
        name: "backtracking and unification",
        command: "build\\felidae.exe examples\\backtracking_unification.fx",
        expectedA: "choice_count: 4",
        expectedB: "same_pair_count: 2",
        expectedC: "nested_count: 2"
    ),
    threadMemory := RunCase3(
        name: "thread memory snapshot",
        command: "build\\felidae.exe examples\\thread_memory_test.fx",
        expectedA: "status1: \"finished\"",
        expectedB: "result1: \"{count: 2, names: [\\\"Alice\\\", \\\"Carol\\\"]}\"",
        expectedC: "result2: \"{count: 3}\""
    ),
    nativeAbi := RunCase(
        name: "native ABI",
        command: "build\\felidae.exe examples\\native_abi_success.fx",
        expected: "expression: \"expression ok\""
    ),
    debugCheck := RunCase(
        name: "debug check",
        command: "build\\felidae_debug.exe examples\\native_module_smoke.fx --check",
        expected: "FELIDAE_CHECK_OK"
    ),
    astWarnings := RunCase3(
        name: "ast analyzer warnings",
        command: "build\\felidae_debug.exe examples\\diagnostics_ast_warnings.fx --check",
        expectedA: "Duplicate fact declaration for 'Person'.",
        expectedB: "Variable 'temp' is declared but never used in method 'UnusedHelper'.",
        expectedC: "FELIDAE_CHECK_OK"
    ),

    badType := RunNegative(
        name: "negative tuple type mismatch",
        command: "cmd /C \"build\\felidae.exe examples\\invalid\\tuple_assignment_type_mismatch.fx 2>&1 & exit /b 0\"",
        expected: "ProgrammingError: tuple assignment target 'active' expects number"
    ),
    badArity := RunNegative(
        name: "negative tuple arity mismatch",
        command: "cmd /C \"build\\felidae.exe examples\\invalid\\tuple_assignment_arity_mismatch.fx 2>&1 & exit /b 0\"",
        expected: "ProgrammingError: tuple assignment expected 2 value(s), got 3"
    ),
    badDuplicate := RunNegative(
        name: "negative tuple duplicate target",
        command: "cmd /C \"build\\felidae.exe examples\\invalid\\tuple_assignment_duplicate_target.fx 2>&1 & exit /b 0\"",
        expected: "Duplicate tuple assignment target 'name'"
    ),
    badUndeclared := RunNegative(
        name: "negative undeclared variable",
        command: "cmd /C \"build\\felidae.exe examples\\invalid\\undeclared_body_var.fx \"? EngineerInSEA(name: Name)\" 2>&1 & exit /b 0\"",
        expected: "Variable 'e' is used before declaration"
    ),
    badGlobalAssign := RunNegative(
        name: "negative global shadow assignment",
        command: "cmd /C \"build\\felidae.exe examples\\invalid\\global_shadow_assignment.fx 2>&1 & exit /b 0\"",
        expected: "Variable 'Shared' is already assigned and immutable"
    ),
    badGlobalTupleAssign := RunNegative(
        name: "negative global shadow tuple assignment",
        command: "cmd /C \"build\\felidae.exe examples\\invalid\\global_shadow_tuple_assignment.fx 2>&1 & exit /b 0\"",
        expected: "Variable 'Shared' is already assigned and immutable"
    ),

    return (
        status: "passed",
        launcher: exe,
        cases: [
            family,
            memberAccess,
            functionImport,
            tupleAssignment,
            methodTruth,
            stdlib,
            fileOps,
            backtracking,
            threadMemory,
            nativeAbi,
            debugCheck,
            astWarnings,
            badType,
            badArity,
            badDuplicate,
            badUndeclared,
            badGlobalAssign,
            badGlobalTupleAssign
        ]
    ).
