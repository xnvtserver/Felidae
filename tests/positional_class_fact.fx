class Employee
    key(name).
    def name: string.
    def age: number.
    def medium: atom.
end

def Employee("ram", 10, english_medium).
def Employee(name: "maya", age: 20, medium: english_medium).

def main() =>
    def transient := Employee("sam", 11, english_medium).
    def persisted := Employee.where(name: "ram").first().
    (
        count: Employee.count(),
        persisted_name: persisted.name,
        persisted_age: persisted.age,
        persisted_medium: persisted.medium,
        persisted_atom: is_atom(persisted.medium),
        transient_name: transient.name,
        transient_age: transient.age,
        transient_medium: transient.medium
    ).
end
