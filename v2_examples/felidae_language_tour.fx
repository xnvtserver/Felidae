# Felidae language tour.
#
# One coherent domain (a small school system) walking through the language's
# core surface end to end: fact hierarchy, guard clauses, fact queries,
# joins, aggregates, DML (insert/update/delete), mixfix syntax, and ancestry
# reasoning. Every section is runnable on its own; main() ties them together
# into one result record so the whole tour can be verified in a single run.

# --- 0. Imports --------------------------------------------------------------
# `import "csv"` brings in a real library module; csv.toFacts turns CSV rows
# into facts of a named type, with `source:` recording which file owns them
# so a later DML write persists back to that file automatically.
import "csv".

def ImportedSchool(name: "", district: "", students: 0, active: 1.0).

def importExample() =>
    def raw := file.readFile(file: "datasets/examples/schools.csv").
    def imported := csv.toFacts(data: raw, type: "ImportedSchool", source: "build/runtime/language_tour_schools.csv").
    count(data: imported).
end

# --- 1. Facts and hierarchy -------------------------------------------------
# `extend` builds an ancestry: Mammal and Reptile both specialize Animal, so
# hierarchy queries (section 8) find their shared ancestor without either
# type knowing about the other.
def Animal(name: "").
def Mammal extend Animal(name: "").
def Reptile extend Animal(name: "").

def School(id: 10, name: "North", district: "central", students: 420, active: 1.0).
def School(id: 20, name: "West", district: "west", students: 280, active: 0.0).
def School(id: 30, name: "Lake", district: "central", students: 350, active: 1.0).
def School(id: 99, name: "Remote", district: "remote", students: 25, active: 1.0).
def Teacher(name: "Ada", subject: "math", school_id: 10).
def Teacher(name: "Grace", subject: "science", school_id: 99).
Link(from: Teacher(name: "Ada"), to: School(id: 10), properties: {kind: "teaches_at"}).
Link(from: Teacher(name: "Grace"), to: School(id: 99), properties: {kind: "teaches_at"}).

# --- 2. Conditional expressions ---------------------------------------------
# Interpreter conditions are strictly boolean; numeric fuzzy degrees remain
# ordinary library values and must be compared explicitly.
def classifyEnrollment(count: number) =>
    count >= 400 then "large" else "standard".
end

# --- 3. Fact queries: where / AndWhere / OrWhere / limit --------------------
# `.where(...)` filters by named-field equality; chaining `.AndWhere`/
# `.OrWhere` composes further conditions left to right, and `.limit(records:)`
# bounds the result without truncating silently on invalid input.
def queryExamples() =>
    def active_central := School.where(district: "central", active: 1.0).
    def central_or_west := School.where(district: "central").OrWhere(district: "west").
    def large_central := School.where(district: "central").AndWhere(active: 1.0).
    def top_one := School.where(active: 1.0).limit(records: 1).
    (
        active_central_count: count(data: active_central),
        central_or_west_count: count(data: central_or_west),
        large_central_count: count(data: large_central),
        top_one_count: count(data: top_one)
    ).
end

# --- 4. Projection: select ---------------------------------------------------
# `.select(fields:, match:)` returns only the requested fields, never the
# whole fact -- useful once a query is answering a specific question rather
# than handing back full records.
def projectionExample() =>
    School.select(fields: ["name", "district"], match: {active: 1.0}).
end

# --- 5. Aggregates: count / sum / average / min / max -----------------------
# Each aggregate accepts the same optional `match:` a query would use.
def aggregateExamples() =>
    (
        total_schools: School.count(),
        total_students: School.sum(field: "students"),
        average_students: School.average(field: "students", match: {active: 1.0}),
        smallest: School.min(field: "students"),
        largest: School.max(field: "students")
    ).
end

# --- 6. Joins ---------------------------------------------------------------
# A Link is durable; a join result is an ephemeral bounded cursor result and
# never enters fact storage. The source selection can use a primary key or
# index, Link properties select an edge family, and a trailing where filters
# left/properties/right fields.
def joinExamples() =>
    def joined := School().join(properties: {kind: "teaches_at"}, direction: backward.class).
    def ada := joined.where(right.name = "Ada").
    (joined_count: count(data: joined), ada_count: count(data: ada)).
end

# --- 7. DML: insert / update / delete ----------------------------------------
# `match:` is mandatory for update and delete -- there is no conditionless
# mutation, unlike a bare SQL UPDATE with no WHERE.
def mutationExamples() =>
    def inserted := School.insert(values: {id: 40, name: "Riverside", district: "east", students: 210, active: 1.0}).
    def updated := School.where(district: "east").update(values: {students: 230.0}).
    def deleted := School.where(name: "Riverside").delete().
    (
        inserted_name: inserted.name,
        updated_count: updated,
        deleted_count: deleted
    ).
end

# --- 8. Ancestry reasoning ---------------------------------------------------
# commonAncestors reads the `extend` graph declared in section 1. Selecting
# one candidate as contextually best belongs to the calling application.
def ancestryExamples() =>
    def cat := Mammal(name: "cat").
    def lizard := Reptile(name: "lizard").
    (
        common: commonAncestors(left: cat, right: lizard)
    ).
end

# --- 9. Mixfix syntax --------------------------------------------------------
# @mixfix declares a natural-language-shaped call pattern; it lowers to an
# ordinary call underneath, so it composes with everything above it.
@mixfix(pattern: '{value:number} rated above {minimum:number}')
def ratedAbove() =>
    value > minimum.
end

def mixfixExample() =>
    82 rated above 75.
end

def main() =>
    (
        imported_count: importExample(),
        classification: classifyEnrollment(count: 420),
        queries: queryExamples(),
        projected: projectionExample(),
        aggregates: aggregateExamples(),
        joins: joinExamples(),
        mutations: mutationExamples(),
        ancestry: ancestryExamples(),
        mixfix: mixfixExample()
    ).
end
