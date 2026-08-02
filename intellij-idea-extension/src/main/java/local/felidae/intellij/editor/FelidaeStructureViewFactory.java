package local.felidae.intellij.editor;

import com.intellij.ide.structureView.StructureViewBuilder;
import com.intellij.ide.structureView.StructureViewModel;
import com.intellij.ide.structureView.StructureViewModelBase;
import com.intellij.ide.structureView.StructureViewTreeElement;
import com.intellij.ide.structureView.TreeBasedStructureViewBuilder;
import com.intellij.ide.util.treeView.smartTree.SortableTreeElement;
import com.intellij.ide.util.treeView.smartTree.Sorter;
import com.intellij.ide.util.treeView.smartTree.TreeElement;
import com.intellij.lang.PsiStructureViewFactory;
import com.intellij.navigation.ItemPresentation;
import com.intellij.openapi.editor.Editor;
import com.intellij.openapi.fileEditor.FileDocumentManager;
import com.intellij.psi.PsiElement;
import com.intellij.psi.PsiFile;
import local.felidae.intellij.FelidaeCallResolver;
import local.felidae.intellij.FelidaeFileType;
import local.felidae.intellij.FelidaeIcons;
import org.jetbrains.annotations.NotNull;
import org.jetbrains.annotations.Nullable;

import javax.swing.Icon;
import java.util.ArrayList;
import java.util.List;
import java.util.regex.Matcher;

/**
 * Structure view (Ctrl+F12 / the Structure tool window) for .fx files.
 *
 * <p>Without a PSI grammar there is no element tree to present, so entries are
 * derived from {@link FelidaeCallResolver#declarations} - the same source
 * completion, folding and Go to Declaration use - and each one navigates by
 * offset, like {@code FelidaeGotoDeclarationHandler} does.
 */
public final class FelidaeStructureViewFactory implements PsiStructureViewFactory {

    @Override
    public @Nullable StructureViewBuilder getStructureViewBuilder(@NotNull PsiFile psiFile) {
        if (psiFile.getFileType() != FelidaeFileType.INSTANCE) {
            return null;
        }
        return new TreeBasedStructureViewBuilder() {
            @Override
            public @NotNull StructureViewModel createStructureViewModel(@Nullable Editor editor) {
                return new Model(psiFile);
            }
        };
    }

    private static final class Model extends StructureViewModelBase
            implements StructureViewModel.ElementInfoProvider {

        Model(@NotNull PsiFile file) {
            super(file, new FileElement(file));
        }

        @Override
        public Sorter @NotNull [] getSorters() {
            return new Sorter[]{ Sorter.ALPHA_SORTER };
        }

        @Override
        public boolean isAlwaysShowsPlus(StructureViewTreeElement element) {
            return false;
        }

        @Override
        public boolean isAlwaysLeaf(StructureViewTreeElement element) {
            return !(element instanceof FileElement);
        }
    }

    /** Root node: the file, whose children are its declarations. */
    private static final class FileElement implements StructureViewTreeElement {
        private final PsiFile file;

        FileElement(@NotNull PsiFile file) {
            this.file = file;
        }

        @Override
        public Object getValue() {
            return file;
        }

        @Override
        public @NotNull ItemPresentation getPresentation() {
            return new Presentation(file.getName(), "", FelidaeIcons.FILE);
        }

        @Override
        public TreeElement @NotNull [] getChildren() {
            String text = file.getText();
            if (text == null || text.isEmpty()) return EMPTY_ARRAY;

            String[] lines = text.split("\n", -1);
            int[] lineStart = new int[lines.length];
            int offset = 0;
            for (int i = 0; i < lines.length; i++) {
                lineStart[i] = offset;
                offset += lines[i].length() + 1;
            }

            List<TreeElement> children = new ArrayList<>();
            for (FelidaeCallResolver.Declaration declaration : FelidaeCallResolver.declarations(text)) {
                StringBuilder signature = new StringBuilder("(");
                List<FelidaeCallResolver.Param> params =
                        FelidaeCallResolver.collectHeadParams(declaration.argsText());
                for (int i = 0; i < params.size(); i++) {
                    if (i > 0) signature.append(", ");
                    signature.append(params.get(i).label());
                }
                signature.append(")");

                children.add(new DeclarationElement(
                        file,
                        declaration.name(),
                        signature.toString(),
                        declaration.isMethod(),
                        declarationOffset(lines, lineStart, declaration.name())));
            }
            return children.toArray(EMPTY_ARRAY);
        }

        private static int declarationOffset(String[] lines, int[] lineStart, String name) {
            Matcher matcher = FelidaeCallResolver.declarationPatternFor(name).matcher("");
            for (int i = 0; i < lines.length; i++) {
                if (lines[i].isEmpty() || Character.isWhitespace(lines[i].charAt(0))) continue;
                matcher.reset(lines[i]);
                if (matcher.find()) return lineStart[i];
            }
            return 0;
        }
    }

    private static final class DeclarationElement
            implements StructureViewTreeElement, SortableTreeElement {

        private final PsiFile file;
        private final String name;
        private final String signature;
        private final boolean isMethod;
        private final int offset;

        DeclarationElement(PsiFile file, String name, String signature, boolean isMethod, int offset) {
            this.file = file;
            this.name = name;
            this.signature = signature;
            this.isMethod = isMethod;
            this.offset = offset;
        }

        @Override
        public Object getValue() {
            return name + "@" + offset;
        }

        @Override
        public @NotNull String getAlphaSortKey() {
            return name;
        }

        @Override
        public @NotNull ItemPresentation getPresentation() {
            return new Presentation(
                    name,
                    signature,
                    isMethod ? FelidaeIcons.ACTION : FelidaeIcons.FILE);
        }

        @Override
        public TreeElement @NotNull [] getChildren() {
            return EMPTY_ARRAY;
        }

        @Override
        public void navigate(boolean requestFocus) {
            // Same approach as FelidaeGotoDeclarationHandler: there is no PSI
            // element at this offset to delegate to, so open the file directly.
            if (file.getVirtualFile() == null) return;
            new com.intellij.openapi.fileEditor.OpenFileDescriptor(
                    file.getProject(), file.getVirtualFile(), offset).navigate(requestFocus);
        }

        @Override
        public boolean canNavigate() {
            return file.getVirtualFile() != null;
        }

        @Override
        public boolean canNavigateToSource() {
            return canNavigate();
        }
    }

    private record Presentation(String text, String location, Icon icon) implements ItemPresentation {
        @Override
        public @Nullable String getPresentableText() {
            return text;
        }

        @Override
        public @Nullable String getLocationString() {
            return location;
        }

        @Override
        public @Nullable Icon getIcon(boolean unused) {
            return icon;
        }
    }
}
