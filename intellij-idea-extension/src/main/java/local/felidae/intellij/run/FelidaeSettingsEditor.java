package local.felidae.intellij.run;

import com.intellij.openapi.fileChooser.FileChooserDescriptorFactory;
import com.intellij.openapi.options.SettingsEditor;
import com.intellij.openapi.project.Project;
import com.intellij.openapi.ui.TextFieldWithBrowseButton;
import com.intellij.ui.components.JBLabel;
import com.intellij.util.ui.FormBuilder;
import org.jetbrains.annotations.NotNull;

import javax.swing.JComponent;
import javax.swing.JPanel;

public final class FelidaeSettingsEditor
        extends SettingsEditor<FelidaeRunConfiguration> {

    private final Project project;

    private final TextFieldWithBrowseButton sourceFileField =
            new TextFieldWithBrowseButton();

    private final TextFieldWithBrowseButton interpreterField =
            new TextFieldWithBrowseButton();

    private final TextFieldWithBrowseButton workingDirectoryField =
            new TextFieldWithBrowseButton();

    private final com.intellij.ui.components.JBTextField
            argumentsField =
            new com.intellij.ui.components.JBTextField();

    private final JPanel panel;

    public FelidaeSettingsEditor(
            @NotNull Project project
    ) {
        this.project = project;

        sourceFileField.addBrowseFolderListener(
                project,
                FileChooserDescriptorFactory
                        .createSingleFileDescriptor("fx")
                        .withTitle("Select Felidae Source File")
        );

        interpreterField.addBrowseFolderListener(
                project,
                FileChooserDescriptorFactory
                        .createSingleFileNoJarsDescriptor()
                        .withTitle("Select Felidae Interpreter")
        );

        workingDirectoryField.addBrowseFolderListener(
                project,
                FileChooserDescriptorFactory
                        .createSingleFolderDescriptor()
                        .withTitle("Select Working Directory")
        );

        panel = FormBuilder.createFormBuilder()
                .addLabeledComponent(
                        new JBLabel("Source file:"),
                        sourceFileField,
                        1,
                        false
                )
                .addLabeledComponent(
                        new JBLabel("Interpreter:"),
                        interpreterField,
                        1,
                        false
                )
                .addLabeledComponent(
                        new JBLabel("Program arguments:"),
                        argumentsField,
                        1,
                        false
                )
                .addLabeledComponent(
                        new JBLabel("Working directory:"),
                        workingDirectoryField,
                        1,
                        false
                )
                .addComponentFillVertically(
                        new JPanel(),
                        0
                )
                .getPanel();
    }

    @Override
    protected void resetEditorFrom(
            @NotNull FelidaeRunConfiguration configuration
    ) {
        sourceFileField.setText(
                configuration.getSourceFile()
        );

        interpreterField.setText(
                configuration.getInterpreterPath()
        );

        argumentsField.setText(
                configuration.getProgramArguments()
        );

        workingDirectoryField.setText(
                configuration.getWorkingDirectory()
        );
    }

    @Override
    protected void applyEditorTo(
            @NotNull FelidaeRunConfiguration configuration
    ) {
        configuration.setSourceFile(
                sourceFileField.getText().trim()
        );

        configuration.setInterpreterPath(
                interpreterField.getText().trim()
        );

        configuration.setProgramArguments(
                argumentsField.getText().trim()
        );

        configuration.setWorkingDirectory(
                workingDirectoryField.getText().trim()
        );
    }

    @Override
    protected @NotNull JComponent createEditor() {
        return panel;
    }
}