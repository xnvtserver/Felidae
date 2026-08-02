const Module = require("module");
const originalResolve = Module._resolveFilename;
Module._resolveFilename = function (request, ...rest) {
  if (request === "vscode") return "vscode-stub";
  return originalResolve.call(this, request, ...rest);
};
require.cache["vscode-stub"] = {
  id: "vscode-stub",
  filename: "vscode-stub",
  loaded: true,
  exports: { EndOfLine: { CRLF: 1, LF: 2 } },
};

const path = require("path");
const fs = require("fs");
const { formatFelidaeSource } = require(path.resolve(process.argv[2]));

const files = process.argv.slice(3);
for (const file of files) {
  const original = fs.readFileSync(file, "utf8").replace(/\r\n/g, "\n");
  const formatted = formatFelidaeSource(original);
  const twice = formatFelidaeSource(formatted);
  const identical = formatted === original;
  const idempotent = twice === formatted;
  console.log(`${file}: identical=${identical} idempotent=${idempotent}`);
  if (!identical) {
    const a = original.split("\n"), b = formatted.split("\n");
    for (let i = 0; i < Math.max(a.length, b.length); i++) {
      if (a[i] !== b[i]) {
        console.log(`  line ${i + 1}: ${JSON.stringify(a[i])} -> ${JSON.stringify(b[i])}`);
      }
    }
  }
  if (!idempotent) {
    console.log("  NOT IDEMPOTENT - this is a bug");
  }
}
