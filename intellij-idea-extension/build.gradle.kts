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

    // Gson replaces the hand-rolled JSON readers this plugin used to carry for
    // builtin-docs.json and the ML model files. Those were written to avoid a
    // dependency, but the string-only one silently dropped 125 of 126 doc
    // entries the moment builtin-docs.json gained an array field - exactly the
    // failure a real parser cannot have. Bundled rather than compileOnly so
    // the plugin does not depend on which libraries a given IDE build exposes.
    implementation("com.google.code.gson:gson:2.11.0")
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

// Bundles the ranking models trained offline by the repo-root ml/ pipeline
// (see ml/README.md). Only the models ship - the plugin scores them with
// FelidaeGbdt and bundles no ML library. Absent models simply disable ranking.
tasks.register("generateMlModels") {
    val sourceDir = file("$projectDir/../ml/models")
    val destDir = file("$projectDir/src/main/resources/models")
    val modelFiles = listOf("completion-rank.json", "next-param.json", "corpus-index.json")

    // Not tracked, and not a Copy task: on a OneDrive-backed checkout the
    // model files can be cloud placeholders rather than regular files, and
    // Gradle fails outright trying to snapshot those ("not a regular file").
    // Reading them through a normal stream materialises them on demand, and
    // this is three small files - incremental tracking buys nothing.
    doNotTrackState("model files may be cloud placeholders that cannot be snapshotted")

    doLast {
        if (!sourceDir.isDirectory) {
            logger.lifecycle(
                "generateMlModels: ${sourceDir.path} not found; ranking stays disabled. " +
                    "Run `npm install && npm run train` in ml/ to produce it."
            )
            return@doLast
        }
        destDir.mkdirs()
        var copied = 0
        for (name in modelFiles) {
            val source = File(sourceDir, name)
            if (!source.exists()) {
                logger.lifecycle("generateMlModels: missing $name, skipping")
                continue
            }
            source.inputStream().use { input ->
                File(destDir, name).outputStream().use { output -> input.copyTo(output) }
            }
            copied++
        }
        logger.lifecycle("generateMlModels: copied $copied/${modelFiles.size} model files")
    }
}

tasks.named("processResources") {
    dependsOn("generateBuiltinDocs", "generateMlModels")
}