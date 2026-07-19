package local.felidae.intellij.run;

import com.intellij.execution.configurations.ConfigurationFactory;
import com.intellij.execution.configurations.ConfigurationType;
import com.intellij.execution.configurations.RunConfiguration;
import com.intellij.openapi.project.Project;
import org.jetbrains.annotations.NotNull;

public final class FelidaeConfigurationFactory
        extends ConfigurationFactory {

    public static final String ID =
            "FelidaeRunFactory";

    public FelidaeConfigurationFactory(
            @NotNull ConfigurationType type
    ) {
        super(type);
    }

    @Override
    public @NotNull String getId() {
        return ID;
    }

    @Override
    public @NotNull RunConfiguration createTemplateConfiguration(
            @NotNull Project project
    ) {
        return new FelidaeRunConfiguration(
                project,
                this,
                "Felidae"
        );
    }

    @Override
    public boolean isEditableInDumbMode() {
        return true;
    }
}