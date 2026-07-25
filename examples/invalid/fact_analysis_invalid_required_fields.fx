import ("flibrary", "system.flibrary.fact_analysis")

main() =>
    need := {category: "vegetable"}
    candidates := [{category: "vegetable", name: "Tomato"}]
    return system_library_loader(module: "fact_analysis", function: "find_nearest_where", args: {input: need, candidates: candidates, count: 1, required_fields: "category"})
