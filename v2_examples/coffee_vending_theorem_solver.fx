# Coffee vending as bounded theorem proving rather than a state-transition
# table. Every predicate returns numeric truth: 0.0 is false, 1.0 is true.

VendRequest(selection: "", credit: 0)
CoffeeRequest extend VendRequest(selection: "coffee", credit: 0)
TeaRequest extend VendRequest(selection: "tea", credit: 0)

VendingMachine(name: "", water: 0, beans: 0, tea: 0, ready: 0)
HotDrinkMachine extend VendingMachine(name: "", water: 0, beans: 0, tea: 0, ready: 0)

requestUnifies(request: any) =>
    return isA(
        left: request,
        right: VendRequest(selection: "", credit: 0)
    )

proveCoffee(request: any, machine: any) =>
    return (
        requestUnifies(request: request) == 1.0
        and request.selection == "coffee"
        and request.credit >= 2.0
        and machine.ready == 1.0
        and machine.water >= 1.0
        and machine.beans >= 1.0
    )

proveTea(request: any, machine: any) =>
    return (
        requestUnifies(request: request) == 1.0
        and request.selection == "tea"
        and request.credit >= 1.5
        and machine.ready == 1.0
        and machine.water >= 1.0
        and machine.tea >= 1.0
    )

proveRefund(request: any) => return request.credit > 0.0

# These selectors are explicit, ordered choice points. A failed proof falls
# through to the next candidate, which is bounded backtracking.
chooseRefund(refund_proof: number) =>
    if refund_proof == 1.0 then
        return "refund"
    else
        return "safe_halt"

chooseTea(tea_proof: number, refund_proof: number) =>
    if tea_proof == 1.0 then
        return "dispense_tea"
    else
        return chooseRefund(refund_proof: refund_proof)

chooseCoffee(coffee_proof: number, tea_proof: number, refund_proof: number) =>
    if coffee_proof == 1.0 then
        return "dispense_coffee"
    else
        return chooseTea(tea_proof: tea_proof, refund_proof: refund_proof)

@mixfix(pattern: "{evidence: number} entails {conclusion: string}")
entail(evidence: number, conclusion: string) =>
    if evidence == 1.0 then
        return conclusion
    else
        return "unproved"

solve(request: any, machine: any) =>
    coffee_proof := proveCoffee(request: request, machine: machine)
    tea_proof := proveTea(request: request, machine: machine)
    refund_proof := proveRefund(request: request)
    theorem := coffee_proof entails "coffee is dispensable"
    action := chooseCoffee(
        coffee_proof: coffee_proof,
        tea_proof: tea_proof,
        refund_proof: refund_proof
    )
    return (
        action: action,
        theorem: theorem,
        coffee_proof: coffee_proof,
        tea_proof: tea_proof,
        refund_proof: refund_proof
    )

main() =>
    healthy := HotDrinkMachine(
        name: "lobby",
        water: 8,
        beans: 5,
        tea: 4,
        ready: 1.0
    )
    depleted := HotDrinkMachine(
        name: "break-room",
        water: 8,
        beans: 0,
        tea: 4,
        ready: 1.0
    )
    request := CoffeeRequest(selection: "coffee", credit: 2.0)
    return (
        normal: solve(request: request, machine: healthy),
        degraded: solve(request: request, machine: depleted)
    )
