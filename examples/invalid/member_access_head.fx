Employee(name: "Alice", role: "Engineer", manager: "Bob").

HasManager(name: e.name) =>
    Employee(e),
    e.manager != nil.
