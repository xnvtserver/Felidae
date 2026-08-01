package local.felidae.intellij.editor;

import com.intellij.openapi.actionSystem.ActionGroup;
import com.intellij.openapi.actionSystem.ActionManager;
import com.intellij.openapi.actionSystem.AnAction;
import com.intellij.openapi.actionSystem.AnActionEvent;
import com.intellij.openapi.actionSystem.DefaultActionGroup;
import com.intellij.openapi.editor.Document;
import com.intellij.openapi.editor.Editor;
import com.intellij.openapi.editor.event.DocumentEvent;
import com.intellij.openapi.editor.event.DocumentListener;
import com.intellij.openapi.editor.event.EditorFactoryEvent;
import com.intellij.openapi.editor.event.EditorFactoryListener;
import com.intellij.openapi.editor.markup.HighlighterLayer;
import com.intellij.openapi.editor.markup.HighlighterTargetArea;
import com.intellij.openapi.editor.markup.MarkupModel;
import com.intellij.openapi.editor.markup.RangeHighlighter;
import com.intellij.openapi.editor.markup.GutterIconRenderer;
import com.intellij.openapi.fileEditor.FileDocumentManager;
import com.intellij.openapi.project.Project;
import com.intellij.openapi.vfs.VirtualFile;
import local.felidae.intellij.FelidaeFileType;
import local.felidae.intellij.FelidaeIcons;
import local.felidae.intellij.execution.FelidaeExecutableResolver;
import local.felidae.intellij.execution.FelidaeProcessRunner;
import local.felidae.intellij.ui.FelidaeConsoleService;
import org.jetbrains.annotations.NotNull;
import org.jetbrains.annotations.Nullable;

import javax.swing.Icon;
import java.nio.file.Path;
import java.util.Map;
import java.util.Objects;
import java.util.concurrent.ConcurrentHashMap;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/**
 * Adds a "Run Felidae main()" gutter icon without relying on a PSI grammar.
 *
 * There is no ParserDefinition for Felidae, so .fx files fall back to a
 * single-leaf PsiPlainTextFile at the PSI layer; a LineMarkerProvider would
 * have no reliable per-line anchor to attach to. Instead this scans the raw
 * Document text directly (the same approach FelidaeExternalAnnotator already
 * uses for diagnostics) and manages a RangeHighlighter/GutterIconRenderer by
 * hand, refreshed on every document change.
 */
public final class FelidaeRunGutterManager implements EditorFactoryListener {

    private static final Pattern MAIN_PATTERN =
            Pattern.compile("(?m)^[ \\t]*main[ \\t]*\\(");

    private final Map<Editor, RangeHighlighter> highlighters = new ConcurrentHashMap<>();

    @Override
    public void editorCreated(@NotNull EditorFactoryEvent event) {
        Editor editor = event.getEditor();
        Project project = editor.getProject();
        if (project == null) {
            return;
        }

        VirtualFile file = FileDocumentManager.getInstance().getFile(editor.getDocument());
        if (!isFelidaeFile(file)) {
            return;
        }

        refresh(editor, project, file);

        editor.getDocument().addDocumentListener(new DocumentListener() {
            @Override
            public void documentChanged(@NotNull DocumentEvent event) {
                refresh(editor, project, file);
            }
        }, () -> highlighters.remove(editor));
    }

    @Override
    public void editorReleased(@NotNull EditorFactoryEvent event) {
        highlighters.remove(event.getEditor());
    }

    private static boolean isFelidaeFile(@Nullable VirtualFile file) {
        if (file == null || file.isDirectory()) {
            return false;
        }
        String extension = file.getExtension();
        return extension != null && FelidaeFileType.DEFAULT_EXTENSION.equalsIgnoreCase(extension);
    }

    private void refresh(@NotNull Editor editor, @NotNull Project project, @NotNull VirtualFile file) {
        RangeHighlighter existing = highlighters.remove(editor);
        if (existing != null) {
            existing.dispose();
        }
        if (project.isDisposed() || editor.isDisposed()) {
            return;
        }

        Document document = editor.getDocument();
        Matcher matcher = MAIN_PATTERN.matcher(document.getImmutableCharSequence());
        if (!matcher.find()) {
            return;
        }

        int line = document.getLineNumber(matcher.start());
        int lineStart = document.getLineStartOffset(line);
        int lineEnd = document.getLineEndOffset(line);

        MarkupModel markupModel = editor.getMarkupModel();
        RangeHighlighter highlighter = markupModel.addRangeHighlighter(
                lineStart,
                lineEnd,
                HighlighterLayer.ADDITIONAL_SYNTAX,
                null,
                HighlighterTargetArea.EXACT_RANGE
        );
        highlighter.setGutterIconRenderer(new FelidaeRunGutterIconRenderer(project, file));
        highlighters.put(editor, highlighter);
    }

    private static final class FelidaeRunGutterIconRenderer extends GutterIconRenderer {

        private final Project project;
        private final VirtualFile file;

        FelidaeRunGutterIconRenderer(@NotNull Project project, @NotNull VirtualFile file) {
            this.project = project;
            this.file = file;
        }

        @Override
        public @NotNull Icon getIcon() {
            return FelidaeIcons.RUN;
        }

        @Override
        public @NotNull String getTooltipText() {
            return "Run Felidae main() (right-click for Check / Visualize)";
        }

        @Override
        public @NotNull AnAction getClickAction() {
            return new AnAction() {
                @Override
                public void actionPerformed(@NotNull AnActionEvent event) {
                    runMain();
                }
            };
        }

        @Override
        public @NotNull ActionGroup getPopupMenuActions() {
            DefaultActionGroup group = new DefaultActionGroup();
            ActionManager actionManager = ActionManager.getInstance();
            for (String actionId : new String[] {"Felidae.RunFile", "Felidae.CheckFile", "Felidae.VisualizeFile"}) {
                AnAction action = actionManager.getAction(actionId);
                if (action != null) {
                    group.add(action);
                }
            }
            return group;
        }

        private void runMain() {
            if (project.isDisposed()) {
                return;
            }
            Path interpreter = FelidaeExecutableResolver.resolveInterpreter(project);
            if (interpreter == null) {
                FelidaeConsoleService console = FelidaeConsoleService.getInstance(project);
                console.show();
                console.clear();
                console.printError(
                        "Felidae interpreter was not found.\n\n" +
                                "Expected location:\n" +
                                "  <project>/build/felidae.exe\n\n" +
                                "Alternatively, configure the FELIDAE_PATH environment variable.\n"
                );
                return;
            }

            Path sourceFile;
            try {
                sourceFile = Path.of(file.getPath()).toAbsolutePath().normalize();
            } catch (Exception exception) {
                return;
            }

            FelidaeProcessRunner.runInterpreter(project, interpreter, sourceFile);
        }

        @Override
        public boolean equals(Object obj) {
            return obj instanceof FelidaeRunGutterIconRenderer other && Objects.equals(other.file, file);
        }

        @Override
        public int hashCode() {
            return file.hashCode();
        }
    }
}
