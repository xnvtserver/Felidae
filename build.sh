# instruction before installing on linux sys.
# check clang found on system if not found then install it.
# sudo apt install clang
# check build folder found on root dir if not create it 
# run build.sh -> it o/p 3 .exe file should generate under build folder
# if want vscode extention then 
    # vs-code-extension -> npx vsce package

# 
# Normal runtime build. This is the lean interpreter used by `felidae file.fx`.
clang++ -std=c++17 -Wall -Wextra -Isrc -Ithird_party src/main.cpp src/FelidaeRuntime.cpp src/Visualization.cpp src/Lexer.cpp src/Parser.cpp src/Interpreter.cpp src/Env.cpp src/Memory.cpp native_modules/csv/NativeCsv.cpp native_modules/http/NativeHttp.cpp native_modules/process/NativeProcess.cpp -o build/felidae.exe ${FELIDAE_DLOPEN_LIBS:-}

# Celidae is the debugger, analytics, and visualization product. It stays
# separate from the lean Felidae interpreter but uses the same runtime.
clang++ -std=c++17 -Wall -Wextra -Isrc -Ithird_party src/felidae_debug.cpp src/FelidaeRuntime.cpp src/Visualization.cpp src/Lexer.cpp src/Parser.cpp src/Interpreter.cpp src/Env.cpp src/Memory.cpp src/AstAnalyzer.cpp native_modules/csv/NativeCsv.cpp native_modules/http/NativeHttp.cpp native_modules/process/NativeProcess.cpp -o build/celidae.exe ${FELIDAE_DLOPEN_LIBS:-}

# Compatibility binary for older extensions/plugins.
clang++ -std=c++17 -Wall -Wextra -Isrc -Ithird_party src/felidae_debug.cpp src/FelidaeRuntime.cpp src/Visualization.cpp src/Lexer.cpp src/Parser.cpp src/Interpreter.cpp src/Env.cpp src/Memory.cpp src/AstAnalyzer.cpp native_modules/csv/NativeCsv.cpp native_modules/http/NativeHttp.cpp native_modules/process/NativeProcess.cpp -o build/felidae_debug.exe ${FELIDAE_DLOPEN_LIBS:-}
