#pragma once

#if defined(_WIN32)
#if defined(FELIDAE_PLOT_BUILD)
#define FELIDAE_PLOT_EXPORT __declspec(dllexport)
#else
#define FELIDAE_PLOT_EXPORT __declspec(dllimport)
#endif
#else
#define FELIDAE_PLOT_EXPORT __attribute__((visibility("default")))
#endif

extern "C" FELIDAE_PLOT_EXPORT char* felidae_native_call(const char* functionName, const char* argsJson);
extern "C" FELIDAE_PLOT_EXPORT void felidae_native_free(char* value);
