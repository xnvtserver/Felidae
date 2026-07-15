package local.felidae.intellij;

import com.intellij.openapi.fileTypes.SyntaxHighlighter;
import com.intellij.openapi.fileTypes.SyntaxHighlighterFactory;
import com.intellij.openapi.project.Project;
import com.intellij.openapi.vfs.VirtualFile;
import org.jetbrains.annotations.NotNull;
import org.jetbrains.annotations.Nullable;

/**
 * Creates the syntax highlighter used for Felidae source files.
 */
public final class FelidaeSyntaxHighlighterFactory
        extends SyntaxHighlighterFactory {

    private static final FelidaeSyntaxHighlighter HIGHLIGHTER =
            new FelidaeSyntaxHighlighter();

    @Override
    public @NotNull SyntaxHighlighter getSyntaxHighlighter(
            @Nullable Project project,
            @Nullable VirtualFile virtualFile
    ) {
        return HIGHLIGHTER;
    }
}