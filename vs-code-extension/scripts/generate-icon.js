#!/usr/bin/env node
// Renders the Felidae logo to icons/felidae-icon.png, the marketplace icon
// referenced by package.json's "icon" field.
//
//   node scripts/generate-icon.js
//
// The VS Code marketplace requires a PNG (SVG is only allowed for the file
// icon theme), so the shared logo has to be rasterised. That is done with
// @resvg/resvg-js - a real SVG renderer - rather than by hand: it understands
// the whole SVG spec, so the logo can gain curves, gradients or opacity
// without this script needing to know anything about them.
//
// resvg is a devDependency only. It runs here at build time to produce a
// checked-in PNG; nothing is bundled into the packaged extension.
//
// Source of truth is intellij-idea-extension's resources/icons/felidaeLogo.svg,
// which IntelliJ consumes as SVG directly (see META-INF/pluginIcon.svg), so
// both extensions show the same mark.

"use strict";

const fs = require("fs");
const path = require("path");
const { Resvg } = require("@resvg/resvg-js");

const SIZE = 128; // marketplace icon size

function main() {
  const repoRoot = path.resolve(__dirname, "..", "..");
  const source = path.join(
    repoRoot,
    "intellij-idea-extension",
    "src",
    "main",
    "resources",
    "icons",
    "felidaeLogo.svg"
  );
  const destination = path.join(__dirname, "..", "icons", "felidae-icon.png");

  if (!fs.existsSync(source)) {
    console.error(`generate-icon: ${source} not found; leaving the existing icon in place.`);
    process.exit(0);
  }

  const renderer = new Resvg(fs.readFileSync(source, "utf8"), {
    fitTo: { mode: "width", value: SIZE },
    // Transparent outside the logo's rounded rect, so the icon sits cleanly
    // on both light and dark marketplace backgrounds.
    background: "rgba(0,0,0,0)"
  });

  fs.mkdirSync(path.dirname(destination), { recursive: true });
  fs.writeFileSync(destination, renderer.render().asPng());
  console.log(
    `generate-icon: rendered ${path.basename(source)} at ${SIZE}x${SIZE} -> ` +
      `${destination} (${fs.statSync(destination).size} bytes)`
  );
}

main();
