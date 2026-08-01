package local.felidae.intellij;

import com.intellij.codeInsight.completion.CompletionContributor;
import com.intellij.codeInsight.completion.CompletionParameters;
import com.intellij.codeInsight.completion.CompletionProvider;
import com.intellij.codeInsight.completion.CompletionResultSet;
import com.intellij.codeInsight.completion.CompletionType;
import com.intellij.codeInsight.lookup.LookupElementBuilder;
import com.intellij.openapi.editor.Document;
import com.intellij.patterns.PlatformPatterns;
import com.intellij.util.ProcessingContext;
import local.felidae.intellij.execution.FelidaeStdlibIndex;
import org.jetbrains.annotations.NotNull;

import java.util.LinkedHashMap;
import java.util.Map;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/**
 * Basic completion for Felidae: stdlib functions after "module.", and
 * in-scope names (locals, lambda items, facts, methods, imported/core
 * libraries) elsewhere. Text-scan based, same constraint as every other
 * Felidae IntelliJ feature: there is no PSI grammar to walk.
 */
public final class FelidaeCompletionContributor extends CompletionContributor {

    private static final Pattern DOT_TRIGGER = Pattern.compile("([A-Za-z_][A-Za-z0-9_]*)\\.$");
    private static final Pattern DECLARATION =
            Pattern.compile("(?m)^[ \\t]*([A-Za-z_][A-Za-z0-9_:.]*)\\s*\\(([\\s\\S]*?)\\)\\s*(=>|\\.)");
    private static final Pattern BINDING = Pattern.compile("\\b([A-Za-z_][A-Za-z0-9_]*)\\s*:=");
    private static final Pattern LAMBDA_ITEM =
            Pattern.compile("\\blambda\\s*\\([^,]+,\\s*([a-z_][A-Za-z0-9_]*)\\s*=>");

    public FelidaeCompletionContributor() {
        extend(CompletionType.BASIC, PlatformPatterns.psiElement(), new CompletionProvider<>() {
            @Override
            protected void addCompletions(
                    @NotNull CompletionParameters parameters,
                    @NotNull ProcessingContext context,
                    @NotNull CompletionResultSet result
            ) {
                if (parameters.getOriginalFile().getFileType() != FelidaeFileType.INSTANCE) {
                    return;
                }

                Document document = parameters.getEditor().getDocument();
                int offset = Math.min(parameters.getOffset(), document.getTextLength());
                int lineNumber = document.getLineNumber(offset);
                String linePrefix = document
                        .getText()
                        .substring(document.getLineStartOffset(lineNumber), offset);

                Matcher dotMatch = DOT_TRIGGER.matcher(linePrefix);
                if (dotMatch.find()) {
                    addNamespaceCompletions(result, dotMatch.group(1));
                    return;
                }

                addScopeCompletions(result, document.getText());
            }
        });
    }

    private static void addNamespaceCompletions(@NotNull CompletionResultSet result, @NotNull String baseName) {
        String prefix = baseName + ":";
        for (Map.Entry<String, FelidaeBuiltinDocs.Entry> entry : FelidaeBuiltinDocs.entries()) {
            if (!entry.getKey().startsWith(prefix)) continue;
            String functionName = entry.getKey().substring(prefix.length());
            FelidaeBuiltinDocs.Entry doc = entry.getValue();
            result.addElement(
                    LookupElementBuilder.create(functionName)
                            .withTypeText(doc.heading())
                            .withTailText("  " + doc.description(), true)
            );
        }
    }

    private static void addScopeCompletions(@NotNull CompletionResultSet result, @NotNull String text) {
        Map<String, LookupElementBuilder> items = new LinkedHashMap<>();

        Matcher binding = BINDING.matcher(text);
        while (binding.find()) {
            String name = binding.group(1);
            items.putIfAbsent(name, LookupElementBuilder.create(name).withTypeText("binding"));
        }

        Matcher lambdaItem = LAMBDA_ITEM.matcher(text);
        while (lambdaItem.find()) {
            String name = lambdaItem.group(1);
            items.putIfAbsent(name, LookupElementBuilder.create(name).withTypeText("lambda item"));
        }

        Matcher declaration = DECLARATION.matcher(text);
        while (declaration.find()) {
            String name = declaration.group(1);
            boolean isMethod = "=>".equals(declaration.group(3));
            items.putIfAbsent(
                    name,
                    LookupElementBuilder.create(name).withTypeText(isMethod ? "method" : "fact")
            );
        }

        for (String library : FelidaeStdlibIndex.getLibraries()) {
            items.putIfAbsent(library, LookupElementBuilder.create(library).withTypeText("core library"));
        }

        for (Map.Entry<String, FelidaeBuiltinDocs.Entry> entry : FelidaeBuiltinDocs.entries()) {
            if (entry.getKey().contains(":")) continue;
            items.putIfAbsent(
                    entry.getKey(),
                    LookupElementBuilder.create(entry.getKey()).withTypeText(entry.getValue().heading())
            );
        }

        for (LookupElementBuilder element : items.values()) {
            result.addElement(element);
        }
    }
}
