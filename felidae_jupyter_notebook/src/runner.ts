// Runs one notebook cell as one ordinary felidae process:
//
//     felidae notebook.fxnb.fx --stdin --query "expression."
//
// The program text (the declaration cells above) is written to the process's
// stdin; the file argument is only its logical name. Adapted from the Felidae
// extension's cellRunner.ts.
//
// Nothing stays alive between cells and nothing is retried: a process that
// fails reports its own error and that is shown as it is. Runs are queued so
// that only one cell process exists at a time, because RocksDB admits a single
// process per database directory (a second one would fail on the lock).
//
// No vscode import, so it can be tested against a stand-in executable.

import * as childProcess from "child_process";
import { CellMetrics, extractMetrics } from "./metrics";

export interface CellRunOptions {
  command: string;
  args: string[];
  cwd: string;
  timeoutMs: number;
  // Written to the process's stdin, which is then closed.
  stdin?: string;
}

export interface CellRunResult {
  // True only for exit code 0.
  ok: boolean;
  stdout: string;
  stderr: string;
  exitCode: number | null;
  elapsedMs: number;
  timedOut: boolean;
  cancelled: boolean;
  // From --metrics-json, when the process printed them; stripped from stderr.
  metrics?: CellMetrics;
}

export class CellRunner {
  private tail: Promise<unknown> = Promise.resolve();
  private current?: childProcess.ChildProcess;
  private cancelRequested = false;

  get running(): boolean {
    return this.current !== undefined;
  }

  run(options: CellRunOptions): Promise<CellRunResult> {
    const task = this.tail.then(() => this.runNow(options));
    this.tail = task.catch(() => undefined);
    return task;
  }

  // Stops the cell process that is running now; queued cells still run.
  cancel(): void {
    if (!this.current) return;
    this.cancelRequested = true;
    this.current.kill();
  }

  private runNow(options: CellRunOptions): Promise<CellRunResult> {
    return new Promise<CellRunResult>((resolve) => {
      const started = Date.now();
      let stdout = "";
      let stderr = "";
      let timedOut = false;
      let finished = false;
      this.cancelRequested = false;

      const finish = (exitCode: number | null) => {
        if (finished) return;
        finished = true;
        clearTimeout(timer);
        const cancelled = this.cancelRequested;
        this.current = undefined;
        this.cancelRequested = false;
        const extracted = extractMetrics(stderr);
        resolve({
          ok: exitCode === 0 && !timedOut && !cancelled,
          stdout,
          stderr: extracted.rest,
          metrics: extracted.metrics,
          exitCode,
          elapsedMs: Date.now() - started,
          timedOut,
          cancelled
        });
      };

      const child = childProcess.spawn(options.command, options.args, { cwd: options.cwd, windowsHide: true });
      this.current = child;
      const timer = setTimeout(() => {
        timedOut = true;
        child.kill();
      }, options.timeoutMs);
      // A process that exits without reading stdin (bad arguments) closes the
      // pipe; that is its failure to report, not ours.
      child.stdin.on("error", () => undefined);
      child.stdin.end(options.stdin ?? "");
      child.stdout.on("data", (data: Buffer) => (stdout += data.toString()));
      child.stderr.on("data", (data: Buffer) => (stderr += data.toString()));
      child.on("error", (error: Error) => {
        stderr += error.message;
        finish(null);
      });
      child.on("close", (code: number | null) => finish(code));
    });
  }
}

// The text to show for a finished cell: its output when it succeeded, its own
// error message when it failed.
export function describeCellRun(run: CellRunResult, timeoutMs: number): { ok: boolean; text: string } {
  if (run.cancelled) return { ok: false, text: "Stopped." };
  if (run.timedOut) return { ok: false, text: "Did not finish within " + timeoutMs / 1000 + " s; the process was stopped." };
  if (run.ok) return { ok: true, text: run.stdout.trimEnd() };
  const message = (run.stderr.trim() || run.stdout.trim() || "The cell failed (exit code " + run.exitCode + ").")
    .replace(/^error:\s*/i, "");
  return { ok: false, text: message };
}
