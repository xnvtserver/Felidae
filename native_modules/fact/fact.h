#pragma once

#if defined(_WIN32)
#define FELIDAE_FACT_EXPORT __declspec(dllexport)
#else
#define FELIDAE_FACT_EXPORT __attribute__((visibility("default")))
#endif

extern "C" {
FELIDAE_FACT_EXPORT char* felidae_native_call(const char* functionName, const char* argsJson);
FELIDAE_FACT_EXPORT void felidae_native_free(char* ptr);
}
