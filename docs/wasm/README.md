# Felidae WASM Playground Runtime

This directory is the output target for the browser playground interpreter.

Build it with Emscripten active on PATH:

```bash
# Windows
.\build.cmd wasm

# Linux/macOS
./build.sh --target wasm
```

Or build it through Docker without installing `em++` on the host:

```bash
./build/felidae wasm.fx
```

`wasm.fx` calls `emcc.fx`, which uses the common `docker.fx` helpers with the `emscripten/emsdk:latest` image, mounts the repository at `/src`, and runs `./build.sh --target wasm` inside the container.

The command emits `felidae_wasm.js`, `felidae_wasm.wasm`, and `felidae_wasm.data`. The docs playground loads those files from `wasm/felidae_wasm.js` and executes examples through the same parser and interpreter core used by the native CLI.

If `em++` is missing, either install and activate the Emscripten SDK first or use the Docker helper, then rerun the build and restart the docs server.
