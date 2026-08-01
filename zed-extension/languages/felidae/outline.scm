; Top-level clause and global-binding declarations, for Zed's outline/
; symbol panel and "go to symbol" - the same declarations
; vs-code-extension's FelidaeDocumentSymbolProvider and
; intellij-idea-extension's FelidaeStdlibIndex surface.

(clause
  head: (call
    function: (qualified_identifier) @name)) @item

(global_binding
  name: (identifier) @name) @item
