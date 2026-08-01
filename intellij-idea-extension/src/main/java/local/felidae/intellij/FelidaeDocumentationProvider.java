package local.felidae.intellij;

import com.intellij.lang.documentation.AbstractDocumentationProvider;
import com.intellij.openapi.editor.Editor;
import com.intellij.openapi.util.Key;
import com.intellij.psi.PsiElement;
import com.intellij.psi.PsiFile;
import org.jetbrains.annotations.NotNull;
import org.jetbrains.annotations.Nullable;

/**
 * Quick Documentation (Ctrl+Q / hover) for Felidae stdlib calls, backed by
 * the same content the VS Code extension shows (FelidaeBuiltinDocs, bundled
 * from the shared repo-root docs/builtin-docs.json).
 *
 * There is no PSI grammar for Felidae, so .fx files have no meaningful
 * per-token PsiElement to resolve a documentation target from. Instead this
 * resolves the identifier under the caret directly from the editor text
 * (mirroring FelidaeExternalAnnotator's raw-offset approach) and stashes the
 * resolved doc key as user data on the context element so generateDoc can
 * read it back.
 */
public final class FelidaeDocumentationProvider extends AbstractDocumentationProvider {

    private static final Key<String> DOC_KEY = Key.create("felidae.documentation.key");

    @Override
    public @Nullable PsiElement getCustomDocumentationElement(
            @NotNull Editor editor,
            @NotNull PsiFile file,
            @Nullable PsiElement contextElement,
            int targetOffset
    ) {
        if (contextElement == null) {
            return null;
        }
        String key = resolveKey(editor.getDocument().getCharsSequence(), targetOffset);
        if (key == null) {
            return null;
        }
        contextElement.putUserData(DOC_KEY, key);
        return contextElement;
    }

    @Override
    public @Nullable String generateDoc(@NotNull PsiElement element, @Nullable PsiElement originalElement) {
        String key = element.getUserData(DOC_KEY);
        if (key == null) {
            return null;
        }
        FelidaeBuiltinDocs.Entry entry = FelidaeBuiltinDocs.get(key);
        if (entry == null) {
            return null;
        }
        return "<div class='definition'><b>" + escape(entry.heading()) + "</b></div>" +
                "<div class='content'>" + escape(entry.description()) + "</div>" +
                "<div class='content'><pre>" + escape(entry.example()) + "</pre></div>";
    }

    private static @Nullable String resolveKey(@NotNull CharSequence text, int offset) {
        int start = Math.max(0, Math.min(offset, text.length()));
        int end = start;
        while (start > 0 && isNameChar(text.charAt(start - 1))) start--;
        while (end < text.length() && isNameChar(text.charAt(end))) end++;
        if (start >= end) {
            return null;
        }
        String raw = text.subSequence(start, end).toString().replaceAll("^[.:]+|[.:]+$", "");
        if (raw.isEmpty()) {
            return null;
        }
        String normalized = raw.replace('.', ':');
        if (FelidaeBuiltinDocs.contains(normalized)) {
            return normalized;
        }
        return FelidaeBuiltinDocs.contains(raw) ? raw : null;
    }

    private static boolean isNameChar(char ch) {
        return Character.isLetterOrDigit(ch) || ch == '_' || ch == ':' || ch == '.';
    }

    private static @NotNull String escape(@NotNull String value) {
        return value
                .replace("&", "&amp;")
                .replace("<", "&lt;")
                .replace(">", "&gt;");
    }
}
