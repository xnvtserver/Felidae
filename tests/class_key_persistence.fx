class School
    key(id).
    id: string.
    name: string.
end

School(id: "school-1", name: "Central").
School(id: "school-1", name: "Central").

def main() =>
    west := new School(id: "school-2", name: "West").save().
    return (
        count: School.count(),
        saved_name: west.name,
        stored_name: School.where(id: "school-2").get(pos: 0).name
    ).
end
