package local.felidae.intellij;

import com.intellij.psi.tree.IElementType;
import org.jetbrains.annotations.NotNull;

/**
 * Shared token definitions used by the Felidae lexer,
 * syntax highlighter, quote handler and brace matcher.
 */
public final class FelidaeTokenTypes {

    public static final IElementType COMMENT =
            token("COMMENT");

    public static final IElementType STRING =
            token("STRING");

    public static final IElementType NUMBER =
            token("NUMBER");

    public static final IElementType KEYWORD =
            token("KEYWORD");

    public static final IElementType OPERATOR =
            token("OPERATOR");

    public static final IElementType LIBRARY =
            token("LIBRARY");

    public static final IElementType IDENTIFIER =
            token("IDENTIFIER");

    public static final IElementType COMMA =
            token("COMMA");

    public static final IElementType COLON =
            token("COLON");

    public static final IElementType DOT =
            token("DOT");

    public static final IElementType QUESTION_MARK =
            token("QUESTION_MARK");

    public static final IElementType LEFT_PAREN =
            token("LEFT_PAREN");

    public static final IElementType RIGHT_PAREN =
            token("RIGHT_PAREN");

    public static final IElementType LEFT_BRACE =
            token("LEFT_BRACE");

    public static final IElementType RIGHT_BRACE =
            token("RIGHT_BRACE");

    public static final IElementType LEFT_BRACKET =
            token("LEFT_BRACKET");

    public static final IElementType RIGHT_BRACKET =
            token("RIGHT_BRACKET");

    private FelidaeTokenTypes() {
        throw new AssertionError(
                "FelidaeTokenTypes is a utility class."
        );
    }

    private static @NotNull IElementType token(
            @NotNull String debugName
    ) {
        return new FelidaeTokenType(debugName);
    }

    private static final class FelidaeTokenType
            extends IElementType {

        private FelidaeTokenType(
                @NotNull String debugName
        ) {
            super(debugName, FelidaeLanguage.INSTANCE);
        }

        @Override
        public @NotNull String toString() {
            return "FelidaeTokenType." + super.toString();
        }
    }
}