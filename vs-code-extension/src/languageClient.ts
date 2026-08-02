import * as fs from "fs";
import * as path from "path";
import * as vscode from "vscode";
import {
  LanguageClient,
  LanguageClientOptions,
  ServerOptions,
  TransportKind
} from "vscode-languageclient/node";

// Runs `felidae_debug --lsp` as a real language server.
//
// Before this, diagnostics were produced by spawning `felidae_debug
// --check-json` once per debounced edit: a new process per keystroke burst,
// no incremental state, and no way for the binary to serve anything else.
// The server now owns diagnostics, document symbols and go-to-definition over
// one long-lived connection, computed from the actual parse rather than from
// each editor re-deriving structure with its own regexes.
//
// The client is strictly optional. If the executable is missing or fails to
// start, `start()` reports false and the extension keeps its previous
// text-scanning providers, so a workspace without a built felidae_debug still
// gets every feature it had before.

let client: LanguageClient | undefined;
let running = false;

export function isRunning(): boolean {
  return running;
}

/**
 * Starts the language server.
 *
 * @returns true when the server was launched, false when the extension should
 *          fall back to its in-process providers.
 */
export async function start(serverPath: string, output: vscode.OutputChannel): Promise<boolean> {
  if (running) return true;
  if (!serverPath || !fs.existsSync(serverPath)) {
    output.appendLine(
      `felidae: language server not started - '${serverPath}' not found. ` +
        "Using built-in analysis. Set felidae.debugInterpreterPath to enable it."
    );
    return false;
  }

  const serverOptions: ServerOptions = {
    run: { command: serverPath, args: ["--lsp"], transport: TransportKind.stdio },
    debug: { command: serverPath, args: ["--lsp"], transport: TransportKind.stdio }
  };

  // start() must not report success when initialization failed: the handler
  // below can run *during* client.start(), and start() does not always reject
  // afterwards, so a plain `running = true` after the await would leave the
  // extension believing a dead server owned diagnostics.
  let initializationFailed = false;

  const clientOptions: LanguageClientOptions = {
    documentSelector: [{ scheme: "file", language: "felidae" }],
    // The server resolves imports relative to the file, so watching .fx keeps
    // diagnostics fresh when an imported file changes outside the editor.
    synchronize: {
      fileEvents: vscode.workspace.createFileSystemWatcher("**/*.fx")
    },
    outputChannel: output,
    // A failed server must not take the rest of the extension down with it.
    initializationFailedHandler: (error) => {
      output.appendLine(`felidae: language server failed to initialize: ${error}`);
      initializationFailed = true;
      return false;
    }
  };

  client = new LanguageClient("felidae", "Felidae Language Server", serverOptions, clientOptions);

  try {
    await client.start();
    if (initializationFailed) {
      output.appendLine("felidae: falling back to built-in analysis.");
      await stop();
      return false;
    }
    running = true;
    output.appendLine(`felidae: language server started (${path.basename(serverPath)} --lsp)`);
    return true;
  } catch (error) {
    output.appendLine(`felidae: could not start language server: ${String(error)}`);
    await stop();
    return false;
  }
}

export async function stop(): Promise<void> {
  running = false;
  if (!client) return;
  const stopping = client;
  client = undefined;
  try {
    await stopping.stop();
  } catch {
    // Already dead; nothing useful to do while shutting down.
  }
}
