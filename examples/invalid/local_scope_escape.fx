Helper() =>
    temp := "inside"
    return (
        value: "ok"
    )

BadScope(result: result) =>
    Helper()
    result == temp
