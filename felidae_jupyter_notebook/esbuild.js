// Bundles src/extension.ts and its local modules into one dist/extension.js,
// the package.json "main". `vscode` stays external: the host injects it.
//
// Usage: node esbuild.js [--watch] [--production]
const esbuild = require("esbuild");
const path = require("path");

const production = process.argv.includes("--production");
const watch = process.argv.includes("--watch");

async function main() {
  const ctx = await esbuild.context({
    absWorkingDir: __dirname,
    entryPoints: [path.join(__dirname, "src", "extension.ts")],
    bundle: true,
    outfile: path.join(__dirname, "dist", "extension.js"),
    external: ["vscode"],
    platform: "node",
    format: "cjs",
    target: "node18",
    sourcemap: !production,
    minify: production,
    logLevel: "info",
  });
  if (watch) {
    await ctx.watch();
  } else {
    await ctx.rebuild();
    await ctx.dispose();
  }
}

main().catch((error) => {
  console.error(error);
  process.exit(1);
});
