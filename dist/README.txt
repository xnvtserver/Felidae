Felidae 0.2.3-beta.1 beta portable distribution

Run from this directory:
  felidae_compiler.exe program.fx
  felidae_vm.exe program.fir

The compiler writes .fir next to itself; use a disposable working copy or move the artifact after compilation.
models/felidae.model is required for SentencePiece encoding/decoding.
LibTorch DLLs are staged beside the executables when this distribution was built with runtime SSM support.
Verify shipped executable/model files with SHA256SUMS.txt before publishing.
