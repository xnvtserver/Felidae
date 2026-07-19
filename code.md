# Felidae Code Implementation Technical Details

This document explains the internal implementation of the direct interpreter.

The project is split into four main stages:

```text
Lexer -> Parser -> AST -> Interpreter
```

There is no VM and no bytecode layer in this version. Parsed statements become AST objects, and the interpreter directly walks the AST during resolution.

## 1. Project layout

```text
src/
  Token.h
  Lexer.h
  Lexer.cpp
  AST.h
  Parser.h
  Parser.cpp
  Interpreter.h
  Interpreter.cpp
  main.cpp

examples/
  main.fx
  family.fx

CMakeLists.txt
```

## 2. Token layer

Defined in:

```text
src/Token.h
```

The lexer emits tokens using `TokenType`:

```cpp
enum class TokenType {
    End,
    Ident,
    String,
    Number,

    Import,

    LParen,
    RParen,
    Comma,
    Colon,
    Dot,
    Question,

    Arrow,      // =>
    EqEq,       // ==
    NotEq,      // !=
    LT,         // <
    LTE,        // <=
    GT,         // >
    GTE         // >=
};
```

Each token stores:

```cpp
struct Token {
    TokenType type;
    std::string text;
    int line = 1;
    int column = 1;
};
```

Line and column are used for parser and lexer error messages.

## 3. Lexer

Defined in:

```text
src/Lexer.h
src/Lexer.cpp
```

The lexer converts source text into a vector of tokens.

Supported lexical elements:

```text
identifiers
strings
numbers
import keyword
=>
==
!=
< <= > >=
( ) , : . ?
# line comments
```

Important behavior:

```text
# starts a comment until end of line
"..." creates a string token
123 and 12.5 create number tokens
import is recognized as a keyword
all other names become identifiers
```

Example:

```Felidae
Employee(name: "Alice", role: "Engineer").
```

Token stream, simplified:

```text
Ident(Employee)
(
Ident(name)
:
String(Alice)
,
Ident(role)
:
String(Engineer)
)
.
End
```

## 4. AST design

Defined in:

```text
src/AST.h
```

The AST uses virtual base classes.

Base node:

```cpp
class AstNode {
public:
    virtual ~AstNode() = default;
    virtual std::string debug() const = 0;
};
```

Expression base:

```cpp
class Expr : public AstNode {
public:
    virtual std::shared_ptr<Expr> clone() const = 0;
};
```

Expression types:

```text
StringExpr
NumberExpr
VarExpr
```

Goal base:

```cpp
class Goal : public AstNode {
public:
    virtual std::shared_ptr<Goal> clone() const = 0;
};
```

Goal types:

```text
CallGoal      -> predicate call goal
BinaryGoal    -> comparison goal such as x == y or age >= 18
```

Statement types:

```text
ImportStmt    -> import "file.fx".
ClauseStmt    -> fact or rule
```

Program root:

```cpp
class Program final : public AstNode {
public:
    std::vector<std::shared_ptr<Statement>> statements;
};
```

## 5. Fact and rule representation

A fact is a `ClauseStmt` with an empty body:

```cpp
class ClauseStmt final : public Statement {
public:
    Call head;
    std::vector<std::shared_ptr<Goal>> body;

    bool isFact() const { return body.empty(); }
};
```

Example fact:

```Felidae
Employee(name: "Alice", role: "Engineer").
```

AST shape:

```text
ClauseStmt
  head = Call(Employee)
    args:
      name -> StringExpr("Alice")
      role -> StringExpr("Engineer")
  body = []
```

A rule is a `ClauseStmt` with a non-empty body:

```Felidae
Engineer(name) =>
    Employee(name: name, role: "Engineer").
```

AST shape:

```text
ClauseStmt
  head = Call(Engineer)
    args:
      positional -> VarExpr(name)
  body:
    CallGoal(Employee)
      name -> VarExpr(name)
      role -> StringExpr("Engineer")
```

## 6. Parser

Defined in:

```text
src/Parser.h
src/Parser.cpp
```

The parser is a recursive descent parser.

Main parser methods:

