# Coffee vending as bounded theorem proving rather than a state-transition
# table. Every predicate returns numeric truth: 0.0 is false, 1.0 is true.

def VendRequest(selection: "", credit: 0).
def CoffeeRequest extend VendRequest(selection: "coffee", credit: 0).
def TeaRequest extend VendRequest(selection: "tea", credit: 0).

def VendingMachine(name: "", water: 0, beans: 0, tea: 0, ready: 0).
def HotDrinkMachine extend VendingMachine(name: "", water: 0, beans: 0, tea: 0, ready: 0).

def requestUnifies(request: any) =>
    instanceof(value: request, type: VendRequest.class).
end

def proveCoffee(request: any, machine: any) =>
    (
        requestUnifies(request: request) = 1.0
        and request.selection = "coffee"
        and request.credit >= 2.0
        and machine.ready = 1.0
        and machine.water >= 1.0
        and machine.beans >= 1.0
    ).
end

def proveTea(request: any, machine: any) =>
    (
        requestUnifies(request: request) = 1.0
        and request.selection = "tea"
        and request.credit >= 1.5
        and machine.ready = 1.0
        and machine.water >= 1.0
        and machine.tea >= 1.0
    ).
end

def proveRefund(request: any) =>
    request.credit > 0.0.
end

# These selectors are explicit, ordered choice points. A failed proof falls
# through to the next candidate, which is bounded backtracking.
def chooseRefund(refund_proof: number) =>
    refund_proof = 1.0 then "refund" else "safe_halt".
end

def chooseTea(tea_proof: number, refund_proof: number) =>
    tea_proof = 1.0 then "dispense_tea"
    else chooseRefund(refund_proof: refund_proof).
end

def chooseCoffee(coffee_proof: number, tea_proof: number, refund_proof: number) =>
    coffee_proof = 1.0 then "dispense_coffee"
    else chooseTea(tea_proof: tea_proof, refund_proof: refund_proof).
end

@mixfix(pattern: '{evidence: number} entails {conclusion: string}')
def entail() =>
    evidence = 1.0 then conclusion else "unproved".
end

def solve(request: any, machine: any) =>
    def coffee_proof := proveCoffee(request: request, machine: machine).
    def tea_proof := proveTea(request: request, machine: machine).
    def refund_proof := proveRefund(request: request).
    def theorem := coffee_proof entails "coffee is dispensable".
    def action := chooseCoffee(
        coffee_proof: coffee_proof,
        tea_proof: tea_proof,
        refund_proof: refund_proof
    ).
    (
        action: action,
        theorem: theorem,
        coffee_proof: coffee_proof,
        tea_proof: tea_proof,
        refund_proof: refund_proof
    ).
end

def main() =>
    def healthy := HotDrinkMachine(
        name: "lobby",
        water: 8,
        beans: 5,
        tea: 4,
        ready: 1.0
    ).
    def depleted := HotDrinkMachine(
        name: "break-room",
        water: 8,
        beans: 0,
        tea: 4,
        ready: 1.0
    ).
    def request := CoffeeRequest(selection: "coffee", credit: 2.0).
    (
        normal: solve(request: request, machine: healthy),
        degraded: solve(request: request, machine: depleted)
    ).
end
