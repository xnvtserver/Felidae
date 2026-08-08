#!/usr/bin/env node
// Regenerates runtime assets from template.html, its two
// stylesheets (input.css via Tailwind, template.css verbatim), plus the
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
const os = require("os");
const { execFileSync } = require("child_process");

const here = __dirname;
const repoRoot = path.resolve(here, "..", "..", "..");
const outputDir = process.env.FELIDAE_ASSET_OUTPUT_DIR || path.join(repoRoot, "src", "celidae", "webui");
const outHtmlPath = path.join(outputDir, "visualizer.template.html");
const outCssPath = path.join(outputDir, "visualizer.css");

const ICONS = {
  __ICON_MAP__: "map",
  __ICON_CIRCLE_STACK__: "circle-stack",
  __ICON_SHARE__: "share",
  __ICON_RECTANGLE_GROUP__: "rectangle-group",
  __ICON_CURSOR__: "cursor-arrow-rays",
  __ICON_SQUARES__: "squares-2x2",
  __ICON_FIT__: "arrows-pointing-out",
  __ICON_DOWNLOAD__: "arrow-down-tray",
  // The sun/moon pair went with the old two-state light/dark toggle. Themes
  // are now a named list in a <select>, and an inline SVG inside an <option>
  // is not rendered by browsers anyway.
  __ICON_CHART__: "chart-bar",
  __ICON_CLOCK__: "clock",
  __ICON_SCALE__: "scale",
  __ICON_SPARKLES__: "sparkles"
};

// Every DiagramType in src/celidae/Visualization.h needs a placeholder, since
// standaloneHtml() renders one per type. Listing them here means a type added
// on the C++ side fails this build with a clear message, rather than producing
// a page with one permanently empty view at Celidae runtime.
//
// The delimiters are inja's, configured to <# #> in standaloneHtml(). Its
// default {{ }} appears thousands of times inside the minified CSS and JS this
// file inlines, and would be parsed as template syntax and corrupted.
const DATA_TOKENS = [
  "schema", "graph", "er", "hierarchy", "timeline",
  "stats", "distribution", "comparison", "cluster"
].map(name => `<# data.${name} #>`);

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
  // OneDrive can briefly lock files created beside the source tree. Generate
  // the transient compiler output in the OS temp directory instead.
  const outCssPath = path.join(os.tmpdir(), `felidae-celidae-${process.pid}.css`);
  execFileSync(
    process.execPath,
    [cliEntry, "-i", path.join(here, "input.css"), "-o", outCssPath, "--minify"],
    { cwd: here, stdio: ["ignore", "pipe", "pipe"] }
  );
  const css = fs.readFileSync(outCssPath, "utf8");
  fs.rmSync(outCssPath, { force: true });
  return css;
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

  // template.css is plain CSS and is inlined verbatim: it defines the theme
  // custom properties and styles elements cytoscape and ECharts create at
  // runtime, neither of which Tailwind can generate utilities for. input.css
  // is the Tailwind source and goes through the compiler above.
  if (!html.includes("__TEMPLATE_CSS__")) throw new Error("Template is missing __TEMPLATE_CSS__");
  const templateCssPath = path.join(here, "template.css");
  if (!fs.existsSync(templateCssPath)) {
    throw new Error(`Missing ${templateCssPath}`);
  }
  const templateCss = fs.readFileSync(templateCssPath, "utf8");
  html = html.replace("<style>__TAILWIND_CSS__</style>\n<style>__TEMPLATE_CSS__</style>",
    "<style>__CELIDAE_CSS__</style>");
  if (!html.includes("__CELIDAE_CSS__")) throw new Error("Template is missing CSS asset placeholder");

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

  fs.mkdirSync(path.dirname(outHtmlPath), { recursive: true });
  fs.writeFileSync(outHtmlPath, html, "utf8");
  fs.writeFileSync(outCssPath, `${tailwindCss}\n${templateCss}`, "utf8");
  console.log(`generate-template: wrote ${html.length} chars -> ${outHtmlPath}`);
  console.log(`generate-template: wrote ${tailwindCss.length + templateCss.length} chars -> ${outCssPath}`);
}

main();
