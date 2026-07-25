import "system"

Increment(value: number) => return value + 1

Double(value: number) =>
    return value * 2

pipelineMath.increment(value: number) =>
    return value + 1

pipelineMath.double(value: number) =>
    return value * 2

pipelineMath.select(value: number) =>
    if value > 0 then
        return pipelineMath.increment(value: value)
            then pipelineMath.double(value: system.result)
    else
        return pipelineMath.double(value: 0)
            then pipelineMath.increment(value: system.result)

Stop(value: any) =>
    value == value
    return nil

Explode(value: any) =>
    return value / 0

Wrap(value: any) =>
    return (
        seen: value,
        tag: "wrapped"
    )

ConditionalThen(value: number) =>
    if value == 2 then
        return "condition-ok"
    else
        return "condition-failed"

UseNested(value: number) =>
    inner := Increment(value: value)
        then Double(value: system.result)
    return inner + 3

ReturnPipeline(value: number) =>
    return Increment(value: value)
        then Double(value: system.result)

CompactReturnPipeline(value: number) => return Increment(value: value) then Double(value: system.result)

main() =>
    result := Increment(value: 1)
        then Double(value: system.result)
    direct := Increment(value: 1)
        then Double(value: system.result)
        then Wrap(value: system.result)
    returned := ReturnPipeline(value: 3)
    compactReturned := CompactReturnPipeline(value: 4)
    methodThen := pipelineMath.increment(value: 4)
        then pipelineMath.double(value: system.result)
    methodIfThen := pipelineMath.select(value: 5)
    methodElseThen := pipelineMath.select(value: 0)
    nested := UseNested(value: 4)
        then Wrap(value: system.result)
    stopped := Increment(value: 1)
        then Stop(value: system.result)
        then Explode(value: system.result)
    arithmeticPrecedence := Increment(value: 1)
        then Double(value: (system.result) + 3)
    conditional := ConditionalThen(value: 2)
      return (
          result: result,
          direct: direct,
          returned: returned,
          compactReturned: compactReturned,
          methodThen: methodThen,
          methodIfThen: methodIfThen,
          methodElseThen: methodElseThen,
          nested: nested,
        stopped: stopped,
        arithmeticPrecedence: arithmeticPrecedence,
        conditional: conditional
    )
