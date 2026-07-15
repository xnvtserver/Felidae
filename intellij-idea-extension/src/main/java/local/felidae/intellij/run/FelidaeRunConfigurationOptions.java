package local.felidae.intellij.run;

import com.intellij.execution.configurations.RunConfigurationOptions;
import com.intellij.openapi.components.StoredProperty;
import org.jetbrains.annotations.NotNull;

public final class FelidaeRunConfigurationOptions
        extends RunConfigurationOptions {

    private final StoredProperty<String> sourceFile =
            string("")
                    .provideDelegate(
                            this,
                            "sourceFile"
                    );

    private final StoredProperty<String> interpreterPath =
            string("")
                    .provideDelegate(
                            this,
                            "interpreterPath"
                    );

    private final StoredProperty<String> programArguments =
            string("")
                    .provideDelegate(
                            this,
                            "programArguments"
                    );

    private final StoredProperty<String> workingDirectory =
            string("")
                    .provideDelegate(
                            this,
                            "workingDirectory"
                    );

    public @NotNull String getSourceFile() {
        return sourceFile.getValue(this);
    }

    public void setSourceFile(@NotNull String value) {
        sourceFile.setValue(this, value);
    }

    public @NotNull String getInterpreterPath() {
        return interpreterPath.getValue(this);
    }

    public void setInterpreterPath(@NotNull String value) {
        interpreterPath.setValue(this, value);
    }

    public @NotNull String getProgramArguments() {
        return programArguments.getValue(this);
    }

    public void setProgramArguments(@NotNull String value) {
        programArguments.setValue(this, value);
    }

    public @NotNull String getWorkingDirectory() {
        return workingDirectory.getValue(this);
    }

    public void setWorkingDirectory(@NotNull String value) {
        workingDirectory.setValue(this, value);
    }
}