```cpp
Program parseProgram();
std::vector<std::shared_ptr<Goal>> parseQuery();
std::shared_ptr<Statement> parseStatement();
std::shared_ptr<ImportStmt> parseImport();
std::shared_ptr<ClauseStmt> parseClause();
Call parseCall();
Arg parseArg();
std::shared_ptr<Goal> parseGoal();
std::shared_ptr<Expr> parseExpr();
```

Grammar, simplified:

```text
program     := statement* End
statement   := importStmt | clauseStmt
importStmt  := "import" String "."
clauseStmt  := call ("=>" goalList)? "."
goalList    := goal ("," goal)*
goal        := call | expr comparison expr
call        := Ident "(" argList? ")"
argList     := arg ("," arg)*
arg         := Ident ":" expr | expr
expr        := String | Number | Ident
comparison  := "==" | "!=" | "<" | "<=" | ">" | ">="
query       := "?"? goalList "."? End
```

Important parser behavior:

```text
If an identifier is followed by ':' inside a call, it becomes a named argument.
If an identifier is not followed by ':', it becomes a variable expression.
If an identifier is followed by '(', it is parsed as a predicate call.
```

## 7. Import loader

Implemented in:

```text
src/main.cpp
```

Function:

```cpp
static void loadProgramRecursive(const fs::path& file,
                                 Interpreter& interpreter,
                                 std::set<fs::path>& visited)
```

Behavior:

```text
1. Normalize the input file path.
2. Skip the file if it was already visited.
3. Parse the file.
4. Load imports first.
5. Add local clauses to the interpreter.
```

Imports are relative to the current file:

```cpp
loadProgramRecursive(baseDir / imp->path, interpreter, visited);
```

This prevents duplicate loading and supports nested imports.

## 8. Interpreter storage

Defined in:

```text
src/Interpreter.h
src/Interpreter.cpp
```

Clauses are stored by predicate name:

```cpp
std::unordered_map<std::string, std::vector<std::shared_ptr<ClauseStmt>>> clauses_;
```

Current indexing level:

```text
predicate name only
```

The current implementation does not yet index by arity or argument values. That can be added later.

## 9. Solving algorithm

Entry point:

```cpp
std::vector<Solution> Interpreter::solve(
    const std::vector<std::shared_ptr<Goal>>& queryGoals,
    size_t maxSolutions
)
```

Core recursive function:

```cpp
void Interpreter::solveRecursive(
    const std::vector<std::shared_ptr<Goal>>& goals,
    Env env,
    std::vector<Solution>& out,
    size_t maxSolutions,
    size_t depth
)
```

Algorithm:

```text
solve(goals, env):
    if goals is empty:
        emit env as solution
        return

    first = first goal
    rest = remaining goals

    if first is a comparison:
        solve comparison
        if success, solve(rest, env)
        return

    if first is a call:
        find clauses with same predicate name
        for each clause:
            standardize clause variables apart
            try to unify query call with clause head
            if unify succeeds:
                combined = clause body + rest
                solve(combined, new_env)
```

This is direct interpretation with backtracking.

## 10. Environment and substitution

The environment maps variable names to expressions:

```cpp
using Env = std::unordered_map<std::string, std::shared_ptr<Expr>>;
```

Example:

```text
name -> "Alice"
office -> "SEA"
```

`resolveExpr()` follows chained variable bindings:

```text
X -> Y -> "Alice"
```

The function recursively follows aliases until it reaches a literal or an unbound variable.

## 11. Unification

Main function:

```cpp
bool Interpreter::unifyExpr(
    const std::shared_ptr<Expr>& a,
    const std::shared_ptr<Expr>& b,
    Env& env
)
```

Unification behavior:

```text
1. Resolve both expressions through the environment.
2. If both are the same variable, success.
3. If left is an unbound variable, bind it to right.
4. If right is an unbound variable, bind it to left.
5. If both are strings, compare string values.
6. If both are numbers, compare numeric values using small epsilon.
7. Otherwise fail.
```

The current version supports simple variable/literal unification. It does not yet support compound term unification.

## 12. Call unification

