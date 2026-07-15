# import "system".

Person(
    name: "Default",
    age: 0,
    country: "India"
).

Employee extend Person(
    name: "Ravi",
    age: 30,
    role: "Engineer"
).

Teacher extend Person(
    name: "Sita",
    age: 25,
    subject: "Math",
    school: "ABC School"
).

Student extend Person(
    name: "Anu",
    age: 17,
    class: "12"
).

Artist extend Person(
    name: "Ramesh",
    age: 20,
    art_form: "Painting",
    category: "Contemporary",
    location: "Mumbai"
).

Adults := lambda(Person, p => p.age >= 18).

Artists()=>
   return (result: Person(
        name: "Ramesh",
        age: 20,
        art_form: "Painting",
        category: "Contemporary",
        location: "Mumbai"
    )).

main(arguments: system.stdin) =>
    names := lambda(Adults, p => p.name),
    Artists(),
    return (
        count: count(Adults),
        names: names,
        args: arguments.args
    ).
