# Exercise literal anchors before, between, and after typed captures.

@mixfix(pattern: "{first: string} {second: string} completes")
endsAfterTwoCaptures() =>
    return Shape(name: "a-b-operator", first: first, second: second)

@mixfix(pattern: "{first: string} {second: string} {third: string} completes")
endsAfterThreeCaptures() =>
    return Shape(name: "a-b-c-operator", first: first, second: second, third: third)

@mixfix(pattern: "{first: string} combines {second: string} {third: string}")
anchorThenAdjacentCaptures() =>
    return Shape(name: "a-operator-b-c", first: first, second: second, third: third)

@mixfix(pattern: "{first: string} combines {second: string} through {third: string}")
interleavedAnchors() =>
    return Shape(name: "a-operator-b-operator-c", first: first, second: second, third: third)

@mixfix(pattern: "prepare inspect {first: string} {second: string}")
twoLeadingAnchors() =>
    return Shape(name: "operator-operator-a-b", first: first, second: second)

@mixfix(pattern: "{first: string} moves through then toward {second: string} {third: string}")
multiWordAnchor() =>
    return Shape(name: "a-operator-operator-operator-b-c", first: first, second: second, third: third)

main() =>
    endingTwo := "a" "b" completes
    endingThree := "a" "b" "c" completes
    rightAdjacent := "a" combines "b" "c"
    interleaved := "a" combines "b" through "c"
    leading := prepare inspect "a" "b"
    multiWord := "a" moves through then toward "b" "c"
    return (ending_two: endingTwo, ending_three: endingThree, right_adjacent: rightAdjacent, interleaved: interleaved, leading: leading, multi_word: multiWord)
