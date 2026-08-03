#!/usr/bin/env node
// Regenerates ../GeneratedVisualizerAssets.h from template.html plus the
// npm-installed cytoscape/chart.js/heroicons packages in this directory's
// node_modules. Celidae's --html output embeds the result directly, so the
// generated file is self-contained (no network, no external files) even
// though building it requires Node.js once, here, at dev time.
//
// Usage (from src/celidae/webui/):
//   npm install   (first time, or after changing package.json)
//   npm run generate

"use strict";

const fs = require("fs");
const path = require("path");
const { execFileSync } = require("child_process");

const here = __dirname;
const repoRoot = path.resolve(here, "..", "..", "..");
const outPath = path.join(repoRoot, "src", "celidae", "GeneratedVisualizerAssets.h");

const ICONS = {
  __ICON_MAP__: "map",
  __ICON_CIRCLE_STACK__: "circle-stack",
  __ICON_SHARE__: "share",
  __ICON_RECTANGLE_GROUP__: "rectangle-group",
  __ICON_CURSOR__: "cursor-arrow-rays",
  __ICON_SQUARES__: "squares-2x2",
  __ICON_FIT__: "arrows-pointing-out",
  __ICON_DOWNLOAD__: "arrow-down-tray",
  __ICON_SUN__: "sun",
  __ICON_MOON__: "moon",
  __ICON_CHART__: "chart-bar",
  __ICON_CLOCK__: "clock",
  __ICON_SCALE__: "scale",
  __ICON_SPARKLES__: "sparkles"
};

// Every DiagramType in src/celidae/Visualization.h needs a __DATA_<NAME>__
// token, because standaloneHtml() substitutes one per type and throws if a
// token is missing. Listing them here means a type added on the C++ side
// fails this build with a clear message instead of at Celidae runtime.
const DATA_TOKENS = [
  "schema", "graph", "er", "hierarchy", "timeline",
  "stats", "distribution", "comparison", "cluster"
].map(name => `__DATA_${name.toUpperCase()}__`);

function requireModuleFile(relativeFromNodeModules) {
  const full = path.join(here, "node_modules", relativeFromNodeModules);
  if (!fs.existsSync(full)) {
    throw new Error(
      `Missing ${full}.\n` +
      "Run 'npm install' inside src/celidae/webui first " +
      "(requires Node.js - install it from https://nodejs.org if 'npm' is not recognized)."
    );
  }
  return fs.readFileSync(full, "utf8");
}

function loadIcon(name) {
  const svgPath = path.join(here, "node_modules", "heroicons", "24", "outline", `${name}.svg`);
  if (!fs.existsSync(svgPath)) {
    throw new Error(`Missing heroicon '${name}' at ${svgPath}`);
  }
  // Inline-size the icon and drop the id/aria attributes we don't need;
  // currentColor already picks up the button's text color.
  return fs.readFileSync(svgPath, "utf8")
    .replace("<svg ", '<svg class="icon-svg" width="15" height="15" ')
    .replace(/\s+data-slot="icon"/, "")
    .trim();
}

function compileTailwind() {
  const cliEntry = path.join(here, "node_modules", "@tailwindcss", "cli", "dist", "index.mjs");
  if (!fs.existsSync(cliEntry)) {
    throw new Error(
      `Missing ${cliEntry}.\n` +
      "Run 'npm install' inside src/celidae/webui first " +
      "(requires Node.js - install it from https://nodejs.org if 'npm'/'npx' is not recognized)."
    );
  }
  const outCssPath = path.join(here, ".output.css");
  execFileSync(
    process.execPath,
    [cliEntry, "-i", path.join(here, "input.css"), "-o", outCssPath, "--minify"],
    { cwd: here, stdio: ["ignore", "pipe", "pipe"] }
  );
  const css = fs.readFileSync(outCssPath, "utf8");
  fs.rmSync(outCssPath, { force: true });
  return css;
}

function pickRawStringDelimiter(content) {
  const candidates = ["CELIDAEUI", "CELIDAEUI2", "CELIDAEUI3", "CELIDAEWEB", "CELIDAEWEB2"];
  for (const candidate of candidates) {
    if (candidate.length > 16) continue;
    if (!content.includes(`)${candidate}"`)) return candidate;
  }
  throw new Error("no collision-free raw string delimiter found among candidates");
}

function main() {
  const cytoscapeJs = requireModuleFile(path.join("cytoscape", "dist", "cytoscape.min.js"));
  // ECharts replaced Chart.js here. Chart.js covers bar/line well but has no
  // treemap, heatmap, boxplot, parallel-coordinates or grouped-scatter, which
  // are exactly the chart types the distribution/comparison/cluster views
  // are built from - and running two charting libraries in one page to cover
  // one set of views is a cost with no benefit.
  const echartsJs = requireModuleFile(path.join("echarts", "dist", "echarts.min.js"));

  let html = fs.readFileSync(path.join(here, "template.html"), "utf8");

  if (!html.includes("__TAILWIND_CSS__")) throw new Error("Template is missing __TAILWIND_CSS__");
  const tailwindCss = compileTailwind();
  html = html.replace("__TAILWIND_CSS__", () => tailwindCss);

  for (const [token, iconName] of Object.entries(ICONS)) {
    if (!html.includes(token)) throw new Error(`Template is missing placeholder ${token}`);
    html = html.split(token).join(loadIcon(iconName));
  }

  if (!html.includes("__CYTOSCAPE_JS__")) throw new Error("Template is missing __CYTOSCAPE_JS__");
  if (!html.includes("__ECHARTS_JS__")) throw new Error("Template is missing __ECHARTS_JS__");
  // Use a replacer function, not a replacement string: minified library
  // source commonly contains literal "$&"-style sequences, which
  // String.prototype.replace treats as special patterns (inserting the
  // matched substring, etc.) when the replacement argument is a string.
  html = html.replace("__CYTOSCAPE_JS__", () => cytoscapeJs);
  html = html.replace("__ECHARTS_JS__", () => echartsJs);

  for (const token of DATA_TOKENS) {
    if (!html.includes(token)) throw new Error(`Template is missing placeholder ${token}`);
  }

  const delimiter = pickRawStringDelimiter(html);
  const header = `// GENERATED FILE - do not edit by hand.
// Regenerate with (requires Node.js):
//   cd src/celidae/webui && npm install && npm run generate
// Source: src/celidae/webui/template.html + input.css (Tailwind CSS) +
// cytoscape + echarts + heroicons (see src/celidae/webui/package.json for
// exact versions).
#pragma once

namespace Felidae::Celidae {

// The __DATA_<TYPE>__ tokens (one per DiagramType) are substituted by
// standaloneHtml() at runtime with that view's JSON payload, or with a JSON
// null for a view the caller did not ask for.
inline constexpr const char* kVisualizerTemplate = R"${delimiter}(${html})${delimiter}";

} // namespace Felidae::Celidae
`;

  fs.mkdirSync(path.dirname(outPath), { recursive: true });
  fs.writeFileSync(outPath, header, "utf8");
  console.log(`generate-template: wrote ${header.length} chars -> ${outPath}`);
}

main();
