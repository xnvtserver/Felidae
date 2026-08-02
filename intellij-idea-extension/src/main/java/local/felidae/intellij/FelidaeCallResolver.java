package local.felidae.intellij;

import org.jetbrains.annotations.NotNull;
import org.jetbrains.annotations.Nullable;

import java.util.ArrayList;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Set;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/**
 * The single place a Felidae call name is turned into the named parameters it
 * accepts. Keyword-argument completion and parameter info both go through this
 * so they can never disagree about what a call takes.
 *
 * <p>Resolution order, most to least authoritative:
 * <ol>
 *   <li>{@link FelidaeBuiltinDocs} - parameters derived at build time from the
 *       documented example (see vs-code-extension/scripts/generate-builtin-docs.js,
 *       which produces the shared builtin-docs.json both IDEs bundle).</li>
 *   <li>A text scan of the file's own declarations, which additionally recovers
 *       the type annotation a head writes (`name: string`).</li>
 * </ol>
 *
 * <p>Mirrors {@code resolveCall} / {@code collectHeadParams} in
 * vs-code-extension/src/extension.ts - keep the two in sync.
 */
public final class FelidaeCallResolver {

    /**
     * Declaration head: {@code Name(args)}, {@code Name extend Parent(args)},
     * optionally followed by {@code =>} (method) or {@code .} (dot-terminated
     * fact). Kept identical to DECLARATION_PATTERN in
     * vs-code-extension/src/extension.ts.
     *
     * <p>Two properties matter. The argument list uses bounded nesting rather
     * than a lazy {@code [\s\S]*?}: the lazy form runs across newlines, so a
     * dotless fact - which has no {@code =>} or {@code .} to stop at - made a
     * single match swallow every declaration up to the next terminated one.
     * And it anchors at column 0, because Felidae declarations are always top
     * level; allowing leading whitespace matched indented *calls* such as
     * {@code return (}, {@code system.print(...)} and {@code instanceof(...)}
     * as if they were declarations.
     */
    private static final Pattern DECLARATION = Pattern.compile(
            "(?m)^([A-Za-z_][A-Za-z0-9_:.]*)"
                    + "(?:[ \\t]+extend[ \\t]+([A-Za-z_][A-Za-z0-9_]*))?"
                    + "[ \\t]*\\(((?:[^()]|\\((?:[^()]|\\([^()]*\\))*\\))*)\\)[ \\t]*(=>|\\.|$)");

    /** Leading `name:` of one argument segment; `(?!=)` keeps `:=` out. */
    private static final Pattern PARAM_HEAD =
            Pattern.compile("^([A-Za-z_][A-Za-z0-9_]*)\\s*:(?!=)");

    public record Param(@NotNull String name, @NotNull String type) {
        public @NotNull String label() {
            return type.isEmpty() ? name + ":" : name + ": " + type;
        }
    }

    public record ResolvedCall(
            @NotNull String label,
            @NotNull List<Param> params,
            @NotNull String detail) {
    }

    /**
     * Pattern matching the declaration of one specific name, for callers that
     * only need to locate a single symbol (Go to Declaration) rather than
     * enumerate every declaration.
     *
     * <p>The optional {@code extend Parent} clause is the part that is easy to
     * forget and silently breaks navigation for every inheriting fact - e.g.
     * {@code MortalBeing extend LivingThing(...)}, where the name is not
     * followed directly by {@code (}. Sharing this builder is what keeps that
     * from drifting apart from {@link #DECLARATION} again.
     */
    public static @NotNull Pattern declarationPatternFor(@NotNull String name) {
        String[] parts = name.replace('.', ':').split(":");
        StringBuilder qualified = new StringBuilder();
        for (int i = 0; i < parts.length; i++) {
            if (i > 0) qualified.append("\\s*[:.]\\s*");
            qualified.append(Pattern.quote(parts[i]));
        }
        return Pattern.compile(
                "(?m)^[ \\t]*" + qualified
                        + "(?:\\s+extend\\s+[A-Za-z_][A-Za-z0-9_]*)?"
                        + "\\s*\\(");
    }

