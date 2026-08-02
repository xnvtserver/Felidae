// Stub for vscode-languageclient/node: the unit tests exercise pure text
// helpers in extension.js and never start a server, but the real package
// pulls in a large amount of the vscode API at import time.
class LanguageClient {
  constructor() {}
  async start() {}
  async stop() {}
}
module.exports = {
  LanguageClient,
  TransportKind: { stdio: 0, ipc: 1, pipe: 2, socket: 3 },
};
