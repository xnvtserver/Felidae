# Facts carrying a date-like field, which is what Celidae's timeline template
# plots. Run:
#
#   celidae examples/timeline_facts.fx --html --template=timeline > timeline.html
#   celidae examples/timeline_facts.fx --html > all-views.html
#
# Any field whose literal looks like an ISO date (YYYY-MM-DD) or a bare year
# is eligible; Celidae picks whichever field carries one in the most records.

Release(name: "0.1.0", date: "2024-03-14", scope: "prototype")
Release(name: "0.2.0", date: "2024-07-02", scope: "parser")
Release(name: "0.3.0", date: "2025-01-20", scope: "facts")
Release(name: "1.0.0", date: "2025-09-08", scope: "stable")
Release(name: "1.1.0", date: "2026-02-11", scope: "operators")

Incident(name: "parser-regression", date: "2024-08-19", severity: "minor")
Incident(name: "import-cycle", date: "2025-03-02", severity: "major")
Incident(name: "cache-staleness", date: "2025-11-27", severity: "minor")

Milestone extend Release(name: "public-launch", date: "2025-09-08", scope: "stable")

main() =>
    system.print(value: "timeline example: visualize with celidae --template=timeline")
    return
