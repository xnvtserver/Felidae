package local.felidae.intellij;

import com.intellij.codeInsight.navigation.actions.GotoDeclarationHandler;
import com.intellij.openapi.editor.Editor;
import com.intellij.openapi.fileEditor.OpenFileDescriptor;
import com.intellij.openapi.project.Project;
import com.intellij.openapi.util.TextRange;
import com.intellij.openapi.vfs.VirtualFile;
import com.intellij.psi.PsiElement;
import com.intellij.psi.PsiFile;
import com.intellij.psi.PsiManager;
import com.intellij.psi.impl.FakePsiElement;
import com.intellij.psi.search.FileTypeIndex;
import com.intellij.psi.search.GlobalSearchScope;
import org.jetbrains.annotations.NotNull;
import org.jetbrains.annotations.Nullable;

import java.util.ArrayList;
import java.util.List;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/**
 * Regex-based Go to Declaration (Ctrl+B / Ctrl+Click), mirroring the VS Code
 * extension's text-scan FelidaeDefinitionProvider. There is no PSI grammar
 * for Felidae (.fx files are PsiPlainTextFile), so a real PsiElement at the
 * matched offset would be a single whole-file leaf and navigate to offset 0
 * instead of the declaration line. FakePsiElement.navigate() sidesteps that
 * by opening an OpenFileDescriptor at the exact offset directly.
 */
public final class FelidaeGotoDeclarationHandler implements GotoDeclarationHandler {

    @Override
    public PsiElement @Nullable [] getGotoDeclarationTargets(
            @Nullable PsiElement sourceElement,
            int offset,
            Editor editor
    ) {
        if (sourceElement == null) {
            return null;
        }
        PsiFile file = sourceElement.getContainingFile();
        if (file == null || file.getFileType() != FelidaeFileType.INSTANCE) {
            return null;
        }

        CharSequence text = editor.getDocument().getImmutableCharSequence();
        String name = resolveName(text, offset);
        if (name == null) {
            return null;
        }

        Project project = file.getProject();
        Pattern declaration = declarationPattern(name);
        List<PsiElement> targets = new ArrayList<>();

        collectMatches(file.getVirtualFile(), project, declaration, targets);
        if (!targets.isEmpty()) {
            return targets.toArray(PsiElement.EMPTY_ARRAY);
        }

        for (VirtualFile candidate : FileTypeIndex.getFiles(FelidaeFileType.INSTANCE, GlobalSearchScope.projectScope(project))) {
            if (candidate.equals(file.getVirtualFile())) continue;
            collectMatches(candidate, project, declaration, targets);
            if (targets.size() > 50) break;
        }

        return targets.isEmpty() ? null : targets.toArray(PsiElement.EMPTY_ARRAY);
    }

    private static void collectMatches(
            @Nullable VirtualFile virtualFile,
            @NotNull Project project,
            @NotNull Pattern declaration,
            @NotNull List<PsiElement> targets
    ) {
        if (virtualFile == null) {
            return;
        }
        PsiFile psiFile = PsiManager.getInstance(project).findFile(virtualFile);
        if (psiFile == null) {
            return;
        }
        CharSequence text = psiFile.getViewProvider().getContents();
        Matcher matcher = declaration.matcher(text);
        while (matcher.find()) {
            int lineStart = matcher.start();
            while (lineStart > 0 && text.charAt(lineStart - 1) != '\n') lineStart--;
            targets.add(new NavigableDeclaration(psiFile, lineStart));
        }
    }

    private static @NotNull Pattern declarationPattern(@NotNull String name) {
        String escaped = Pattern.quote(name.replace('.', ':'));
        String dotted = escaped.replace(":", "\\E[:.]\\Q");
        return Pattern.compile("(?m)^[ \\t]*" + dotted + "\\s*\\(");
    }

    private static @Nullable String resolveName(@NotNull CharSequence text, int offset) {
        int start = Math.max(0, Math.min(offset, text.length()));
        int end = start;
        while (start > 0 && isNameChar(text.charAt(start - 1))) start--;
        while (end < text.length() && isNameChar(text.charAt(end))) end++;
        if (start >= end) return null;
        String raw = text.subSequence(start, end).toString().replaceAll("^[.:]+|[.:]+$", "");
        return raw.isEmpty() ? null : raw;
    }

    private static boolean isNameChar(char ch) {
        return Character.isLetterOrDigit(ch) || ch == '_' || ch == ':' || ch == '.';
    }

    private static final class NavigableDeclaration extends FakePsiElement {
        private final PsiFile containingFile;
        private final int offset;

        NavigableDeclaration(@NotNull PsiFile containingFile, int offset) {
            this.containingFile = containingFile;
            this.offset = offset;
        }

        @Override
        public PsiElement getParent() {
            return containingFile;
        }

        @Override
        public @NotNull PsiFile getContainingFile() {
            return containingFile;
        }

        @Override
        public TextRange getTextRange() {
            return TextRange.from(offset, 1);
        }

        @Override
        public int getTextOffset() {
            return offset;
        }

        @Override
        public void navigate(boolean requestFocus) {
            VirtualFile virtualFile = containingFile.getVirtualFile();
            if (virtualFile == null) return;
            new OpenFileDescriptor(containingFile.getProject(), virtualFile, offset).navigate(requestFocus);
        }

        @Override
        public String getPresentableText() {
            return containingFile.getName();
        }
    }
}
