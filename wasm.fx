import ("emcc.fx" "file").

WasmArtifactStatus() =>
    js := file.exists(path: "docs/wasm/felidae_wasm.js"),
    wasm := file.exists(path: "docs/wasm/felidae_wasm.wasm"),
    data := file.exists(path: "docs/wasm/felidae_wasm.data"),
    return (
        js: js,
        wasm: wasm,
        data: data
    ).

BuildFelidaeWasm() =>
    build := EmccBuildTarget(target: "wasm"),
    artifacts := WasmArtifactStatus(),
    return (
        status: "felidae wasm build finished",
        buildStatus: build.status,
        image: build.image,
        platform: build.platform,
        command: build.command,
        output: build.output,
        artifactJs: artifacts.js,
        artifactWasm: artifacts.wasm,
        artifactData: artifacts.data
    ).

main() =>
    return (BuildFelidaeWasm()).
