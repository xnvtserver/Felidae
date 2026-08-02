#!/usr/bin/env node
"use strict";
// Checks the IntelliJ plugin's Java scorer against the training pipeline.
//
//   cd ml && node verify-parity-java.js
//
// Requires a JDK on PATH and the plugin's model resources in place
// (./gradlew generateMlModels in intellij-idea-extension). Compiles the two
// scoring classes plus a tiny stdin harness into a temp directory, feeds them
// the same pseudo-random vectors ml/verify-parity.js uses, and compares
// against ml/lib/gbdt.js.

const fs = require("fs");
const os = require("os");
const path = require("path");
const { execFileSync } = require("child_process");
const gbdt = require("./lib/gbdt");

const REPO_ROOT = path.resolve(__dirname, "..");
const IJ = path.join(REPO_ROOT, "intellij-idea-extension");
const SRC = path.join(IJ, "src", "main", "java", "local", "felidae", "intellij", "ml");
const RESOURCES = path.join(IJ, "src", "main", "resources");
const HARNESS = path.join(__dirname, "java", "ParityHarness.java");
const SEPARATOR = process.platform === "win32" ? ";" : ":";

/**
 * Locates the Gson jar the plugin compiles against. FelidaeGbdt reads its
 * model with Gson rather than a hand-rolled parser, so this harness needs it
 * on the classpath too. Preference order: the jar Gradle already packaged
 * into the built plugin, then the Gradle module cache.
 */
function findGsonJar() {
  const packaged = path.join(IJ, "build", "ijx", "felidae-intellij", "lib");
  if (fs.existsSync(packaged)) {
    const hit = fs.readdirSync(packaged).find((name) => /^gson-.*\.jar$/.test(name));
    if (hit) return path.join(packaged, hit);
  }

  const cache = path.join(
    process.env.USERPROFILE || process.env.HOME || "",
    ".gradle", "caches", "modules-2", "files-2.1", "com.google.code.gson", "gson"
  );
  if (!fs.existsSync(cache)) return null;
  for (const version of fs.readdirSync(cache)) {
    const versionDir = path.join(cache, version);
    if (!fs.statSync(versionDir).isDirectory()) continue;
    for (const hash of fs.readdirSync(versionDir)) {
      const jar = path.join(versionDir, hash, `gson-${version}.jar`);
      if (fs.existsSync(jar)) return jar;
    }
  }
  return null;
}

function randomVectors(count, width, seed) {
  let state = seed;
  const next = () => (state = (state * 1103515245 + 12345) & 0x7fffffff) / 0x7fffffff;
  const vectors = [];
  for (let i = 0; i < count; i++) {
    const row = [];
    for (let f = 0; f < width; f++) {
      const draw = next();
      row.push(
        draw < 0.3
          ? Math.floor(next() * 8)
          : draw < 0.6
            ? next()
            : Math.log1p(Math.floor(next() * 400))
      );
    }
    vectors.push(row);
  }
  return vectors;
}

function main() {
  for (const required of [SRC, RESOURCES, HARNESS]) {
    if (!fs.existsSync(required)) {
      console.error(`verify-parity-java: missing ${required}`);
      process.exit(1);
    }
  }
  if (!fs.existsSync(path.join(RESOURCES, "models", "completion-rank.json"))) {
    console.error(
      "verify-parity-java: plugin model resources missing; run " +
        "`./gradlew generateMlModels` in intellij-idea-extension first"
    );
    process.exit(1);
  }

  const outDir = fs.mkdtempSync(path.join(os.tmpdir(), "felidae-parity-"));
  const stubDir = path.join(outDir, "stub", "org", "jetbrains", "annotations");
  fs.mkdirSync(stubDir, { recursive: true });
  for (const name of ["NotNull", "Nullable"]) {
    fs.writeFileSync(
      path.join(stubDir, `${name}.java`),
      "package org.jetbrains.annotations;\n" +
        "import java.lang.annotation.*;\n" +
        "@Retention(RetentionPolicy.CLASS)\n" +
        "@Target({ElementType.METHOD,ElementType.FIELD,ElementType.PARAMETER," +
        "ElementType.LOCAL_VARIABLE,ElementType.TYPE_USE,ElementType.RECORD_COMPONENT})\n" +
        `public @interface ${name} {}\n`
    );
  }

  const gsonJar = findGsonJar();
  if (!gsonJar) {
    console.error("verify-parity-java: gson jar not found; build the plugin first (./gradlew buildPlugin)");
    process.exit(1);
  }

  execFileSync(
    "javac",
    [
      "-cp", gsonJar,
      "-d", outDir,
      path.join(stubDir, "NotNull.java"),
      path.join(stubDir, "Nullable.java"),
      path.join(SRC, "FelidaeGbdt.java"),
      path.join(SRC, "FelidaeMlRanking.java"),
      HARNESS
    ],
    { stdio: "inherit" }
  );

  let failures = 0;
  for (const name of ["completion-rank", "next-param"]) {
    const model = JSON.parse(fs.readFileSync(path.join(__dirname, "models", `${name}.json`), "utf8"));
    const vectors = randomVectors(2000, model.featureNames.length, 987654321);
    const input = vectors.map((v) => `${name}|${v.join(",")}`).join("\n") + "\n";

    const stdout = execFileSync(
      "java",
      ["-cp", [outDir, RESOURCES, gsonJar].join(SEPARATOR), "ParityHarness"],
      { input, encoding: "utf8" }
    );
    const javaScores = stdout.trim().split("\n").map(Number);

    let maxDelta = 0;
    for (let i = 0; i < vectors.length; i++) {
      maxDelta = Math.max(maxDelta, Math.abs(gbdt.predict(model, vectors[i]) - javaScores[i]));
    }
    const ok = maxDelta < 1e-12;
    if (!ok) failures++;
    console.log(
      `  ${name}: ${ok ? "ok" : "MISMATCH"} over ${vectors.length} vectors ` +
        `(max |delta| = ${maxDelta.toExponential(2)})`
    );
  }

  fs.rmSync(outDir, { recursive: true, force: true });
  console.log(failures === 0 ? "java parity: scorer agrees" : `java parity: ${failures} mismatch(es)`);
  process.exit(failures === 0 ? 0 : 1);
}

main();
