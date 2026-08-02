#!/usr/bin/env node
// Drives `felidae_debug --lsp` through the real vscode-languageserver-protocol
// stack - the same implementation vscode-languageclient uses underneath.
//
//   node scripts/verify-lsp.js [path/to/felidae_debug[.exe]] [file.fx]
//
// A hand-rolled framing check is not enough: it happily accepts a response the
// protocol library would reject. This connection performs the real
// initialize/initialized handshake and decodes every reply through the
// library's own validators, so passing here means the extension's client will
// connect too. It exists because the server was for a long time emitting
// malformed publishDiagnostics JSON that only a real decoder caught.

"use strict";

const path = require("path");
const fs = require("fs");
const { spawn } = require("child_process");
const {
  createProtocolConnection,
  StreamMessageReader,
  StreamMessageWriter,
  InitializeRequest,
  InitializedNotification,
  DidOpenTextDocumentNotification,
  DidCloseTextDocumentNotification,
  PublishDiagnosticsNotification,
  DocumentSymbolRequest,
  DefinitionRequest,
  ShutdownRequest,
  ExitNotification
} = require("vscode-languageserver-protocol/node");

const repoRoot = path.resolve(__dirname, "..", "..");
const serverPath =
  process.argv[2] || path.join(repoRoot, "build", process.platform === "win32" ? "felidae_debug.exe" : "felidae_debug");
const samplePath =
  process.argv[3] || path.join(repoRoot, "examples", "advanced_mortality_fact_reasoning.fx");

let pass = 0;
let fail = 0;
function check(name, ok, detail) {
  if (ok) {
    pass++;
    console.log("  ok   " + name);
  } else {
    fail++;
    console.log("  FAIL " + name + (detail ? "\n         " + detail : ""));
  }
}

function toUri(file) {
  const resolved = path.resolve(file).replace(/\\/g, "/");
  return "file:///" + resolved.replace(/^\/+/, "");
}

async function main() {
  for (const required of [serverPath, samplePath]) {
    if (!fs.existsSync(required)) {
      console.error(`verify-lsp: missing ${required}`);
      process.exit(2);
    }
  }

  const child = spawn(serverPath, ["--lsp"], { stdio: ["pipe", "pipe", "pipe"] });
  child.stderr.on("data", (d) => console.error("  server stderr:", String(d).trim()));

  const connection = createProtocolConnection(
    new StreamMessageReader(child.stdout),
    new StreamMessageWriter(child.stdin)
  );

  const diagnostics = [];
  connection.onNotification(PublishDiagnosticsNotification.type, (params) => {
    diagnostics.push(params);
  });
  connection.listen();

  const uri = toUri(samplePath);
  const text = fs.readFileSync(samplePath, "utf8");

  const initialize = await connection.sendRequest(InitializeRequest.type, {
    processId: process.pid,
    rootUri: toUri(repoRoot),
    capabilities: {},
    workspaceFolders: null
  });
  const capabilities = initialize.capabilities || {};
  check("initialize decodes through the protocol library", !!initialize.capabilities);
  check("advertises documentSymbolProvider", !!capabilities.documentSymbolProvider);
  check("advertises definitionProvider", !!capabilities.definitionProvider);
  check("reports serverInfo", !!(initialize.serverInfo && initialize.serverInfo.name));

  connection.sendNotification(InitializedNotification.type, {});
  connection.sendNotification(DidOpenTextDocumentNotification.type, {
    textDocument: { uri, languageId: "felidae", version: 1, text }
  });

  // Diagnostics arrive as a notification, so wait for the first one.
  await new Promise((resolve) => {
    const deadline = Date.now() + 15000;
    const timer = setInterval(() => {
      if (diagnostics.length > 0 || Date.now() > deadline) {
        clearInterval(timer);
        resolve();
      }
    }, 50);
  });
  check(
    "publishDiagnostics decodes (this is what the malformed JSON broke)",
    diagnostics.length > 0 && Array.isArray(diagnostics[0].diagnostics),
    diagnostics.length === 0 ? "no notification decoded within 15s" : ""
  );
  if (diagnostics.length > 0) {
    const first = diagnostics[0].diagnostics[0];
    check(
      "diagnostic carries a well-formed range",
      !first || (first.range && typeof first.range.start.line === "number"),
      JSON.stringify(first || {}).slice(0, 140)
    );
  }

  const symbols = await connection.sendRequest(DocumentSymbolRequest.type, {
    textDocument: { uri }
  });
  check("documentSymbol returns entries", Array.isArray(symbols) && symbols.length > 0);
  const named = (symbols || []).map((s) => s.name);
  const withParams = (symbols || []).find((s) => s.detail && s.detail.startsWith("("));
  check("documentSymbol includes parameter detail", !!withParams,
    withParams ? "" : "no symbol carried a (params) detail");
  if (symbols && symbols.length) {
    console.log("       symbols:", named.slice(0, 6).join(", ") + (named.length > 6 ? " ..." : ""));
  }

  // Ask for the definition of a declaration from one of its use sites.
  const lines = text.replace(/\r\n/g, "\n").split("\n");
  const target = named.find((n) => !n.includes(":") && n !== "main");
  let probe;
  for (let i = 0; i < lines.length && target; i++) {
    const at = lines[i].indexOf(target + "(");
    if (at > 0 && /^\s/.test(lines[i])) {
      probe = { line: i, character: at + 1 };
      break;
    }
  }
  if (probe) {
    const definition = await connection.sendRequest(DefinitionRequest.type, {
      textDocument: { uri },
      position: probe
    });
    const location = Array.isArray(definition) ? definition[0] : definition;
    check(
      `definition of '${target}' resolves from a use site`,
      !!(location && location.range && typeof location.range.start.line === "number"),
      JSON.stringify(definition || null).slice(0, 140)
    );
  } else {
    console.log("  skip definition probe (no indented use site found)");
  }

  connection.sendNotification(DidCloseTextDocumentNotification.type, {
    textDocument: { uri }
  });
  await connection.sendRequest(ShutdownRequest.type);
  check("shutdown completes", true);
  connection.sendNotification(ExitNotification.type);

  await new Promise((resolve) => setTimeout(resolve, 300));
  connection.dispose();
  child.kill();

  console.log(`\n${pass} passed, ${fail} failed`);
  process.exit(fail === 0 ? 0 : 1);
}

main().catch((error) => {
  console.error("verify-lsp: " + (error && error.message ? error.message : String(error)));
  process.exit(1);
});
