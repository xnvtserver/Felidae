# A long recursive chain plus a wide mixfix verifies that nested expression
# trees retain their structure in every capture branch.

@mixfix(pattern: "seed {value: number}")
seedValue() =>
    return Stage(depth: value)

@mixfix(pattern: "advance {value: mixfix} by {amount: number}")
advanceValue() =>
    return Stage(depth: value.depth + amount)

@mixfix(
    pattern: "assemble {first: mixfix} then {second: mixfix} then {third: mixfix} then {fourth: mixfix} then {fifth: mixfix} then {sixth: mixfix}"
)
assembleValues() =>
    return Assembly(
        depth: first.depth + second.depth + third.depth + fourth.depth + fifth.depth + sixth.depth
    )

main() =>
    chain := advance (advance (advance (advance (advance (advance (advance (advance (advance (advance (advance (advance (seed 0) by 1) by 1) by 1) by 1) by 1) by 1) by 1) by 1) by 1) by 1) by 1) by 1
    assembly := assemble (seed 1) then (advance (seed 1) by 1) then (advance (advance (seed 1) by 1) by 1) then (advance (advance (advance (seed 1) by 1) by 1) by 1) then (advance (advance (advance (advance (seed 1) by 1) by 1) by 1) by 1) then (advance (advance (advance (advance (advance (seed 1) by 1) by 1) by 1) by 1) by 1)
    return (chain: chain, assembly: assembly)
