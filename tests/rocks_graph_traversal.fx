class Employee
    key(id).
    index(active).
    def id: string.
    def name: string.
    def active: bool.
end

def Employee(id: "e1", name: "Grace", active: true).
def Employee(id: "e2", name: "Ada", active: true).
def Employee(id: "e3", name: "Lin", active: true).
Link(from: Employee(id: "e1"), to: Employee(id: "e1"), properties: {kind: "reports_to"}).
Link(from: Employee(id: "e2"), to: Employee(id: "e1"), properties: {kind: "reports_to"}).
Link(from: Employee(id: "e3"), to: Employee(id: "e2"), properties: {kind: "reports_to"}).

def main() =>
    def target := Employee.where(id: "e1").get(pos: 0).
    def direct := Employee().where(active: true)
        .join(properties: {kind: "reports_to"}, direction: forward.class).
    def limited := Employee().where(active: true)
        .join(properties: {kind: "reports_to"}, direction: forward.class)
        .limit(records: 1).
    def both_from_source := Employee().where(id: "e1")
        .join(properties: {kind: "reports_to"}, direction: both.class).
    def recursive := Employee().where(id: "e3")
        .recursive_join(
            properties: {kind: "reports_to"},
            direction: forward.class,
            min_depth: 1,
            max_depth: 5
        ).
    def shortest := Employee().where(id: "e3")
        .shortest_path(
            to: target,
            properties: {kind: "reports_to"},
            direction: both.class,
            max_depth: 8
        ).
    (
        direct_count: direct.len(),
        limited_count: limited.len(),
        both_count: both_from_source.len(),
        recursive_count: recursive.len(),
        shortest_depth: shortest.get(position: 0).depth
    ).
end
