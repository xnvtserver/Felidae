# Felidae Notebook

Notebooks for the Felidae DSL in VS Code: markdown and code cells, a Run button on
every cell, outputs saved under each cell. Files are `.fxnb`.

It uses the **Felidae extension** (`local.felidae-vscode`) for highlighting and
formatting, and a `felidae` interpreter that can run a program from stdin
(`felidae file.fx --stdin`, in the build that adds it). Pick the interpreter with
*Felidae: Select Interpreter*; the status bar shows which one a notebook uses.

## Getting started

1. *Felidae: New Notebook*, then save it as `something.fxnb` in a project folder.
2. The folder needs an `init.fx`, the project's manifest (see below). If it is missing,
   running a cell offers to create one and tells you where the database will be.
3. Run the cells.

## How a cell runs

There is no kernel and no session. Running a cell starts **one ordinary `felidae`
process**, which ends when the cell ends. Nothing is retried: if a cell fails you
see the interpreter's own error, mapped back to the cell and line it came from.

A code cell is one of:

- **declarations**: `def`, `class` or `import` statements. They build the program.
- **one expression** ending in `.`: a query, for example `total + 1.` or
  `Employee.where(role: "dev").`

Annotations such as `@mixfix(...)` sit directly above the `def` they apply to and end with
their closing bracket (no period); they are part of the declaration. A cell that mixes
declarations and an expression is refused with a message asking you to split it.

To run cell *N*, the extension assembles a program from every declaration cell
above it (and *N* itself, if it is a declaration) and pipes it to
`felidae <notebook>.fxnb.fx --stdin --query "felidae_cell_result()." --metrics-json`.
The cell's expression is wrapped in a generated function, `felidae_cell_result`,
because a bare query is parsed on its own, where a name bound by an earlier `def`
is only an atom; inside the program it resolves to its value.

| Cell | Output |
|---|---|
| expression | its value |
| `def name := …` | the value |
| `def Fact(…).` / `class` | its rows (`Fact.all()`) |
| functions, or several defs | `defined: f, g` |

State between cells lives only in the database (RocksDB), as in the language
itself. Facts seeded by a `def Fact(…).` cell persist when the program loads, so
running cells again is idempotent. A broken declaration cell makes every later
run fail with that cell's error until it is fixed.

## Project folder and `init.fx`

`init.fx` is the project manifest (think `package.json`). Usually it is two lines:

```felidae
import "db".
db.location("build/examples/data").
```

The call ends with a period. The interpreter requires `init.fx` directly beside the
entry program, so a notebook's folder is its project by default, and relative imports
resolve from there. The database path is relative to `init.fx`.

- **Missing `init.fx`:** running a cell offers *Create init.fx here*. It asks where the
  database should be (default `./data.db`), writes the two lines above, and then shows a
  message saying what it created and the full path of the database. It never overwrites
  an existing file and never creates one without asking.
- **A notebook outside the project:** *Felidae: Set Notebook Project Folder* points the
  notebook at another folder that has an `init.fx`. The setting lives in the notebook's own
  metadata (relative to it when saved), so the notebook can sit anywhere, such as Downloads.
- *Felidae: Create init.fx for Notebook* does the same offer from the Command Palette.

Cells run **one at a time**, because RocksDB admits one process per database
directory; a cell that overlaps a run or debug session you started yourself reports
the database lock error.

## Choosing the interpreter

Resolution order: the `felidae.interpreterPath` setting (also `.vscode/felidae.json`),
the `FELIDAE_PATH` environment variable, a build in a known folder (looked for under every
workspace folder and under the notebook's folder and its parents, release before debug,
including `build/debug/x64/Debug/felidae.exe`), then `felidae` on `PATH`. A path you set
is used as it is, even if it does not exist, so a wrong setting fails loudly.

*Felidae: Select Interpreter* lists what it finds, lets you browse, or clear the setting.
The status bar shows the version, and a warning if that build predates `--stdin` runs.

## Seeing what is going on

- **Database status:** the status bar shows the database the project's `init.fx` points at,
  with the full path in its tooltip, or says that `init.fx` is missing or has no
  `db.location` (highlighted, so it is noticed). Click it to open `init.fx`, or to create it.
  It follows the notebook's project folder and changes to `init.fx`.
- **Under each code cell:** a line saying what it is: `declares total, twice`, `query`, or a
  warning (a mixed cell, an expression missing its period, `db.location(...)` lines that belong
  in `init.fx`). You see how a cell will be treated before you run it.
