package local.felidae.intellij.editor;

import com.intellij.lang.BracePair;
import com.intellij.lang.PairedBraceMatcher;
import com.intellij.psi.PsiFile;
import com.intellij.psi.tree.IElementType;
import local.felidae.intellij.FelidaeSyntaxHighlighter;
import org.jetbrains.annotations.NotNull;
import org.jetbrains.annotations.Nullable;

public final class FelidaeBraceMatcher
        implements PairedBraceMatcher {

    private static final BracePair[] PAIRS = {
            new BracePair(
                    FelidaeSyntaxHighlighter.LEFT_PAREN,
                    FelidaeSyntaxHighlighter.RIGHT_PAREN,
                    false
            ),
            new BracePair(
                    FelidaeSyntaxHighlighter.LEFT_BRACE,
                    FelidaeSyntaxHighlighter.RIGHT_BRACE,
                    true
            ),
            new BracePair(
                    FelidaeSyntaxHighlighter.LEFT_BRACKET,
                    FelidaeSyntaxHighlighter.RIGHT_BRACKET,
                    false
            )
    };

    @Override
    public BracePair @NotNull [] getPairs() {
        return PAIRS.clone();
    }

    @Override
    public boolean isPairedBracesAllowedBeforeType(
            @NotNull IElementType leftBraceType,
            @Nullable IElementType contextType
    ) {
        return true;
    }

    @Override
    public int getCodeConstructStart(
            @NotNull PsiFile file,
            int openingBraceOffset
    ) {
        return openingBraceOffset;
    }
}