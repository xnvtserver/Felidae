const path = require("path");
const { CellRunner, describeCellRun } = require(path.resolve(__dirname, "..", "out", "runner.js"));
const { planRun, mapProgramErrors } = require(path.resolve(__dirname, "..", "out", "program.js"));

let pass = 0, fail = 0;
const check = (name, actual, expected) => {
  const a = JSON.stringify(actual), e = JSON.stringify(expected);
  if (a === e) { pass++; console.log("  ok  ", name); } else { fail++; console.log("  FAIL", name, "\n     exp", e, "\n     act", a); }
};

const FAKE = path.resolve(__dirname, "fake-felidae.js");
const LOGICAL = path.join(__dirname, "analysis.fxnb.fx");
const options = (expression, stdin = "", extra = {}) => ({
  command: process.execPath,
  args: [FAKE, LOGICAL, "--stdin", "--query", expression, "--metrics-json"],
  cwd: __dirname,
  timeoutMs: 5000,
  stdin,
  ...extra
});

(async () => {
  const runner = new CellRunner();

  const ok = await runner.run(options("total.", "def total := 1.\n"));
  check("the program arrives on stdin and the query on the command line", ok.stdout.trim(), "program=16 query=total.");
  check("a successful run is ok with exit code 0", [ok.ok, ok.exitCode], [true, 0]);
  check("the metrics block is taken from stderr", [ok.metrics.executionMs, ok.stderr], [0.9, ""]);
  check("a run is described by its output", describeCellRun(ok, 5000), { ok: true, text: "program=16 query=total." });

  const big = "def filler := 1.\n".repeat(20000);
  const large = await runner.run(options("big.", big));
  check("a large program is delivered completely without blocking", large.stdout.trim(), "program=" + big.length + " query=big.");

  const empty = await runner.run(options("only.", ""));
  check("an empty program (no declarations above) is fine", empty.stdout.trim(), "program=0 query=only.");

  // A failure inside the assembled program is mapped back to the cell it came from.
  const cells = [
    { kind: "code", source: "def total := 1." },
    { kind: "code", source: "def twice(value: number) =>\n    value * 2.\nend" },
    { kind: "code", source: "fail." }
  ];
  const plan = planRun(cells, 2);
  const failed = await runner.run(options(plan.query, plan.program));
  const described = describeCellRun(failed, 5000);
  check("a failing run reports the interpreter's error", [failed.ok, failed.exitCode, described.ok], [false, 1, false]);
  check("the error names the cell and line it came from",
    mapProgramErrors(described.text, plan.lineMap, LOGICAL), "boom at cell 2, line 2, column 7");

  const queryFailed = await runner.run(options("queryfail.", "def a := 1.\n"));
  check("an error about the query is shown as the interpreter wrote it",
    mapProgramErrors(describeCellRun(queryFailed, 5000).text, [], LOGICAL), "query went wrong at line 1, column 3");

  // The process may exit without reading stdin; that is its failure to report.
  const rejected = await runner.run({ command: process.execPath, args: [FAKE, LOGICAL, "--nope", "x."], cwd: __dirname, timeoutMs: 5000, stdin: "def a := 1.\n".repeat(50000) });
  check("an interpreter that rejects its arguments before reading stdin does not crash the runner",
    [rejected.ok, describeCellRun(rejected, 5000).text], [false, "Unknown option: --nope"]);

  // One felidae process at a time.
  const order = [];
  await Promise.all([
    runner.run(options("slow.", "")).then(() => order.push("slow")),
    runner.run(options("quick.", "")).then(() => order.push("quick"))
  ]);
  check("a queued cell starts only after the one before it finished", order, ["slow", "quick"]);

  const hung = await new CellRunner().run(options("hang.", "", { timeoutMs: 150 }));
  check("a hung cell is stopped at the timeout", [hung.ok, hung.timedOut, describeCellRun(hung, 150).text.includes("0.15 s")], [false, true, true]);

  const cancelling = new CellRunner();
  const pending = cancelling.run(options("hang.", ""));
  await new Promise((resolve) => setTimeout(resolve, 100));
  check("a running cell reports running", cancelling.running, true);
  cancelling.cancel();
  const cancelled = await pending;
  check("an interrupted cell is reported as stopped", [cancelled.cancelled, describeCellRun(cancelled, 5000).text], [true, "Stopped."]);
  check("nothing is left running afterwards", cancelling.running, false);

  const missing = await new CellRunner().run({ command: "definitely-not-a-real-felidae", args: [], cwd: __dirname, timeoutMs: 5000, stdin: "x" });
  check("a missing executable is a failed run with a message, not an exception", [missing.ok, missing.stderr.length > 0], [false, true]);

  console.log(`${pass} passed, ${fail} failed`);
  process.exit(fail ? 1 : 0);
})();
