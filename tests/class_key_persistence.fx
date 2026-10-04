class School
    key(id).
    def id: string.
    def name: string.
end

def School(id: "school-1", name: "Central").
def School(id: "school-1", name: "Central").

def main() =>
    def west := new School(id: "school-2", name: "West").save().
    return (
        count: School.count(),
        saved_name: west.name,
        stored_name: School.where(id: "school-2").get(pos: 0).name
    ).
end
