# `system.printf` resolves placeholders from the active scope. Escapes in the
# string are normal string escapes, so a newline is written with `\n`.

main() =>
    data := "Felidae"
    score := 0.92
    decision := {applicant: "Ava", state: "approved"}
    system.printf("Hello {data}\nDecision: {decision.applicant} is {decision.state}\nScore: {score}\n")
    return "printf complete"
