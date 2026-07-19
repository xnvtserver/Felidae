package local.felidae.intellij.highlighting;

import com.intellij.openapi.editor.DefaultLanguageHighlighterColors;
import com.intellij.openapi.editor.HighlighterColors;
import com.intellij.openapi.editor.colors.TextAttributesKey;
import org.jetbrains.annotations.NotNull;

public final class FelidaeTextAttributes {

    public static final TextAttributesKey COMMENT =
            key(
                    "FELIDAE_COMMENT",
                    DefaultLanguageHighlighterColors.LINE_COMMENT
            );

    public static final TextAttributesKey STRING =
            key(
                    "FELIDAE_STRING",
                    DefaultLanguageHighlighterColors.STRING
            );

    public static final TextAttributesKey NUMBER =
            key(
                    "FELIDAE_NUMBER",
                    DefaultLanguageHighlighterColors.NUMBER
            );

    public static final TextAttributesKey KEYWORD =
            key(
                    "FELIDAE_KEYWORD",
                    DefaultLanguageHighlighterColors.KEYWORD
            );

    public static final TextAttributesKey OPERATOR =
            key(
                    "FELIDAE_OPERATOR",
                    DefaultLanguageHighlighterColors.OPERATION_SIGN
            );

    public static final TextAttributesKey PUNCTUATION =
            key(
                    "FELIDAE_PUNCTUATION",
                    DefaultLanguageHighlighterColors.PARENTHESES
            );

    public static final TextAttributesKey FACT =
            key(
                    "FELIDAE_FACT",
                    DefaultLanguageHighlighterColors.CLASS_NAME
            );

    public static final TextAttributesKey METHOD =
            key(
                    "FELIDAE_METHOD",
                    DefaultLanguageHighlighterColors.FUNCTION_DECLARATION
            );

    public static final TextAttributesKey VARIABLE =
            key(
                    "FELIDAE_VARIABLE",
                    DefaultLanguageHighlighterColors.LOCAL_VARIABLE
            );

    public static final TextAttributesKey PARAMETER =
            key(
                    "FELIDAE_PARAMETER",
                    DefaultLanguageHighlighterColors.PARAMETER
            );

    public static final TextAttributesKey FIELD =
            key(
                    "FELIDAE_FIELD",
                    DefaultLanguageHighlighterColors.INSTANCE_FIELD
            );

    public static final TextAttributesKey LIBRARY =
            key(
                    "FELIDAE_LIBRARY",
                    DefaultLanguageHighlighterColors.CLASS_REFERENCE
            );

    public static final TextAttributesKey LIBRARY_MEMBER =
            key(
                    "FELIDAE_LIBRARY_MEMBER",
                    DefaultLanguageHighlighterColors.STATIC_METHOD
            );

    public static final TextAttributesKey IDENTIFIER =
            key(
                    "FELIDAE_IDENTIFIER",
                    DefaultLanguageHighlighterColors.IDENTIFIER
            );

    public static final TextAttributesKey BAD_CHARACTER =
            key(
                    "FELIDAE_BAD_CHARACTER",
                    HighlighterColors.BAD_CHARACTER
            );

    private FelidaeTextAttributes() {
        throw new AssertionError(
                "FelidaeTextAttributes is a utility class."
        );
    }

    private static @NotNull TextAttributesKey key(
            @NotNull String externalName,
            @NotNull TextAttributesKey fallback
    ) {
        return TextAttributesKey.createTextAttributesKey(
                externalName,
                fallback
        );
    }
}