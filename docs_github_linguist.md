# GitHub Linguist And Code Navigation

Felidae source files use the `.fx` extension and are marked in `.gitattributes`:

```gitattributes
*.fx linguist-language=Felidae
examples/*.fx linguist-language=Felidae
```

GitHub will still show `.fx` files as `Other` until `Felidae` is added to
GitHub Linguist upstream. Repository-level `.gitattributes` can map files only
to languages already known by Linguist; it cannot create a new language entry.

The repository now includes a starter Tree-sitter grammar in
`tree-sitter-felidae/`. Tree-sitter is the parser technology GitHub mentions
for code navigation, but GitHub will not automatically load a grammar from this
repository. It is still useful because it gives Felidae a concrete grammar that
can be used by editors and proposed upstream later.

To make GitHub show `Felidae` as a first-class language:

1. Add a Felidae entry to GitHub Linguist's `languages.yml`.
2. Add the `.fx` extension and TextMate grammar scope.
3. Add representative samples under Linguist's samples directory.
4. Submit the change upstream to GitHub Linguist.

To make GitHub code navigation work for Felidae:

1. Mature the `tree-sitter-felidae` grammar.
2. Add highlight and locals/tags queries.
3. Publish or upstream the grammar where GitHub tooling can consume it.
4. Wait for GitHub-side support; `.gitattributes` cannot enable navigation.

Until then, using `linguist-language=Prolog` would provide a known label and
highlighting, but it would misrepresent Felidae as Prolog.
