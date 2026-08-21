#pragma once

namespace Felidae {

// Runs the existing C++/LibTorch runtime trainer.  The VM CLI owns its public
// option parsing so no auxiliary trainer executable is needed.
int runRuntimeGruTraining(int argc, char** argv);

} // namespace Felidae
