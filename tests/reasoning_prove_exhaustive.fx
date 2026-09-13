# Reasoning.prove's DerivationResult carries an explicit `exhaustive: true`
# field: tableCallAnswers computes a predicate's complete tabled fixpoint -
# every answer the current fact base can derive, never a capped search - so
# this is never a guess or a silent partial answer.
Animal(name: "cat").
Animal(name: "dog").

def main() =>
    return Reasoning.prove(query: Animal(name: "cat"))
