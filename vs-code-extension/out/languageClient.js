"use strict";
var __createBinding = (this && this.__createBinding) || (Object.create ? (function(o, m, k, k2) {
    if (k2 === undefined) k2 = k;
    var desc = Object.getOwnPropertyDescriptor(m, k);
    if (!desc || ("get" in desc ? !m.__esModule : desc.writable || desc.configurable)) {
      desc = { enumerable: true, get: function() { return m[k]; } };
    }
    Object.defineProperty(o, k2, desc);
}) : (function(o, m, k, k2) {
    if (k2 === undefined) k2 = k;
    o[k2] = m[k];
}));
var __setModuleDefault = (this && this.__setModuleDefault) || (Object.create ? (function(o, v) {
    Object.defineProperty(o, "default", { enumerable: true, value: v });
}) : function(o, v) {
    o["default"] = v;
});
var __importStar = (this && this.__importStar) || (function () {
    var ownKeys = function(o) {
        ownKeys = Object.getOwnPropertyNames || function (o) {
            var ar = [];
            for (var k in o) if (Object.prototype.hasOwnProperty.call(o, k)) ar[ar.length] = k;
            return ar;
        };
        return ownKeys(o);
    };
    return function (mod) {
        if (mod && mod.__esModule) return mod;
        var result = {};
        if (mod != null) for (var k = ownKeys(mod), i = 0; i < k.length; i++) if (k[i] !== "default") __createBinding(result, mod, k[i]);
        __setModuleDefault(result, mod);
        return result;
    };
})();
Object.defineProperty(exports, "__esModule", { value: true });
exports.isRunning = isRunning;
exports.start = start;
exports.stop = stop;
const fs = __importStar(require("fs"));
const path = __importStar(require("path"));
const vscode = __importStar(require("vscode"));
const node_1 = require("vscode-languageclient/node");
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
let client;
let running = false;
function isRunning() {
    return running;
}
/**
 * Starts the language server.
 *
 * @returns true when the server was launched, false when the extension should
 *          fall back to its in-process providers.
 */
async function start(serverPath, output) {
    if (running)
        return true;
    if (!serverPath || !fs.existsSync(serverPath)) {
        output.appendLine(`felidae: language server not started - '${serverPath}' not found. ` +
            "Using built-in analysis. Set felidae.debugInterpreterPath to enable it.");
        return false;
    }
    const serverOptions = {
        run: { command: serverPath, args: ["--lsp"], transport: node_1.TransportKind.stdio },
        debug: { command: serverPath, args: ["--lsp"], transport: node_1.TransportKind.stdio }
    };
    // start() must not report success when initialization failed: the handler
    // below can run *during* client.start(), and start() does not always reject
    // afterwards, so a plain `running = true` after the await would leave the
    // extension believing a dead server owned diagnostics.
    let initializationFailed = false;
    const clientOptions = {
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
    client = new node_1.LanguageClient("felidae", "Felidae Language Server", serverOptions, clientOptions);
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
    }
    catch (error) {
        output.appendLine(`felidae: could not start language server: ${String(error)}`);
        await stop();
        return false;
    }
}
async function stop() {
    running = false;
    if (!client)
        return;
    const stopping = client;
    client = undefined;
    try {
        await stopping.stop();
    }
    catch {
        // Already dead; nothing useful to do while shutting down.
    }
}
//# sourceMappingURL=languageClient.js.map