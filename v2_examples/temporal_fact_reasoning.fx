# Temporal ranking is a deterministic ISA operation. It reads the named
# effective-time and priority fields and returns the current fact snapshot in
# descending temporal order.

Employee(id: "e1", role: "analyst", salary: 40000, fx.effective_at: 20240101, fx.priority: 1)
Employee(id: "e1", role: "engineer", salary: 50000, fx.effective_at: 20250101, fx.priority: 2)
Employee(id: "e2", role: "analyst", salary: 42000, fx.effective_at: 20240101, fx.priority: 1)

main() =>
    return temporalRank(effectiveAt: fx.effective_at, priority: fx.priority)
