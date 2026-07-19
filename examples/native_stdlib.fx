import "file".
import "math".
import "ml".

main(arguments: system.stdin) =>
    writeStatus := file.writeFile(path: "build/native_stdlib.tmp", data: "Felidae IO"),
    readBack := file.readFile(path: "build/native_stdlib.tmp"),
    exists := file.exists(path: "build/native_stdlib.tmp"),
    root := math.sqrt(value: 81),
    powered := math.pow(base: 2, exponent: 8),
    activation := ml.sigmoid(value: 0),
    dot := ml.dot(left: [1, 2, 3], right: [4, 5, 6]),
    mse := ml.meanSquaredError(left: [1, 2, 3], right: [1, 2, 5]),
    return (
        writeStatus: writeStatus,
        readBack: readBack,
        exists: exists,
        root: root,
        powered: powered,
        activation: activation,
        dot: dot,
        mse: mse
    ).
