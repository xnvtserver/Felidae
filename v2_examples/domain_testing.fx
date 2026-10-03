class Domain
  key(id).
    id: number.
    name: string.
    description: string.
    def to_string() =>
      return name.
    end

    def to_json() =>
      return {id: id, name: name, description: description}.
    end

    def forward(input:optional<list<number>>) =>
    if input == nil then
      return nil.
    end
    
    for i in input then
           if i == 101 then
             return Domain(id: 1, name: "Phi", description: "A test domain").
           elif i == 102 then
             return Domain(id: 2, name: "Psi", description: "Another test domain").
         elif i == 103 then
             return Domain(id: 3, name: "Omega", description: "Yet another test domain").
           else
             return Domain(id: 0, name: "Unknown", description: "Unknown domain").
           end
        end
    end
end

class steps
  key(id).
    id: number.
    key:Fact.
end

def main() =>
  d := Domain(id: 1, name: "Phi", description: "A test domain").
  s := Domain.forward(input: [101,232,211,221]).
  return (domain: d.name, step_key: s.key.id).
end