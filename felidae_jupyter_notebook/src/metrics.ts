// What `felidae --metrics-json` reports, turned into something worth reading.
//
// The interpreter prints one `FELIDAE_METRICS {...}` block to stderr after a
// successful run. The JSON spans several lines (newlines before each comma) but
// is otherwise ordinary. Pure text handling, no vscode import.

export interface CellMetrics {
  // Time spent loading and registering the program file.
  loadMs: number;
  // Time spent running the cell's own expression: the number that matters.
  executionMs: number;
  runtime: Record<string, number | boolean>;
}

const MARKER = "FELIDAE_METRICS";

// Splits a stderr capture into the metrics block (if present and valid) and
// everything else, so metrics never show up as an error message.
export function extractMetrics(stderr: string): { metrics?: CellMetrics; rest: string } {
  const markerAt = stderr.indexOf(MARKER);
  if (markerAt < 0) return { rest: stderr };
  const before = stderr.slice(0, markerAt);
  const after = stderr.slice(markerAt + MARKER.length);
  const open = after.indexOf("{");
  if (open < 0) return { rest: stderr };

  const body = after.slice(open);
  const parse = (text: string): CellMetrics | undefined => {
    try {
      const value = JSON.parse(text) as Partial<CellMetrics>;
      if (typeof value.loadMs !== "number" || typeof value.executionMs !== "number" || typeof value.runtime !== "object" || value.runtime === null) {
        return undefined;
      }
      return value as CellMetrics;
    } catch {
      return undefined;
    }
  };

  const whole = parse(body.trim());
  if (whole) return { metrics: whole, rest: before };
  // Something was written after the block: cut at its closing brace.
  const close = body.lastIndexOf("}");
  const cut = close >= 0 ? parse(body.slice(0, close + 1)) : undefined;
  // The newline that ends the block is not part of what followed it.
  return cut ? { metrics: cut, rest: before + body.slice(close + 1).replace(/^\r?\n/, "") } : { rest: stderr };
}

export interface MetricsSummary {
  // Short facts for a hover or log: time, work, database, parsing.
  lines: string[];
  // Things worth acting on, stated only when the counters show them.
  hints: string[];
}

const ms = (value: number): string => (value < 10 ? value.toFixed(1) : String(Math.round(value)));
const plural = (count: number, one: string, many = one + "s"): string => count + " " + (count === 1 ? one : many);

export function summarizeMetrics(metrics: CellMetrics): MetricsSummary {
  const n = (key: string): number => {
    const value = metrics.runtime[key];
    return typeof value === "number" ? value : 0;
  };
  const lines: string[] = [];
  const hints: string[] = [];

  lines.push("run " + ms(metrics.executionMs) + " ms · load " + ms(metrics.loadMs) + " ms (the file is loaded again for every cell)");
  lines.push(
    plural(n("clauseAttempts"), "clause attempt") + " · " + plural(n("unificationAttempts"), "unification") + " · " +
    plural(n("factCandidates"), "fact candidate") + " · " + plural(n("solutionMaterializations"), "solution")
  );
  const dispatch = n("dispatchCacheHits") + n("dispatchCacheMisses");
  if (dispatch > 0) {
    lines.push("call dispatch cache: " + Math.round((100 * n("dispatchCacheHits")) / dispatch) + "% hits of " + dispatch);
  }

  if (metrics.runtime.durableStore === true) {
    const reads = n("rocksPointReads") + n("rocksTypeScans") + n("rocksIndexScans") + n("rocksFullScans") + n("rocksLinkScans");
    const writes = n("rocksFactWrites") + n("rocksLinkWrites");
    if (reads + writes > 0) {
      lines.push(
        "database reads: " + n("rocksPointReads") + " point, " + n("rocksTypeScans") + " type scan, " + n("rocksIndexScans") +
        " index scan, " + n("rocksFullScans") + " full scan · rows scanned: " + n("rocksFactRowsScanned") + " fact, " +
        n("rocksIndexRowsScanned") + " index · links visited: " + n("rocksLinksVisited")
      );
      lines.push("database writes: " + plural(n("rocksFactWrites"), "fact") + ", " + plural(n("rocksLinkWrites"), "link"));
    }
    if (n("rocksFullScans") > 0) {
      hints.push(
        "Scanned the whole fact store " + plural(n("rocksFullScans"), "time") + " (" + plural(n("rocksFactRowsScanned"), "row") +
        "). An index on the field being filtered avoids that."
      );
    }
    if (writes > 0) {
      hints.push("This cell wrote to the database (" + plural(n("rocksFactWrites"), "fact") + ", " + plural(n("rocksLinkWrites"), "link") + ").");
    }
  }
  lines.push(plural(n("parserTokensLexed"), "token") + " parsed in " + ms(n("streamedModuleMicros") / 1000) + " ms across " + plural(n("moduleLoads"), "module"));
  return { lines, hints };
}
