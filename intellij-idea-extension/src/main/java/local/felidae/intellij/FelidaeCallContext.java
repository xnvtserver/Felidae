package local.felidae.intellij;

import org.jetbrains.annotations.NotNull;
import org.jetbrains.annotations.Nullable;

import java.util.LinkedHashSet;
import java.util.Set;

/**
 * Locates the call whose argument list an offset sits inside, and describes
 * what has already been written in it. There is no PSI grammar for .fx files,
 * so this walks raw document text - the same constraint every other Felidae
 * IntelliJ feature works under.
 *
 * <p>Mirrors {@code enclosingCall} / {@code callArgumentState} in
 * vs-code-extension/src/extension.ts.
 */
public final class FelidaeCallContext {

    private final String callName;
    private final int openParenOffset;
    private final int activeParameter;
    private final Set<String> suppliedKeys;
    private final String keyBeingTyped;

    private FelidaeCallContext(
            String callName,
            int openParenOffset,
            int activeParameter,
            Set<String> suppliedKeys,
            String keyBeingTyped) {
        this.callName = callName;
        this.openParenOffset = openParenOffset;
        this.activeParameter = activeParameter;
        this.suppliedKeys = suppliedKeys;
        this.keyBeingTyped = keyBeingTyped;
    }

    public @NotNull String callName() {
        return callName;
    }

    public int openParenOffset() {
        return openParenOffset;
    }

    /** Zero-based index of the comma-separated slot the caret is in. */
    public int activeParameter() {
        return activeParameter;
    }

    /** Keys already named earlier in this call, e.g. `left` in `f(left: 1, |)`. */
    public @NotNull Set<String> suppliedKeys() {
        return suppliedKeys;
    }

    /** The key of the slot being typed right now, or null if none yet. */
    public @Nullable String keyBeingTyped() {
        return keyBeingTyped;
    }

    /**
     * Returns the call context at {@code offset}, or null when the caret is not
     * inside a call's parentheses.
     */
    public static @Nullable FelidaeCallContext at(@NotNull CharSequence text, int offset) {
        int limit = Math.min(offset, text.length());
        int openParen = findEnclosingOpenParen(text, limit);
        if (openParen < 0) return null;

        String name = readCallNameBefore(text, openParen);
        if (name == null) return null;

        int activeParameter = 0;
        int slotStart = openParen + 1;
        int depth = 0;
        boolean inString = false;
        Set<String> supplied = new LinkedHashSet<>();
        for (int i = openParen + 1; i < limit; i++) {
            char ch = text.charAt(i);
            if (inString) {
                if (ch == '\\') i++;
                else if (ch == '"') inString = false;
                continue;
            }
            if (ch == '"') {
                inString = true;
            } else if (ch == '(' || ch == '{' || ch == '[') {
                depth++;
            } else if (ch == ')' || ch == '}' || ch == ']') {
                depth--;
            } else if (ch == ',' && depth == 0) {
                activeParameter++;
                slotStart = i + 1;
            } else if (ch == ':' && depth == 0 && (i + 1 >= limit || text.charAt(i + 1) != '=')) {
                String key = identifierEndingAt(text, i, slotStart);
                if (key != null) supplied.add(key);
            }
        }

        String typed = leadingIdentifier(text, slotStart, limit);
        // A slot whose key is already committed (`left: `) is not "being typed".
        if (typed != null && supplied.contains(typed)) {
            int after = slotStart + typed.length();
            while (after < limit && Character.isWhitespace(text.charAt(after))) after++;
            if (after < limit && text.charAt(after) == ':') typed = null;
        }

        return new FelidaeCallContext(name, openParen, activeParameter, supplied, typed);
    }

    /** Scans back for the `(` that is still open at {@code limit}. */
    private static int findEnclosingOpenParen(CharSequence text, int limit) {
        int depth = 0;
        for (int i = limit - 1; i >= 0; i--) {
            char ch = text.charAt(i);
            if (ch == '"') {
                // Skip back over a string literal so its contents never count.
                int j = i - 1;
                while (j >= 0 && !(text.charAt(j) == '"' && (j == 0 || text.charAt(j - 1) != '\\'))) j--;
                if (j < 0) return -1;
                i = j;
                continue;
            }
            if (ch == ')' || ch == '}' || ch == ']') {
                depth++;
            } else if (ch == '(') {
                if (depth == 0) return i;
                depth--;
            } else if (ch == '{' || ch == '[') {
                if (depth == 0) return -1; // inside a map/array literal, not a call
                depth--;
            } else if (ch == '\n' && depth == 0) {
                // Keep scanning: Felidae call arguments routinely span lines.
                continue;
            }
        }
        return -1;
    }

    /** Reads the possibly dotted/namespaced call name ending at {@code parenOffset}. */
    private static @Nullable String readCallNameBefore(CharSequence text, int parenOffset) {
        int end = parenOffset;
        while (end > 0 && Character.isWhitespace(text.charAt(end - 1))) end--;
        int start = end;
        while (start > 0) {
            char ch = text.charAt(start - 1);
            if (Character.isLetterOrDigit(ch) || ch == '_' || ch == '.' || ch == ':') start--;
            else break;
        }
        if (start >= end) return null;
        String name = text.subSequence(start, end).toString();
        // `foo(` is a call; `(` after an operator or nothing is a group.
        if (name.isEmpty() || !isIdentifierStart(name.charAt(0))) return null;
        return name;
    }

    private static boolean isIdentifierStart(char ch) {
        return Character.isLetter(ch) || ch == '_';
    }

    /** The identifier immediately before {@code colonOffset}, if any. */
    private static @Nullable String identifierEndingAt(CharSequence text, int colonOffset, int notBefore) {
        int end = colonOffset;
        while (end > notBefore && Character.isWhitespace(text.charAt(end - 1))) end--;
        int start = end;
        while (start > notBefore) {
            char ch = text.charAt(start - 1);
            if (Character.isLetterOrDigit(ch) || ch == '_') start--;
            else break;
        }
        if (start >= end) return null;
        char first = text.charAt(start);
        if (!isIdentifierStart(first)) return null;
        return text.subSequence(start, end).toString();
    }

    /** First identifier at or after {@code from}, bounded by {@code limit}. */
    private static @Nullable String leadingIdentifier(CharSequence text, int from, int limit) {
        int i = from;
        while (i < limit && Character.isWhitespace(text.charAt(i))) i++;
        if (i >= limit || !isIdentifierStart(text.charAt(i))) return null;
        int start = i;
        while (i < limit) {
            char ch = text.charAt(i);
            if (Character.isLetterOrDigit(ch) || ch == '_') i++;
            else break;
        }
        return text.subSequence(start, i).toString();
    }
}
