package local.felidae.intellij;

import com.intellij.lexer.Lexer;
import com.intellij.lexer.LexerBase;
import com.intellij.openapi.editor.DefaultLanguageHighlighterColors;
import com.intellij.openapi.editor.HighlighterColors;
import com.intellij.openapi.editor.colors.TextAttributesKey;
import com.intellij.openapi.fileTypes.SyntaxHighlighterBase;
import com.intellij.psi.TokenType;
import com.intellij.psi.tree.IElementType;
import org.jetbrains.annotations.NotNull;
import org.jetbrains.annotations.Nullable;
import local.felidae.intellij.highlighting.FelidaeTextAttributes;

import java.util.Set;

/**
 * Lightweight lexical highlighter for Felidae source files.
 *
 * This lexer is intentionally limited to visual token classification.
 * Full parsing and validation are performed by Celidae through
 * {@link FelidaeExternalAnnotator}.
 */
public final class FelidaeSyntaxHighlighter
        extends SyntaxHighlighterBase {

    /*
     * Felidae token types
     */

    public static final IElementType FACT =
            new FelidaeTokenType("FACT");

    public static final IElementType METHOD =
            new FelidaeTokenType("METHOD");

    public static final IElementType VARIABLE =
            new FelidaeTokenType("VARIABLE");

    public static final IElementType PARAMETER =
            new FelidaeTokenType("PARAMETER");

    public static final IElementType FIELD =
            new FelidaeTokenType("FIELD");

    public static final IElementType LIBRARY =
            new FelidaeTokenType("LIBRARY");

    public static final IElementType LIBRARY_MEMBER =
            new FelidaeTokenType("LIBRARY_MEMBER");

    public static final IElementType IDENTIFIER =
            new FelidaeTokenType("IDENTIFIER");

    public static final IElementType COMMENT =
            new FelidaeTokenType("COMMENT");

    public static final IElementType STRING =
            new FelidaeTokenType("STRING");

    public static final IElementType UNTERMINATED_STRING =
            new FelidaeTokenType("UNTERMINATED_STRING");

    public static final IElementType NUMBER =
            new FelidaeTokenType("NUMBER");

    public static final IElementType KEYWORD =
            new FelidaeTokenType("KEYWORD");

    public static final IElementType OPERATOR =
            new FelidaeTokenType("OPERATOR");

    public static final IElementType PUNCTUATION =
            new FelidaeTokenType("PUNCTUATION");

    public static final IElementType LEFT_PAREN =
            new FelidaeTokenType("LEFT_PAREN");

    public static final IElementType RIGHT_PAREN =
            new FelidaeTokenType("RIGHT_PAREN");

    public static final IElementType LEFT_BRACE =
            new FelidaeTokenType("LEFT_BRACE");

    public static final IElementType RIGHT_BRACE =
            new FelidaeTokenType("RIGHT_BRACE");

    public static final IElementType LEFT_BRACKET =
            new FelidaeTokenType("LEFT_BRACKET");

    public static final IElementType RIGHT_BRACKET =
            new FelidaeTokenType("RIGHT_BRACKET");

    public static final IElementType COMMA =
            new FelidaeTokenType("COMMA");

    public static final IElementType COLON =
            new FelidaeTokenType("COLON");

    public static final IElementType DOT =
            new FelidaeTokenType("DOT");

    public static final IElementType QUESTION_MARK =
            new FelidaeTokenType("QUESTION_MARK");



    /*
     * IntelliJ text styles
     */

    private static final TextAttributesKey[] COMMENT_KEYS =
            pack(FelidaeTextAttributes.COMMENT);

    private static final TextAttributesKey[] STRING_KEYS =
            pack(FelidaeTextAttributes.STRING);

    private static final TextAttributesKey[] NUMBER_KEYS =
            pack(FelidaeTextAttributes.NUMBER);

    private static final TextAttributesKey[] KEYWORD_KEYS =
            pack(FelidaeTextAttributes.KEYWORD);

    private static final TextAttributesKey[] OPERATOR_KEYS =
            pack(FelidaeTextAttributes.OPERATOR);

    private static final TextAttributesKey[] PUNCTUATION_KEYS =
            pack(FelidaeTextAttributes.PUNCTUATION);

    private static final TextAttributesKey[] FACT_KEYS =
            pack(FelidaeTextAttributes.FACT);

    private static final TextAttributesKey[] METHOD_KEYS =
            pack(FelidaeTextAttributes.METHOD);

    private static final TextAttributesKey[] VARIABLE_KEYS =
            pack(FelidaeTextAttributes.VARIABLE);

    private static final TextAttributesKey[] PARAMETER_KEYS =
            pack(FelidaeTextAttributes.PARAMETER);

    private static final TextAttributesKey[] FIELD_KEYS =
            pack(FelidaeTextAttributes.FIELD);

    private static final TextAttributesKey[] LIBRARY_KEYS =
            pack(FelidaeTextAttributes.LIBRARY);

    private static final TextAttributesKey[] LIBRARY_MEMBER_KEYS =
            pack(FelidaeTextAttributes.LIBRARY_MEMBER);

    private static final TextAttributesKey[] IDENTIFIER_KEYS =
            pack(FelidaeTextAttributes.IDENTIFIER);

    private static final TextAttributesKey[] BAD_CHARACTER_KEYS =
            pack(FelidaeTextAttributes.BAD_CHARACTER);

    private static final TextAttributesKey[] EMPTY_KEYS =
            TextAttributesKey.EMPTY_ARRAY;

    @Override
    public @NotNull Lexer getHighlightingLexer() {
        return new FelidaeHighlightingLexer();
    }

    @Override
    public TextAttributesKey @NotNull [] getTokenHighlights(
            @NotNull IElementType tokenType
    ) {
        if (tokenType == COMMENT) {
            return COMMENT_KEYS;
        }

        if (tokenType == STRING) {
            return STRING_KEYS;
        }

        if (tokenType == UNTERMINATED_STRING ||
                tokenType == TokenType.BAD_CHARACTER) {
            return BAD_CHARACTER_KEYS;
        }

        if (tokenType == NUMBER) {
            return NUMBER_KEYS;
        }

        if (tokenType == KEYWORD) {
            return KEYWORD_KEYS;
        }

        if (tokenType == OPERATOR) {
            return OPERATOR_KEYS;
        }

        if (isPunctuationToken(tokenType)) {
            return PUNCTUATION_KEYS;
        }

        if (tokenType == FACT) {
            return FACT_KEYS;
        }

        if (tokenType == METHOD) {
            return METHOD_KEYS;
        }

        if (tokenType == VARIABLE) {
            return VARIABLE_KEYS;
        }

        if (tokenType == PARAMETER) {
            return PARAMETER_KEYS;
        }

        if (tokenType == FIELD) {
            return FIELD_KEYS;
        }

        if (tokenType == LIBRARY) {
            return LIBRARY_KEYS;
        }

        if (tokenType == LIBRARY_MEMBER) {
            return LIBRARY_MEMBER_KEYS;
        }

        if (tokenType == IDENTIFIER) {
            return IDENTIFIER_KEYS;
        }

        return EMPTY_KEYS;
    }

    private static boolean isPunctuationToken(
            @NotNull IElementType tokenType
    ) {
        return tokenType == LEFT_PAREN ||
                tokenType == RIGHT_PAREN ||
                tokenType == LEFT_BRACE ||
                tokenType == RIGHT_BRACE ||
                tokenType == LEFT_BRACKET ||
                tokenType == RIGHT_BRACKET ||
                tokenType == COMMA ||
                tokenType == COLON ||
                tokenType == DOT ||
                tokenType == QUESTION_MARK;
    }

    /**
     * Associates custom token types with the Felidae language.
     */
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

    /**
     * Stateless lexer used only for editor highlighting.
     *
     * Every token begins exactly where the previous token ended. This is
     * important because IntelliJ lexers must cover the complete input without
     * silently skipping whitespace or other characters.
     */
    private static final class FelidaeHighlightingLexer
            extends LexerBase {

        private static final Set<String> KEYWORDS = Set.of(
                "import",
                "extend",
                "where",
                "else",
                "return",
                "lambda",
                "true",
                "false",
                "nil"
        );

        private static final Set<String> LIBRARIES = Set.of(
                "array",
                "console",
                "file",
                "fn",
                "http",
                "json",
                "math",
                "ml",
                "pair",
                "str",
                "system"
        );

        private CharSequence buffer = "";
        private int bufferEnd;

        private int tokenStart;
        private int tokenEnd;

        private @Nullable IElementType tokenType;

        @Override
        public void start(
                @NotNull CharSequence buffer,
                int startOffset,
                int endOffset,
                int initialState
        ) {
            this.buffer = buffer;
            this.bufferEnd = endOffset;

            tokenStart = startOffset;
            tokenEnd = startOffset;
            tokenType = null;

            advance();
        }

        @Override
        public int getState() {
            /*
             * This lexer has no multiline incremental states.
             * Strings and comments are scanned completely in one token.
             */
            return 0;
        }

        @Override
        public @Nullable IElementType getTokenType() {
            return tokenType;
        }

        @Override
        public int getTokenStart() {
            return tokenStart;
        }

        @Override
        public int getTokenEnd() {
            return tokenEnd;
        }

        @Override
        public @NotNull CharSequence getBufferSequence() {
            return buffer;
        }

        @Override
        public int getBufferEnd() {
            return bufferEnd;
        }

        @Override
        public void advance() {
            tokenStart = tokenEnd;

            if (tokenStart >= bufferEnd) {
                tokenEnd = tokenStart;
                tokenType = null;
                return;
            }

            char current = buffer.charAt(tokenStart);

            if (Character.isWhitespace(current)) {
                readWhitespace();
                return;
            }

            if (current == '#') {
                readComment();
                return;
            }

            if (current == '"') {
                readString();
                return;
            }

            if (Character.isDigit(current)) {
                readNumber();
                return;
            }

            if (isIdentifierStart(current)) {
                readIdentifier();
                return;
            }

            readSymbol();
        }

        private void readWhitespace() {
            tokenEnd = tokenStart + 1;

            while (tokenEnd < bufferEnd &&
                    Character.isWhitespace(buffer.charAt(tokenEnd))) {
                tokenEnd++;
            }

            tokenType = TokenType.WHITE_SPACE;
        }

        private void readComment() {
            tokenEnd = tokenStart + 1;

            while (tokenEnd < bufferEnd) {
                char current = buffer.charAt(tokenEnd);

                if (current == '\n' || current == '\r') {
                    break;
                }

                tokenEnd++;
            }

            tokenType = COMMENT;
        }

        private void readString() {
            tokenEnd = tokenStart + 1;

            boolean escaped = false;
            boolean terminated = false;

            while (tokenEnd < bufferEnd) {
                char current = buffer.charAt(tokenEnd);
                tokenEnd++;

                if (escaped) {
                    escaped = false;
                    continue;
                }

                if (current == '\\') {
                    escaped = true;
                    continue;
                }

                if (current == '"') {
                    terminated = true;
                    break;
                }

                /*
                 * Felidae strings are assumed to be single-line.
                 * Remove this check if multiline strings become supported.
                 */
                if (current == '\n' || current == '\r') {
                    break;
                }
            }

            tokenType = terminated
                    ? STRING
                    : UNTERMINATED_STRING;
        }

        private void readNumber() {
            tokenEnd = tokenStart;

            readDigits();

            /*
             * Decimal component: 12.25
             *
             * A dot is part of the number only when followed by a digit.
             * Therefore, "12." becomes NUMBER("12") + PUNCTUATION(".").
             */
            if (tokenEnd < bufferEnd &&
                    buffer.charAt(tokenEnd) == '.' &&
                    tokenEnd + 1 < bufferEnd &&
                    Character.isDigit(buffer.charAt(tokenEnd + 1))) {
                tokenEnd++;
                readDigits();
            }

            /*
             * Optional scientific notation:
             * 1e6, 1E6, 2.5e-3
             */
            if (tokenEnd < bufferEnd &&
                    isExponentMarker(buffer.charAt(tokenEnd))) {
                int exponentStart = tokenEnd;
                int probe = tokenEnd + 1;

                if (probe < bufferEnd &&
                        (buffer.charAt(probe) == '+' ||
                                buffer.charAt(probe) == '-')) {
                    probe++;
                }

                if (probe < bufferEnd &&
                        Character.isDigit(buffer.charAt(probe))) {
                    tokenEnd = probe;
                    readDigits();
                } else {
                    /*
                     * Do not consume an invalid exponent marker.
                     * The external debugger will report the syntax error.
                     */
                    tokenEnd = exponentStart;
                }
            }

            tokenType = NUMBER;
        }

        private void readDigits() {
            while (tokenEnd < bufferEnd &&
                    Character.isDigit(buffer.charAt(tokenEnd))) {
                tokenEnd++;
            }
        }

        private void readIdentifier() {
            tokenEnd = tokenStart + 1;

            while (tokenEnd < bufferEnd &&
                    isIdentifierPart(buffer.charAt(tokenEnd))) {
                tokenEnd++;
            }

            String text = buffer
                    .subSequence(tokenStart, tokenEnd)
                    .toString();

            if (KEYWORDS.contains(text)) {
                tokenType = KEYWORD;
                return;
            }

            tokenType = isLibraryAccess(text)
                    ? LIBRARY
                    : IDENTIFIER;
        }

        private boolean isLibraryAccess(
                @NotNull String identifier
        ) {
            if (!LIBRARIES.contains(identifier)) {
                return false;
            }

            int probe = tokenEnd;

            /*
             * Allow highlighting both:
             *
             * system.print(...)
             * system . print(...)
             *
             * Remove whitespace probing if spaces around member access
             * are not valid Felidae syntax.
             */
            while (probe < bufferEnd &&
                    Character.isWhitespace(buffer.charAt(probe))) {
                probe++;
            }

            if (probe >= bufferEnd) {
                return false;
            }

            char next = buffer.charAt(probe);

            return next == '.' || next == ':';
        }

        private void readSymbol() {
            if (hasTwoCharacterOperator()) {
                tokenEnd = tokenStart + 2;
                tokenType = OPERATOR;
                return;
            }

            char current = buffer.charAt(tokenStart);
            tokenEnd = tokenStart + 1;

            tokenType = switch (current) {
                case '(' -> LEFT_PAREN;
                case ')' -> RIGHT_PAREN;

                case '{' -> LEFT_BRACE;
                case '}' -> RIGHT_BRACE;

                case '[' -> LEFT_BRACKET;
                case ']' -> RIGHT_BRACKET;

                case ',' -> COMMA;
                case ':' -> COLON;
                case '.' -> DOT;
                case '?' -> QUESTION_MARK;

                case '=',
                     '!',
                     '<',
                     '>',
                     '|',
                     '&',
                     '+',
                     '-',
                     '*',
                     '/',
                     '%' -> OPERATOR;

                default -> TokenType.BAD_CHARACTER;
            };
        }

        private boolean hasTwoCharacterOperator() {
            if (tokenStart + 1 >= bufferEnd) {
                return false;
            }

            char first = buffer.charAt(tokenStart);
            char second = buffer.charAt(tokenStart + 1);

            return switch (first) {
                case '=' ->
                        second == '>' || second == '=';

                case '!' ->
                        second == '=';

                case '>' ->
                        second == '=';

                case '<' ->
                        second == '=';

                case ':' ->
                        second == '=';

                case '&' ->
                        second == '&';

                case '|' ->
                        second == '|';

                default ->
                        false;
            };
        }


        private static boolean isIdentifierStart(
                char value
        ) {
            return Character.isLetter(value) ||
                    value == '_';
        }

        private static boolean isIdentifierPart(
                char value
        ) {
            return Character.isLetterOrDigit(value) ||
                    value == '_';
        }

        private static boolean isExponentMarker(
                char value
        ) {
            return value == 'e' || value == 'E';
        }
    }
}
