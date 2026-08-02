package local.felidae.intellij.editor;

import com.intellij.lang.ASTNode;
import com.intellij.lang.folding.FoldingBuilderEx;
import com.intellij.lang.folding.FoldingDescriptor;
import com.intellij.openapi.editor.Document;
import com.intellij.openapi.project.DumbAware;
import com.intellij.openapi.util.TextRange;
import com.intellij.psi.PsiElement;
import local.felidae.intellij.FelidaeCallResolver;
import local.felidae.intellij.FelidaeFileType;
import org.jetbrains.annotations.NotNull;
import org.jetbrains.annotations.Nullable;

import java.util.ArrayList;
import java.util.List;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/**
 * Code folding for .fx files: each declaration folds to its head, and
 * consecutive comment lines fold to the first one.
 *
 * <p>There is no PSI grammar, so this cannot fold by walking a tree. It folds
 * on text ranges instead, deriving declaration starts from
 * {@link FelidaeCallResolver} - the same pattern completion, parameter info
 * and Go to Declaration use - and ending each region where the next top-level
 * declaration begins.
 */
public final class FelidaeFoldingBuilder extends FoldingBuilderEx implements DumbAware {

    /**
     * A line that starts a new top-level construct, ending the previous fold.
     * The optional {@code extend Parent} clause must be allowed, or a fact
     * written as {@code Child extend Parent(...)} is not seen as starting
     * anything and a whole run of facts collapses into one region.
     */
    private static final Pattern TOP_LEVEL_START = Pattern.compile(
            "^(?:[A-Za-z_][A-Za-z0-9_:.]*(?:[ \\t]+extend[ \\t]+[A-Za-z_][A-Za-z0-9_]*)?[ \\t]*\\("
                    + "|import\\b"
                    + "|[A-Za-z_][A-Za-z0-9_]*[ \\t]*:=)");

    private static final Pattern COMMENT_LINE = Pattern.compile("^[ \\t]*#");

    @Override
    public FoldingDescriptor @NotNull [] buildFoldRegions(
            @NotNull PsiElement root,
            @NotNull Document document,
            boolean quick
    ) {
        if (root.getContainingFile() == null
                || root.getContainingFile().getFileType() != FelidaeFileType.INSTANCE) {
            return FoldingDescriptor.EMPTY_ARRAY;
        }

        String text = document.getText();
        String[] lines = text.split("\n", -1);
        int[] lineStart = new int[lines.length];
        int offset = 0;
        for (int i = 0; i < lines.length; i++) {
            lineStart[i] = offset;
            offset += lines[i].length() + 1;
        }

        List<FoldingDescriptor> descriptors = new ArrayList<>();
        ASTNode node = root.getNode();
        if (node == null) return FoldingDescriptor.EMPTY_ARRAY;

        addDeclarationFolds(text, lines, lineStart, document, node, descriptors);
        addCommentFolds(lines, lineStart, document, node, descriptors);

        return descriptors.toArray(FoldingDescriptor.EMPTY_ARRAY);
    }

    private static void addDeclarationFolds(
            String text,
            String[] lines,
            int[] lineStart,
            Document document,
            ASTNode node,
            List<FoldingDescriptor> descriptors
    ) {
        for (FelidaeCallResolver.Declaration declaration : FelidaeCallResolver.declarations(text)) {
            int headLine = declarationLine(lines, declaration.name());
            if (headLine < 0) continue;

            // The region runs to the last non-blank line before the next
            // top-level construct, so trailing blank separators stay visible.
            int end = headLine;
            for (int i = headLine + 1; i < lines.length; i++) {
                if (TOP_LEVEL_START.matcher(lines[i]).find()) break;
                if (!lines[i].isBlank()) end = i;
            }
            if (end <= headLine) continue; // single-line declaration: nothing to fold

            int startOffset = lineStart[headLine] + lines[headLine].length();
            int endOffset = Math.min(document.getTextLength(), lineStart[end] + lines[end].length());
            if (endOffset <= startOffset) continue;

            descriptors.add(new FoldingDescriptor(
                    node,
                    new TextRange(startOffset, endOffset),
                    null,
                    " ..."));
        }
    }

    private static void addCommentFolds(
            String[] lines,
            int[] lineStart,
            Document document,
            ASTNode node,
            List<FoldingDescriptor> descriptors
    ) {
        int blockStart = -1;
        for (int i = 0; i <= lines.length; i++) {
            boolean isComment = i < lines.length && COMMENT_LINE.matcher(lines[i]).find();
            if (isComment && blockStart < 0) {
                blockStart = i;
            } else if (!isComment && blockStart >= 0) {
                int last = i - 1;
                if (last > blockStart) {
                    int startOffset = lineStart[blockStart] + lines[blockStart].length();
                    int endOffset = Math.min(
                            document.getTextLength(),
                            lineStart[last] + lines[last].length());
                    if (endOffset > startOffset) {
                        descriptors.add(new FoldingDescriptor(
                                node,
                                new TextRange(startOffset, endOffset),
                                null,
                                " ..."));
                    }
                }
                blockStart = -1;
            }
        }
    }

    /** First line that declares {@code name} at column 0. */
    private static int declarationLine(String[] lines, String name) {
        Matcher matcher = FelidaeCallResolver.declarationPatternFor(name).matcher("");
        for (int i = 0; i < lines.length; i++) {
            if (lines[i].isEmpty() || Character.isWhitespace(lines[i].charAt(0))) continue;
            matcher.reset(lines[i]);
            if (matcher.find()) return i;
        }
        return -1;
    }

    @Override
    public @Nullable String getPlaceholderText(@NotNull ASTNode node) {
        return " ...";
    }

    @Override
    public boolean isCollapsedByDefault(@NotNull ASTNode node) {
        return false;
    }
}