Main function:

```cpp
bool Interpreter::unifyCall(const Call& goal, const Call& head, Env& env)
```

Behavior:

```text
1. Predicate names must match.
2. Each query argument must match a corresponding head argument.
3. Named arguments are matched by name.
4. Positional arguments are matched by index.
5. Positional-only calls require equal arity.
6. Named calls allow partial matching.
```

Partial named matching example:

```Felidae
Employee(name: "Alice", role: "Engineer", office: "SEA").
```

can match:

```Felidae
Employee(name: name, role: "Engineer")
```

because `office` is not required by the goal.

## 13. Backtracking

Backtracking happens naturally because `solveRecursive()` tries every matching clause.

For each candidate clause:

```text
1. Copy current environment.
2. Try unification.
3. If unification succeeds, continue solving the body.
4. If it later fails, the copied environment is discarded.
5. The next clause is tried.
```

Because environments are copied, this version does not need a Prolog-style trail stack yet.

This is simple and correct for a first direct interpreter, but less memory-efficient than a real Prolog engine.

## 14. Standardizing apart / variable renaming

Before using a rule, its internal variables are renamed with a unique prefix:

```cpp
std::shared_ptr<ClauseStmt> Interpreter::standardizeApart(const ClauseStmt& clause)
```

Example rule:

```Felidae
Ancestor(x, y) =>
    Parent(parent: x, child: z),
    Ancestor(z, y).
```

On one use, it may become internally:

```text
Ancestor(__r12_x, __r12_y) =>
    Parent(parent: __r12_x, child: __r12_z),
    Ancestor(__r12_z, __r12_y).
```

This prevents variables from different rule invocations from accidentally sharing bindings.

Internal renamed variables are hidden from query output by `main.cpp` when printing user-visible query variables.

## 15. Comparisons

Implemented by:

```cpp
bool Interpreter::solveBinaryGoal(const BinaryGoal& goal, Env& env)
```

Behavior:

```text
== uses unification
!= succeeds when unification would fail
< <= > >= require both sides to be resolved ground literals
```

Numbers are compared numerically. Strings are compared lexicographically.

## 16. Query variable collection

Implemented in:

```text
src/main.cpp
```

Before solving, the program collects variables from the query only:

```cpp
static std::vector<std::string> collectQueryVars(
    const std::vector<std::shared_ptr<Goal>>& goals
)
```

Only those variables are printed.

Example:

```bash
./build/felidae examples/main.fx '? Engineer(name: name)'
```

prints:

```text
name = "Alice"
```

It does not print internal variables created by rules.

## 17. Current complexity

For a query goal, lookup is:

```text
O(number of clauses for predicate name)
```

Because there is no argument indexing yet.

For large codebases, the next optimization should be:

```text
predicate name + arity index
named argument index
constant argument index
module-level lazy loading
rule dependency graph
```

## 18. Why this is direct interpretation, not VM

This implementation does not compile rules into bytecode.

It directly uses AST objects:

```text
CallGoal
BinaryGoal
ClauseStmt
Expr
```

The interpreter recursively evaluates AST nodes.

So execution path is:

```text
source -> tokens -> AST -> direct AST walking interpreter
```

not:

```text
source -> tokens -> AST -> bytecode -> VM
```

## 19. Useful next improvements

Recommended next steps:

```text
1. Add arity-aware predicate keys: PredicateName/arity.
2. Add named-field indexes for facts.
3. Add compound terms and list terms.
4. Add arithmetic expressions and builtins.
5. Add module namespaces.
6. Add REPL mode.
7. Add better error recovery.
8. Add SQL or OLAP fact-store pushdown later.
9. Add tail-recursion safety or iterative resolver.
10. Add tracing/explain mode for debugging proof search.
```

## 20. Main implementation idea

The interpreter is built around one simple concept:

```text
A query is solved by repeatedly proving goals.
```

Each goal either:

```text
matches a clause head
runs a comparison
produces a new environment
continues with the remaining goals
```

This gives the language its core Prolog-like behavior:

```text
unification
substitution
backtracking
recursive rule resolution
multiple solutions
```