    /** One top-level declaration found in a file's text. */
    public record Declaration(
            @NotNull String name,
            @NotNull String argsText,
            boolean isMethod) {
    }

    /**
     * Every method/fact declaration in {@code fileText}. Shared so callers do
     * not each maintain their own copy of the declaration pattern.
     */
    public static @NotNull List<Declaration> declarations(@NotNull String fileText) {
        List<Declaration> declarations = new ArrayList<>();
        Matcher matcher = DECLARATION.matcher(fileText);
        while (matcher.find()) {
            declarations.add(new Declaration(
                    matcher.group(1),
                    matcher.group(3),
                    "=>".equals(matcher.group(4))));
        }
        return declarations;
    }

    private FelidaeCallResolver() {
        throw new AssertionError("FelidaeCallResolver cannot be instantiated.");
    }

    public static @Nullable ResolvedCall resolve(@NotNull String fileText, @NotNull String callName) {
        String normalized = callName.replace('.', ':');

        FelidaeBuiltinDocs.Entry builtin = FelidaeBuiltinDocs.get(normalized);
        if (builtin != null) {
            List<Param> params = new ArrayList<>(builtin.params().size());
            for (FelidaeBuiltinDocs.Param param : builtin.params()) {
                params.add(new Param(param.name(), param.type()));
            }
            return new ResolvedCall(builtin.heading(), params, builtin.description());
        }

        int lastSeparator = normalized.lastIndexOf(':');
        String simpleName = lastSeparator < 0 ? normalized : normalized.substring(lastSeparator + 1);

        Matcher declaration = DECLARATION.matcher(fileText);
        while (declaration.find()) {
            String name = declaration.group(1);
            if (!name.equals(normalized)
                    && !name.equals(simpleName)
                    && !name.replace('.', ':').equals(normalized)) {
                continue;
            }
            boolean isMethod = "=>".equals(declaration.group(4));
            return new ResolvedCall(
                    name,
                    collectHeadParams(declaration.group(3)),
                    (isMethod ? "method " : "fact ") + name);
        }
        return null;
    }

    /**
     * Splits a declaration head's raw {@code (...)} text into its named
     * parameters, keeping the annotation after the {@code :}. Depth-aware, so a
     * nested default like {@code opts: {a: 1, b: 2}} stays a single parameter -
     * a naive comma split would wrongly report {@code b} as its own parameter.
     */
    public static @NotNull List<Param> collectHeadParams(@NotNull String argsText) {
        List<Param> params = new ArrayList<>();
        Set<String> seen = new LinkedHashSet<>();
        int depth = 0;
        int segmentStart = 0;
        for (int i = 0; i <= argsText.length(); i++) {
            boolean atEnd = i == argsText.length();
            char ch = atEnd ? '\0' : argsText.charAt(i);
            if (!atEnd && (ch == '(' || ch == '{' || ch == '[')) {
                depth++;
            } else if (!atEnd && (ch == ')' || ch == '}' || ch == ']')) {
                depth = Math.max(0, depth - 1);
            } else if (atEnd || (ch == ',' && depth == 0)) {
                String segment = argsText.substring(segmentStart, i).trim();
                Matcher head = PARAM_HEAD.matcher(segment);
                if (head.find() && seen.add(head.group(1))) {
                    String type = segment.substring(head.group(0).length()).trim();
                    params.add(new Param(head.group(1), type));
                }
                segmentStart = i + 1;
            }
        }
        return params;
    }

    /** Name-only view, for callers that just label fields. */
    public static @NotNull List<String> collectHeadFields(@NotNull String argsText) {
        List<String> names = new ArrayList<>();
        for (Param param : collectHeadParams(argsText)) names.add(param.name());
        return names;
    }
}