- **`init.fx` lines in a cell** are refused with a pointer to `init.fx`: they are project
  configuration and would do nothing in a cell.
- The notebook toolbar's overflow menu has *Select Interpreter*, *Set Notebook Project
  Folder* and *Open Notebook init.fx*.

## Editing

- **Notebook menu:** the first button of the notebook toolbar (or *Felidae: Notebook Menu*) lists everything in
  one place: run all, clear outputs, REPL, send cell to REPL, init.fx, project folder, interpreter, export, log.
- **Parse errors are underlined** in code cells like in a `.fx` file. The cell is checked
  in the context of the cells above it (so an operator declared above still parses), with
  `felidae --check-json --stdin`: parse only, nothing is executed and the database is not
  opened. A missing period underlines the last character of the cell.
  Cells are checked one at a time, and a cell whose program is unchanged is not checked again,
  so opening a long notebook does not start a process per cell at once.
- **Formatting:** the Felidae formatter applies in cells: use *Format Cell*, or VS Code's
  *Format Notebook* for all of them.
- **Quick fix and hints from the Felidae extension (0.3.0+) apply in cells:** *Insert the missing '.'*
  on a missing-period squiggle, unused locals and parameters shown faded, and the doc comment above a
  function in its hover.
- **REPL:** *Felidae: Open Notebook REPL* starts `felidae --repl` in a terminal in the notebook's project
  folder (it offers to create `init.fx` first if it is missing); *Felidae: Send Cell to REPL* sends the
  selected code cell(s). Declarations in other cells are not loaded for you: send them too, in order.
  The REPL holds the database while it is open, so running a notebook cell then reports the lock error.
- **Highlighting:** code cells use the Felidae grammar. In markdown cells, a fenced
  <code>```felidae</code> block is highlighted too.

## Outputs

- A **value** is beautified: lines longer than 80 columns are broken one element per line
  inside their brackets (only whitespace changes, and anything it cannot parse is left as
  it was), shown highlighted as Felidae, and the plain text is what you copy and what is saved.
- **Rows of facts** also get a table above the value.
- A second output shows the interpreter's **metrics**: the cell's run time (separate from
  loading), work done, database reads and writes, and a hint when a cell scanned the
  whole fact store or wrote to the database. Turn it off with `felidae.notebook.showMetrics`.
- **Errors** name the cell and line.

## Commands and settings

- *Felidae: New Notebook*, *Select Interpreter*, *Set Notebook Project Folder*, *Create
  init.fx for Notebook*, *Open Notebook init.fx*, *Open Notebook REPL*, *Send Cell to REPL*, *Export Notebook as .fx* (declarations as written, prose and
  queries as comments), *Show Notebook Output*.
- Run, Run All, Run Above/Below, Interrupt and Clear Outputs are VS Code's own notebook
  controls. *Run All* stops at the first failing cell; *Interrupt* stops the running process.
- `felidae.notebook.runTimeoutSeconds` (default 60), `felidae.notebook.showMetrics`.

## The `.fxnb` format

```json
{
  "version": 1,
  "metadata": {},
  "cells": [
    { "kind": "markdown", "source": ["# Title\n"] },
    { "kind": "code", "source": ["def total := 40 + 2.\n"], "executionOrder": 1,
      "outputs": [{ "text": "42", "ok": true }] }
  ]
}
```

`source` is an array of lines so notebooks diff line by line. `metadata.projectFolder`
holds the project folder when one is set. A file that is not valid is refused with a reason
and never repaired; an empty file is a new notebook.

## Notes

- Calling an undefined function is not an error in Felidae: it evaluates to a data
  term, so a typo in a cell shows up as a value.
- The cell program is read from stdin, so nothing is written into your project.

## Development

```
npm install
npm test            # tsc, then the plain-Node suites in test/ (the serializer and controller
                    # run against stand-ins for VS Code's notebook classes and for felidae)
npm run compile     # bundle to dist/extension.js
npx vsce package --no-dependencies --skip-license
```

Open this folder in VS Code and press F5 with the Felidae extension installed to try the
notebook in an Extension Development Host. `src/cells.ts`, `src/metrics.ts`, `src/blocks.ts`
and `src/runner.ts` are copies of small pure modules from the Felidae extension, so neither
extension imports the other.
