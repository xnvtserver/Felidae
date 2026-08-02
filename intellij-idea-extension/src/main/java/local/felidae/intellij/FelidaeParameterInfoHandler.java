package local.felidae.intellij;

import com.intellij.lang.parameterInfo.CreateParameterInfoContext;
import com.intellij.lang.parameterInfo.ParameterInfoHandler;
import com.intellij.lang.parameterInfo.ParameterInfoUIContext;
import com.intellij.lang.parameterInfo.UpdateParameterInfoContext;
import com.intellij.psi.PsiElement;
import com.intellij.psi.PsiFile;
import org.jetbrains.annotations.NotNull;
import org.jetbrains.annotations.Nullable;

import java.util.List;

/**
 * Shows the expected {@code key:} parameters while the caret is inside a call's
 * parentheses (Ctrl+P / "Parameter Info"), highlighting the argument being
 * typed. This is the IntelliJ counterpart of the VS Code extension's
 * SignatureHelpProvider, and resolves parameters through the same
 * {@link FelidaeCallResolver} that keyword-argument completion uses.
 *
 * <p>There is no PSI grammar for .fx files - every one is a PsiPlainTextFile -
 * so the "parameter owner" element is simply the file, and the real work is
 * done against raw text by {@link FelidaeCallContext}.
 */
public final class FelidaeParameterInfoHandler
        implements ParameterInfoHandler<PsiElement, FelidaeCallResolver.ResolvedCall> {

    @Override
    public @Nullable PsiElement findElementForParameterInfo(
            @NotNull CreateParameterInfoContext context
    ) {
        FelidaeCallResolver.ResolvedCall resolved = resolveAt(context.getFile(), context.getOffset());
        if (resolved == null) return null;
        context.setItemsToShow(new Object[]{ resolved });
        return context.getFile();
    }

    @Override
    public void showParameterInfo(
            @NotNull PsiElement element,
            @NotNull CreateParameterInfoContext context
    ) {
        context.showHint(element, context.getOffset(), this);
    }

    @Override
    public @Nullable PsiElement findElementForUpdatingParameterInfo(
            @NotNull UpdateParameterInfoContext context
    ) {
        // Keep the popup alive as long as the caret is still inside a call.
        FelidaeCallContext call = callContext(context.getFile(), context.getOffset());
        return call == null ? null : context.getFile();
    }

    @Override
    public void updateParameterInfo(
            @NotNull PsiElement parameterOwner,
            @NotNull UpdateParameterInfoContext context
    ) {
        FelidaeCallContext call = callContext(context.getFile(), context.getOffset());
        if (call == null) {
            context.setCurrentParameter(-1);
            return;
        }

        Object[] items = context.getObjectsToView();
        FelidaeCallResolver.ResolvedCall resolved =
                items != null && items.length > 0
                        && items[0] instanceof FelidaeCallResolver.ResolvedCall value
                        ? value
                        : null;
        context.setCurrentParameter(highlightIndex(resolved, call));
    }

    @Override
    public void updateUI(
            @Nullable FelidaeCallResolver.ResolvedCall resolved,
            @NotNull ParameterInfoUIContext context
    ) {
        if (resolved == null || resolved.params().isEmpty()) {
            context.setUIComponentEnabled(false);
            return;
        }

        List<FelidaeCallResolver.Param> params = resolved.params();
        StringBuilder text = new StringBuilder();
        int highlightStart = -1;
        int highlightEnd = -1;
        int current = context.getCurrentParameterIndex();

        for (int i = 0; i < params.size(); i++) {
            if (i > 0) text.append(", ");
            int start = text.length();
            text.append(params.get(i).label());
            if (i == current) {
                highlightStart = start;
                highlightEnd = text.length();
            }
        }

        context.setupUIComponentPresentation(
                text.toString(),
                highlightStart,
                highlightEnd,
                false,
                false,
                false,
                context.getDefaultParameterColor()
        );
    }

    /**
     * Named arguments may be written in any order, so the caret's slot index
     * alone does not identify the parameter. Prefer the key actually being
     * typed; otherwise point at the first parameter not yet supplied; fall back
     * to the positional slot.
     */
    private static int highlightIndex(
            @Nullable FelidaeCallResolver.ResolvedCall resolved,
            @NotNull FelidaeCallContext call
    ) {
        if (resolved == null || resolved.params().isEmpty()) return -1;
        List<FelidaeCallResolver.Param> params = resolved.params();

        String typed = call.keyBeingTyped();
        if (typed != null) {
            for (int i = 0; i < params.size(); i++) {
                if (params.get(i).name().equals(typed)) return i;
            }
        }
        for (int i = 0; i < params.size(); i++) {
            if (!call.suppliedKeys().contains(params.get(i).name())) return i;
        }
        return Math.min(call.activeParameter(), params.size() - 1);
    }

    private static @Nullable FelidaeCallContext callContext(@Nullable PsiFile file, int offset) {
        if (file == null || file.getFileType() != FelidaeFileType.INSTANCE) return null;
        return FelidaeCallContext.at(file.getText(), offset);
    }

    private static @Nullable FelidaeCallResolver.ResolvedCall resolveAt(
            @Nullable PsiFile file,
            int offset
    ) {
        FelidaeCallContext call = callContext(file, offset);
        if (call == null) return null;
        FelidaeCallResolver.ResolvedCall resolved =
                FelidaeCallResolver.resolve(file.getText(), call.callName());
        return resolved == null || resolved.params().isEmpty() ? null : resolved;
    }
}
