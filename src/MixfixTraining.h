#pragma once

namespace Felidae {

// Runs the existing C++/LibTorch mixfix trainer.  The compiler CLI owns its
// public option parsing; this keeps one training implementation.
int runMixfixGruTraining(int argc, char** argv);

} // namespace Felidae
