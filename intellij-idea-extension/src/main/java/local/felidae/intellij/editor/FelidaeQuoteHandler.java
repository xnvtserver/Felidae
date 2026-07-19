package local.felidae.intellij.editor;

import com.intellij.codeInsight.editorActions.SimpleTokenSetQuoteHandler;
import local.felidae.intellij.FelidaeSyntaxHighlighter;

public final class FelidaeQuoteHandler
        extends SimpleTokenSetQuoteHandler {

    public FelidaeQuoteHandler() {
        super(
                FelidaeSyntaxHighlighter.STRING,
                FelidaeSyntaxHighlighter.UNTERMINATED_STRING
        );
    }
}