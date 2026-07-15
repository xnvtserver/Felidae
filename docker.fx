import ("process" "str").

DockerVersion() =>
    return (process.exec(command: "docker --version")).

DockerRepoVolume(platform: string) =>
    platform == "windows",
    return ("\"%cd%:/src\"")
else
    return ("\"$PWD:/src\"").

DockerRunCommand(platform: string, image: string, script: string) =>
    volume := DockerRepoVolume(platform: platform),
    prefix := "docker run --rm -v ",
    withVolume := str.concat(left: prefix, right: volume),
    withWorkdir := str.concat(left: withVolume, right: " -w /src "),
    withImage := str.concat(left: withWorkdir, right: image),
    withShell := str.concat(left: withImage, right: " bash -lc \""),
    withScript := str.concat(left: withShell, right: script),
    return (str.concat(left: withScript, right: "\"")).

DockerRunInRepo(image: string, script: string) =>
    platform := process.platform(),
    command := DockerRunCommand(platform: platform, image: image, script: script),
    output := process.exec(command: command),
    return (
        platform: platform,
        image: image,
        command: command,
        output: output
    ).

main() =>
    docker := DockerVersion(),
    platform := process.platform(),
    return (
        status: "docker available",
        docker: docker,
        platform: platform
    ).
