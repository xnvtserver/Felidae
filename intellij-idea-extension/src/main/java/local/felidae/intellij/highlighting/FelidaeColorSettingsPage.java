package local.felidae.intellij.highlighting;

import com.intellij.openapi.editor.colors.TextAttributesKey;
import com.intellij.openapi.fileTypes.SyntaxHighlighter;
import com.intellij.openapi.options.colors.AttributesDescriptor;
import com.intellij.openapi.options.colors.ColorDescriptor;
import com.intellij.openapi.options.colors.ColorSettingsPage;
import local.felidae.intellij.FelidaeIcons;
import local.felidae.intellij.FelidaeSyntaxHighlighter;
import org.jetbrains.annotations.NotNull;
import org.jetbrains.annotations.Nullable;

import javax.swing.Icon;
import java.util.Map;

public final class FelidaeColorSettingsPage
        implements ColorSettingsPage {

    private static final AttributesDescriptor[] DESCRIPTORS = {
            new AttributesDescriptor(
                    "Comments",
                    FelidaeTextAttributes.COMMENT
            ),
            new AttributesDescriptor(
                    "Strings",
                    FelidaeTextAttributes.STRING
            ),
            new AttributesDescriptor(
                    "Numbers",
                    FelidaeTextAttributes.NUMBER
            ),
            new AttributesDescriptor(
                    "Keywords",
                    FelidaeTextAttributes.KEYWORD
            ),
            new AttributesDescriptor(
                    "Operators",
                    FelidaeTextAttributes.OPERATOR
            ),
            new AttributesDescriptor(
                    "Punctuation",
                    FelidaeTextAttributes.PUNCTUATION
            ),
            new AttributesDescriptor(
                    "Facts and types",
                    FelidaeTextAttributes.FACT
            ),
            new AttributesDescriptor(
                    "Methods",
                    FelidaeTextAttributes.METHOD
            ),
            new AttributesDescriptor(
                    "Variables",
                    FelidaeTextAttributes.VARIABLE
            ),
            new AttributesDescriptor(
                    "Parameters",
                    FelidaeTextAttributes.PARAMETER
            ),
            new AttributesDescriptor(
                    "Fields",
                    FelidaeTextAttributes.FIELD
            ),
            new AttributesDescriptor(
                    "Libraries",
                    FelidaeTextAttributes.LIBRARY
            ),
            new AttributesDescriptor(
                    "Library members",
                    FelidaeTextAttributes.LIBRARY_MEMBER
            ),
            new AttributesDescriptor(
                    "Other identifiers",
                    FelidaeTextAttributes.IDENTIFIER
            ),
            new AttributesDescriptor(
                    "Invalid characters",
                    FelidaeTextAttributes.BAD_CHARACTER
            )
    };

    @Override
    public @Nullable Icon getIcon() {
        return FelidaeIcons.FILE;
    }

    @Override
    public @NotNull SyntaxHighlighter getHighlighter() {
        return new FelidaeSyntaxHighlighter();
    }

    @Override
    public @NotNull String getDemoText() {
        return """
                # Felidae syntax-color preview

                import "employees.fx".

                Employee(
                    name: "Alice",
                    age: 30,
                    role: "Engineer"
                ).

                isEngineer(input: Employee) =>
                    employee := input,
                    label := employee.name
                        then system.print(value: system.result),
                    where employee.role == "Engineer",
                    return (
                        name: employee.name,
                        active: true
                    ).
                """;
    }

    @Override
    public AttributesDescriptor @NotNull [] getAttributeDescriptors() {
        return DESCRIPTORS.clone();
    }

    @Override
    public ColorDescriptor @NotNull [] getColorDescriptors() {
        return ColorDescriptor.EMPTY_ARRAY;
    }

    @Override
    public @NotNull String getDisplayName() {
        return "Felidae";
    }

    @Override
    public @NotNull Map<String, TextAttributesKey>
    getAdditionalHighlightingTagToDescriptorMap() {
        return Map.of();
    }
}
