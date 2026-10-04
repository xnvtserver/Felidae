class School
    def name: string.
    def district: string.
    def students: number.
    def active: number.
end

def make_school(name: string, district: string, students: number) =>
    return School.insert(values: {
        name: name,
        district: district,
        students: students,
        active: 1.0
    }).
end

def main() =>
    def inserted := make_school(
        name: "Function School",
        district: "central",
        students: 240
    ).
    def matches := School.where(name: "Function School").
    return (
        inserted: inserted,
        count: matches.len(),
        stored: matches.get(position: 0)
    ).
end
