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
import "csv"

ImportedSchool(name: "", district: "", students: 0, active: 1.0)

importExample() =>
    raw := file.readFile(file: "datasets/examples/schools.csv")
    imported := csv.toFacts(data: raw, type: "ImportedSchool", source: "build/runtime/language_tour_schools.csv")
    return count(data: imported)

# --- 1. Facts and hierarchy -------------------------------------------------
# `extend` builds an ancestry: Mammal and Reptile both specialize Animal, so
# hierarchy queries (section 8) find their shared ancestor without either
# type knowing about the other.
Animal(name: "")
Mammal extend Animal(name: "")
Reptile extend Animal(name: "")

School(id: 10, name: "North", district: "central", students: 420, active: 1.0)
School(id: 20, name: "West", district: "west", students: 280, active: 0.0)
School(id: 30, name: "Lake", district: "central", students: 350, active: 1.0)
Teacher(name: "Ada", subject: "math", school_id: 10)
Teacher(name: "Grace", subject: "science", school_id: 99)

# --- 2. Guard clauses --------------------------------------------------------
# `where` narrows a method to the cases it actually handles; the implicit
# `else` branch runs when the guard does not hold. Truth in Felidae is
# numeric: 1.0/0.0, never a separate boolean.
classifyEnrollment(count: number) =>
    where count >= 400
    return "large"
else
    return "standard"

# --- 3. Fact queries: where / AndWhere / OrWhere / limit --------------------
# `.where(...)` filters by named-field equality; chaining `.AndWhere`/
# `.OrWhere` composes further conditions left to right, and `.limit(records:)`
# bounds the result without truncating silently on invalid input.
queryExamples() =>
    active_central := School.where(district: "central", active: 1.0)
    central_or_west := School.where(district: "central").OrWhere(district: "west")
    large_central := School.where(district: "central").AndWhere(active: 1.0)
    top_one := School.where(active: 1.0).limit(records: 1)
    return (
        active_central_count: count(data: active_central),
        central_or_west_count: count(data: central_or_west),
        large_central_count: count(data: large_central),
        top_one_count: count(data: top_one)
    )

# --- 4. Projection: select ---------------------------------------------------
# `.select(fields:, match:)` returns only the requested fields, never the
# whole fact -- useful once a query is answering a specific question rather
# than handing back full records.
projectionExample() =>
    return School.select(fields: ["name", "district"], match: {active: 1.0})

# --- 5. Aggregates: count / sum / average / min / max -----------------------
# Each aggregate accepts the same optional `match:` a query would use.
aggregateExamples() =>
    return (
        total_schools: School.count(),
        total_students: School.sum(field: "students"),
        average_students: School.average(field: "students", match: {active: 1.0}),
        smallest: School.min(field: "students"),
        largest: School.max(field: "students")
    )

# --- 6. Joins: join / leftJoin / rightJoin / OuterJoin -----------------------
# A join result is ephemeral: it never enters fact storage on its own, so
# joining never inflates School.count() or Teacher.count(). Only an explicit
# .insert() persists anything.
joinExamples() =>
    inner := School.join(type: Teacher, left: "id", right: "school_id")
    left := School.leftJoin(type: Teacher, left: "id", right: "school_id")
    return (inner_count: count(data: inner), left_count: count(data: left))

# --- 7. DML: insert / update / delete ----------------------------------------
# `match:` is mandatory for update and delete -- there is no conditionless
# mutation, unlike a bare SQL UPDATE with no WHERE.
mutationExamples() =>
    inserted := School.insert(values: {id: 40, name: "Riverside", district: "east", students: 210, active: 1.0})
    updated := School.update(match: {district: "east"}, values: {students: 230})
    deleted := School.delete(match: {name: "Riverside"})
    return (
        inserted_name: inserted.name,
        updated_count: count(data: updated),
        deleted_count: deleted
    )

# --- 8. Ancestry reasoning ---------------------------------------------------
# commonAncestors/lowestCommonAncestor/highestCommonAncestor read the
# `extend` graph declared in section 1: Mammal and Reptile both resolve back
# to Animal without any fact-level relationship being declared between them.
ancestryExamples() =>
    cat := Mammal(name: "cat")
    lizard := Reptile(name: "lizard")
    return (
        common: commonAncestors(left: cat, right: lizard),
        lowest: lowestCommonAncestor(left: cat, right: lizard),
        highest: highestCommonAncestor(left: cat, right: lizard)
    )

# --- 9. Mixfix syntax --------------------------------------------------------
# @mixfix declares a natural-language-shaped call pattern; it lowers to an
# ordinary call underneath, so it composes with everything above it.
@mixfix(pattern: "{value:number} rated above {minimum:number}")
ratedAbove() => return value > minimum

mixfixExample() =>
    return 82 rated above 75

main() =>
    return (
        imported_count: importExample(),
        classification: classifyEnrollment(count: 420),
        queries: queryExamples(),
        projected: projectionExample(),
        aggregates: aggregateExamples(),
        joins: joinExamples(),
        mutations: mutationExamples(),
        ancestry: ancestryExamples(),
        mixfix: mixfixExample()
    )
