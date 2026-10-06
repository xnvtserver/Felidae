// Stand-in for `felidae <logical.fx> --stdin --query "expr." [--metrics-json]`:
// the program text arrives on stdin, the expression picks the outcome.
const args = process.argv.slice(2);
const [logical, flag, queryFlag, expression = ""] = args;

if (flag !== "--stdin" || queryFlag !== "--query") {
  process.stderr.write("error: Unknown option: " + (flag !== "--stdin" ? flag : queryFlag) + "\n");
  process.exit(1); // exits without reading stdin, like a real argument error
}

const wantsMetrics = args.includes("--metrics-json");
const metricsBlock = () => process.stderr.write(
  'FELIDAE_METRICS {"loadMs":10.5\n,"executionMs":0.9\n,"queryRuns":1\n,"firstQueryMs":0.8\n,"repeatedQueryAverageMs":0\n,' +
  '"runtime":{"durableStore":true,"clauseAttempts":2,"unificationAttempts":5,"factCandidates":0,"rocksPointReads":0,' +
  '"rocksTypeScans":0,"rocksFullScans":0,"rocksIndexScans":0,"rocksLinkScans":0,"rocksFactRowsScanned":0,' +
  '"rocksIndexRowsScanned":0,"rocksLinksVisited":0,"rocksFactWrites":0,"rocksLinkWrites":0,"solutionMaterializations":1,' +
  '"moduleLoads":1,"parserTokensLexed":50,"streamedModuleMicros":900,"dispatchCacheHits":4,"dispatchCacheMisses":0}}\n');

let program = "";
process.stdin.setEncoding("utf8");
process.stdin.on("data", (chunk) => (program += chunk));
process.stdin.on("end", () => {
  // Loading a broken program fails, whatever the query is.
  if (expression.startsWith("fail") || program.includes("    fail.")) {
    process.stderr.write("error: " + logical + ": boom at line 4, column 7\n");
    process.exit(1);
  }
  if (expression.startsWith("queryfail")) {
    process.stderr.write("error: query went wrong at line 1, column 3\n");
    process.exit(1);
  }
  if (expression.startsWith("hang")) {
    setInterval(() => {}, 1000);
    return;
  }
  const respond = () => {
    console.log("program=" + program.length + " query=" + expression);
    if (wantsMetrics) metricsBlock();
  };
  if (expression.startsWith("slow")) setTimeout(respond, 150);
  else respond();
});
