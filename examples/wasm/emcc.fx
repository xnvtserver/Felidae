import ("docker.fx", "str").

EmscriptenImage() =>
    return ("emscripten/emsdk:latest").

EmccBuildScript(target: string) =>
    prefix := "chmod +x ./build.sh && ./build.sh --target ",
    return (str.concat(left: prefix, right: target)).

EmccBuildTarget(target: string) =>
    image := EmscriptenImage(),
    script := EmccBuildScript(target: target),
    docker := DockerRunInRepo(image: image, script: script),
    return (
        status: "emscripten build finished",
        target: target,
        image: image,
        platform: docker.platform,
        command: docker.command,
        output: docker.output
    ).

main() =>
    return (EmccBuildTarget(target: "wasm")).
