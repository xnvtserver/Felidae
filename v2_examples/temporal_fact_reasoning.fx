# Temporal metadata is inferred from existing fields and kept outside the
# fact schema. Facts in one id lineage are ordered independently of others.

Employee(id: "e1", role: "analyst", salary: 40000, fx.effective_at: "2024-01-01", fx.provenance: "observed") as employees
Employee(id: "e1", role: "engineer", salary: 50000, fx.effective_at: "2025-01-01", fx.provenance: "observed") as employees
Employee(id: "e2", role: "analyst", salary: 42000, fx.effective_at: "2024-01-01", fx.provenance: "observed") as employees
Employee(id: "future", role: "architect", fx.effective_at: "2099-01-01", fx.provenance: "scheduled") as employees
Employee(id: "projection", role: "lead", fx.effective_at: "2099-06-01", fx.provenance: "predicted") as employees
Employee(id: "auto", role: "intern") as employees
Employee(id: "auto", role: "staff") as employees

main() =>
    current := system.run(value: "Fact.first(type: \"Employee\", field: \"id\", equals: \"e1\")")
    unrelated := system.run(value: "Fact.first(type: \"Employee\", field: \"id\", equals: \"e2\")")
    automaticCurrent := system.run(value: "Fact.first(type: \"Employee\", field: \"id\", equals: \"auto\")")
    employeeTimeline := Fact.timeline(selection: employees.role == "engineer")
    scheduledTimeline := Fact.timeline(selection: employees.id == "future")
    predictedTimeline := Fact.timeline(selection: employees.id == "projection")
    return (
        current_employee: current,
        unrelated_current_employee: unrelated,
        automatic_current_employee: automaticCurrent,
        employee_history: employeeTimeline,
        scheduled_knowledge: scheduledTimeline,
        predicted_knowledge: predictedTimeline
    )
