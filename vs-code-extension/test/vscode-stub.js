// Minimal `vscode` module stub so extension.js can be imported in plain Node
// for unit-testing its pure text-analysis functions.
class Position {
  constructor(line, character) { this.line = line; this.character = character; }
  translate(dl, dc) { return new Position(this.line + (dl || 0), this.character + (dc || 0)); }
}
class Range {
  constructor(a, b) { this.start = a; this.end = b; }
}
class MarkdownString {
  constructor() { this.value = ""; }
  appendMarkdown(s) { this.value += s; return this; }
  appendCodeblock(s) { this.value += "\n```\n" + s + "\n```\n"; return this; }
}
class SnippetString { constructor(v) { this.value = v; } }
class CompletionItem {
  constructor(label, kind) { this.label = label; this.kind = kind; }
}
class ParameterInformation {
  constructor(label, doc) { this.label = label; this.documentation = doc; }
}
class SignatureInformation {
  constructor(label, doc) { this.label = label; this.documentation = doc; this.parameters = []; }
}
class SignatureHelp {
  constructor() { this.signatures = []; this.activeSignature = 0; this.activeParameter = 0; }
}
class Hover { constructor(contents) { this.contents = contents; } }
class CodeAction { constructor(title, kind) { this.title = title; this.kind = kind; } }
class WorkspaceEdit { replace() {} }
class DocumentSymbol {
  constructor(name, detail, kind, range, selectionRange) {
    Object.assign(this, { name, detail, kind, range, selectionRange, children: [] });
  }
}
class Diagnostic {}
class Location {}
class DocumentLink {}
class FoldingRange {}
class CodeLens {}
class SemanticTokensLegend {}
class EventEmitter { constructor() { this.event = () => ({ dispose() {} }); } fire() {} }
class ThemeIcon {}

const enumProxy = new Proxy({}, { get: (_t, prop) => String(prop) });

module.exports = {
  Position, Range, MarkdownString, SnippetString, CompletionItem,
  ParameterInformation, SignatureInformation, SignatureHelp, Hover,
  CodeAction, WorkspaceEdit, DocumentSymbol, Diagnostic, Location,
  DocumentLink, FoldingRange, CodeLens, SemanticTokensLegend, EventEmitter,
  ThemeIcon,
  CompletionItemKind: enumProxy,
  SymbolKind: enumProxy,
  DiagnosticSeverity: enumProxy,
  CodeActionKind: enumProxy,
  EndOfLine: { CRLF: 2, LF: 1 },
  Uri: { file: (p) => ({ fsPath: p, scheme: "file", toString: () => "file://" + p }) },
  languages: {
    createDiagnosticCollection: () => ({ set() {}, delete() {}, dispose() {} }),
    registerHoverProvider: () => ({ dispose() {} }),
    registerDocumentLinkProvider: () => ({ dispose() {} }),
    registerDefinitionProvider: () => ({ dispose() {} }),
    registerFoldingRangeProvider: () => ({ dispose() {} }),
    registerCodeLensProvider: () => ({ dispose() {} }),
    registerDocumentSemanticTokensProvider: () => ({ dispose() {} }),
    registerDocumentSymbolProvider: () => ({ dispose() {} }),
    registerCompletionItemProvider: () => ({ dispose() {} }),
    registerCodeActionsProvider: () => ({ dispose() {} }),
    registerSignatureHelpProvider: () => ({ dispose() {} }),
    registerDocumentFormattingEditProvider: () => ({ dispose() {} }),
  },
  workspace: {
    textDocuments: [],
    getConfiguration: () => ({ get: (_k, d) => d }),
    onDidOpenTextDocument: () => ({ dispose() {} }),
    onDidChangeTextDocument: () => ({ dispose() {} }),
    onDidSaveTextDocument: () => ({ dispose() {} }),
    onDidCloseTextDocument: () => ({ dispose() {} }),
    getWorkspaceFolder: () => undefined,
    workspaceFolders: undefined,
  },
  window: {
    activeTextEditor: undefined,
    onDidChangeActiveTextEditor: () => ({ dispose() {} }),
    showErrorMessage() {}, showInformationMessage() {}, showWarningMessage() {},
    createOutputChannel: () => ({ appendLine() {}, show() {}, dispose() {} }),
  },
  commands: { registerCommand: () => ({ dispose() {} }), executeCommand() {} },
  debug: {
    registerDebugConfigurationProvider: () => ({ dispose() {} }),
    registerDebugAdapterDescriptorFactory: () => ({ dispose() {} }),
  },
  ProgressLocation: enumProxy,
};
