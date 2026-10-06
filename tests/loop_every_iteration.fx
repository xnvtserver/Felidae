# Every iteration of for and while must run. A loop body whose statements
# produce values (an insert, a binding) is not a `return`: the loop goes on.
class Tally
    key(id).
    def id: number.
end

def main() =>
    for i in [1, 2, 3] then
        def doubled := i * 2.
        Tally.insert(values: {id: i}).
    end
    while Tally.count() < 6 then
        Tally.insert(values: {id: Tally.count() + 10}).
    end
    (rows: Tally.count(), from_for: Tally.where(id: 3).count()).
end
