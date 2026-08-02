package local.felidae.intellij;

import com.intellij.codeInsight.completion.CompletionContributor;
import com.intellij.codeInsight.completion.CompletionParameters;
import com.intellij.codeInsight.completion.CompletionProvider;
import com.intellij.codeInsight.completion.CompletionResultSet;
import com.intellij.codeInsight.completion.CompletionType;
import com.intellij.codeInsight.completion.PrioritizedLookupElement;
import com.intellij.codeInsight.lookup.LookupElementBuilder;
import com.intellij.openapi.editor.Document;
import com.intellij.patterns.PlatformPatterns;
import com.intellij.util.ProcessingContext;
import local.felidae.intellij.execution.FelidaeStdlibIndex;
import local.felidae.intellij.ml.FelidaeMlRanking;
import org.jetbrains.annotations.NotNull;

import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
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
    private static final Pattern BINDING = Pattern.compile("\\b([A-Za-z_][A-Za-z0-9_]*)\\s*:=");
    private static final Pattern TRAILING_IDENTIFIER = Pattern.compile("([A-Za-z_][A-Za-z0-9_]*)$");
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

                String text = document.getText();

                // Inside a call's parentheses, the useful suggestions are the
                // named-argument keys that call accepts. Felidae calls are
                // written `f(key: value, ...)`, so this is the completion that
                // matters most there.
                FelidaeCallContext call = FelidaeCallContext.at(text, offset);
                if (call != null) {
                    addNamedArgumentCompletions(result, text, call);
                }

                Matcher prefixMatch = TRAILING_IDENTIFIER.matcher(linePrefix);
                String prefix = prefixMatch.find() ? prefixMatch.group(1) : "";
                addScopeCompletions(result, text, text.substring(0, offset), prefix);
            }
        });
    }

    /**
     * Suggests the `key:` names the enclosing call accepts, skipping keys the
     * call already names. Parameters come from {@link FelidaeCallResolver}, the
     * same resolver {@link FelidaeParameterInfoHandler} uses, so completion and
     * parameter info always agree.
     */
    private static void addNamedArgumentCompletions(
            @NotNull CompletionResultSet result,
            @NotNull String text,
            @NotNull FelidaeCallContext call
    ) {
        FelidaeCallResolver.ResolvedCall resolved =
                FelidaeCallResolver.resolve(text, call.callName());
        if (resolved == null) return;

        List<String> paramNames = new ArrayList<>();
        for (FelidaeCallResolver.Param param : resolved.params()) paramNames.add(param.name());
        int firstUnsupplied =
                FelidaeMlRanking.firstUnsuppliedIndex(paramNames, call.suppliedKeys());
        boolean isBuiltinCall =
                FelidaeBuiltinDocs.contains(call.callName().replace('.', ':'));

        String typed = call.keyBeingTyped();
        for (FelidaeCallResolver.Param param : resolved.params()) {
            // A key already written earlier in this call is not a useful
            // suggestion - unless it is the one being typed right now.
            if (call.suppliedKeys().contains(param.name())
                    && !param.name().equals(typed)) {
                continue;
            }
            LookupElementBuilder element = LookupElementBuilder
                    .create(param.name())
                    .withPresentableText(param.name())
                    .withTailText(": ", true)
                    .withTypeText(param.type().isEmpty() ? resolved.label() : param.type())
                    .withInsertHandler((context, item) -> {
                        // Complete to `name: ` so the caret lands on the value.
                        int tail = context.getTailOffset();
                        CharSequence document = context.getDocument().getCharsSequence();
                        int probe = tail;
                        while (probe < document.length() && document.charAt(probe) == ' ') probe++;
                        if (probe >= document.length() || document.charAt(probe) != ':') {
                            context.getDocument().insertString(tail, ": ");
                            context.getEditor().getCaretModel().moveToOffset(tail + 2);
                        }
                    });
            // Named arguments are what belongs here, so float them above the
            // generic in-scope identifiers added afterwards. Within that band
            // the trained next-param model decides the order; with no model
            // bundled every candidate scores 0 and declaration order stands.
            double modelScore = FelidaeMlRanking.scoreNextParam(
                    param.name(),
                    paramNames.indexOf(param.name()),
                    paramNames.size(),
                    call.suppliedKeys().size(),
                    firstUnsupplied,
                    isBuiltinCall);
            result.addElement(
                    PrioritizedLookupElement.withPriority(element, 100.0 + modelScore));
        }
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

    private static void addScopeCompletions(
            @NotNull CompletionResultSet result,
            @NotNull String text,
            @NotNull String textAboveCaret,
            @NotNull String prefix
    ) {
        Map<String, LookupElementBuilder> items = new LinkedHashMap<>();
        Map<String, FelidaeMlRanking.CandidateKind> kinds = new LinkedHashMap<>();

        Matcher binding = BINDING.matcher(text);
        while (binding.find()) {
            String name = binding.group(1);
            items.putIfAbsent(name, LookupElementBuilder.create(name).withTypeText("binding"));
            kinds.putIfAbsent(name, FelidaeMlRanking.CandidateKind.LOCAL);
        }

        Matcher lambdaItem = LAMBDA_ITEM.matcher(text);
        while (lambdaItem.find()) {
            String name = lambdaItem.group(1);
            items.putIfAbsent(name, LookupElementBuilder.create(name).withTypeText("lambda item"));
            kinds.putIfAbsent(name, FelidaeMlRanking.CandidateKind.PARAM);
        }

        for (FelidaeCallResolver.Declaration declaration : FelidaeCallResolver.declarations(text)) {
            items.putIfAbsent(
                    declaration.name(),
                    LookupElementBuilder.create(declaration.name())
                            .withTypeText(declaration.isMethod() ? "method" : "fact")
            );
            kinds.putIfAbsent(
                    declaration.name(),
                    declaration.isMethod()
                            ? FelidaeMlRanking.CandidateKind.METHOD
                            : FelidaeMlRanking.CandidateKind.FACT);
            // Head parameters are in scope inside the declaration's own body.
            for (FelidaeCallResolver.Param param
                    : FelidaeCallResolver.collectHeadParams(declaration.argsText())) {
                items.putIfAbsent(
                        param.name(),
                        LookupElementBuilder.create(param.name()).withTypeText("parameter"));
                kinds.putIfAbsent(param.name(), FelidaeMlRanking.CandidateKind.PARAM);
            }
        }

        for (String library : FelidaeStdlibIndex.getLibraries()) {
            items.putIfAbsent(library, LookupElementBuilder.create(library).withTypeText("core library"));
            kinds.putIfAbsent(library, FelidaeMlRanking.CandidateKind.LIBRARY);
        }

        for (Map.Entry<String, FelidaeBuiltinDocs.Entry> entry : FelidaeBuiltinDocs.entries()) {
            if (entry.getKey().contains(":")) continue;
            items.putIfAbsent(
                    entry.getKey(),
                    LookupElementBuilder.create(entry.getKey()).withTypeText(entry.getValue().heading())
            );
            kinds.putIfAbsent(entry.getKey(), FelidaeMlRanking.CandidateKind.LIBRARY);
        }

        // The trained completion model decides the order. With no model
        // bundled every candidate scores 0, leaving IntelliJ's own ordering.
        FelidaeMlRanking.CompletionContext context =
                FelidaeMlRanking.buildCompletionContext(textAboveCaret, prefix);
        for (Map.Entry<String, LookupElementBuilder> item : items.entrySet()) {
            double score = FelidaeMlRanking.scoreCompletion(
                    item.getKey(),
                    kinds.getOrDefault(item.getKey(), FelidaeMlRanking.CandidateKind.LOCAL),
                    context);
            result.addElement(PrioritizedLookupElement.withPriority(item.getValue(), score));
        }
    }
}
