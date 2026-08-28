# Authoritative native-package allowlist.
#
# The runtime reads these declarations before accepting a DLL manifest.  A
# manifest may describe fewer capabilities, but never capabilities beyond the
# corresponding package entry here.

NativePackage(name: "smoke", wrapper: "core/smoke.fx", declaration: "core/smoke.fx", abi: 1, manifest: true, capabilities: [])
NativePackage(name: "db", wrapper: "core/db.fx", declaration: "core/system/flibrary/db.fx", abi: 1, manifest: true, capabilities: [])
NativePackage(name: "http", wrapper: "core/http.fx", declaration: "core/system/flibrary/http.fx", abi: 1, manifest: true, capabilities: [])
NativePackage(name: "process", wrapper: "core/process.fx", declaration: "core/system/flibrary/process.fx", abi: 1, manifest: true, capabilities: [])
NativePackage(name: "plot", wrapper: "core/plot.fx", declaration: "core/system/flibrary/plot.fx", abi: 1, manifest: true, capabilities: [])
NativePackage(name: "gtk", wrapper: "core/gtk.fx", declaration: "core/system/flibrary/gtk.fx", abi: 1, manifest: true, capabilities: [])
NativePackage(name: "qt", wrapper: "core/qt.fx", declaration: "core/system/flibrary/qt.fx", abi: 1, manifest: true, capabilities: [])
NativePackage(name: "set", wrapper: "core/set.fx", declaration: "core/system/flibrary/set.fx", abi: 1, manifest: true, capabilities: ["pure", "thread_safe", "batch", "fact_selections"])
NativePackage(name: "group", wrapper: "core/group.fx", declaration: "core/system/flibrary/group.fx", abi: 1, manifest: true, capabilities: ["pure", "thread_safe", "batch", "fact_selections"])
NativePackage(name: "fact_analysis", wrapper: "core/fact_analysis.fx", declaration: "core/system/flibrary/fact_analysis.fx", abi: 1, manifest: true, capabilities: ["pure", "thread_safe", "fact_selections"])
NativePackage(name: "wordnet", wrapper: "core/wordnet.fx", declaration: "core/system/flibrary/wordnet.fx", abi: 1, manifest: true, capabilities: ["pure", "thread_safe", "fact_projection"])
NativePackage(name: "fact", wrapper: "core/fact.fx", declaration: "core/system/flibrary/fact.fx", abi: 1, manifest: true, capabilities: ["pure", "thread_safe", "fact_hierarchy"])
