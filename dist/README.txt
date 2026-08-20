Felidae 0.2.3-beta.1 beta portable distribution

Run from this directory:
  felidae_compiler.exe program.fx
  felidae_vm.exe program.bin

The compiler writes .bin next to itself; use a disposable working copy or move the artifact after compilation.
FELBIN v8 is incompatible with legacy FELIR/.fir artifacts; recompile the .fx source.
models/felidae.model is required for SentencePiece encoding/decoding.
LibTorch DLLs are staged beside the executables when this distribution was built with runtime SSM support.
Verify shipped files with SHA256SUMS.txt before publishing.
