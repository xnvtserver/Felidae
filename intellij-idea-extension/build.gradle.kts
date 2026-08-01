plugins {
    id("java")
    id("org.jetbrains.intellij.platform") version "2.9.0"
}

group = "local.felidae"
version = "0.0.1"

java {
    sourceCompatibility = JavaVersion.VERSION_21
    targetCompatibility = JavaVersion.VERSION_21
}

tasks.withType<JavaCompile>().configureEach {
    options.release.set(21)
}

repositories {
    mavenCentral()

    intellijPlatform {
        defaultRepositories()
    }
}

dependencies {
    intellijPlatform {
        // IntelliJ IDEA 2025.3 uses platform build 253.
        intellijIdea("2025.3")

        // Keep this only if the plugin uses Java-language APIs.
        bundledPlugin("com.intellij.java")
    }
}

intellijPlatform {
    pluginConfiguration {
        name = "Felidae"
        version = project.version.toString()

        ideaVersion {
            sinceBuild = "253"
            untilBuild = "253.*"
        }
    }
}

// Keeps the bundled stdlib hover-doc content in sync with the repo-root
// docs/builtin-docs.json, which is the single source of truth shared with
// the VS Code extension (see vs-code-extension/scripts/generate-builtin-docs.js).
tasks.register("generateBuiltinDocs") {
    val source = file("$projectDir/../docs/builtin-docs.json")
    val dest = file("$projectDir/src/main/resources/builtin-docs.json")
    inputs.file(source)
    outputs.file(dest)
    doLast {
        dest.parentFile.mkdirs()
        source.copyTo(dest, overwrite = true)
    }
}

tasks.named("processResources") {
    dependsOn("generateBuiltinDocs")
}