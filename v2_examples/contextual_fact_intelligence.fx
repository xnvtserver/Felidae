# Knowledge is learned by loading facts. Operators only interpret those facts;
# no domain answer is embedded in the interpreter.

Knowledge(
    name: "",
    description: "",
    learned: true
)

FelidaeKnowledge extend Knowledge(
    name: "felidae",
    description: "a deterministic logic language for executable facts",
    learned: true
)

AnimalKnowledge extend Knowledge(
    name: "animal",
    description: "a living organism",
    learned: true
)

CatKnowledge extend AnimalKnowledge(
    name: "cat",
    description: "a feline mammal",
    legs: 4,
    learned: true
)

SonyKnowledge extend CatKnowledge(
    name: "sony",
    description: "a known cat",
    legs: 4,
    learned: true
)

BankMeaning extend Knowledge(
    name: "",
    context: "",
    description: "",
    learned: true
)

FinancialBank extend BankMeaning(
    name: "bank",
    context: "financial",
    description: "an institution that manages money"
)

RiverBank extend BankMeaning(
    name: "bank",
    context: "river",
    description: "land beside a river"
)

QuestionBank extend BankMeaning(
    name: "bank",
    context: "question",
    description: "a reusable collection of questions"
)

BankingAlgorithm extend BankMeaning(
    name: "bank",
    context: "algorithm",
    description: "a learned procedure for banking operations"
)

KnowledgeContext(term: "", context: "", article: nil)

ResolutionEvidence(
    source: "fact-base",
    matched_fact: nil,
    matched_field: "",
    matched_value: "",
    context: nil,
    relevance: 0
)

ContextualAnswer(
    query: "",
    state: "unknown",
    learned: false,
    confidence: 0,
    crisp: false,
    answer: nil,
    candidates: [],
    evidence: []
)

QuantityAnswer(
    query: "",
    state: "unknown",
    learned: false,
    confidence: 0,
    crisp: false,
    subject: nil,
    property: "",
    value: nil,
    evidence: []
)

@mixfix(pattern: "{context:obj} bank")
interpretBankContext() =>
    return KnowledgeContext(
        term: "bank",
        context: context.text,
        article: nil
    )

@mixfix(pattern: "a {subject:obj}")
applyArticleContext() =>
    return KnowledgeContext(
        term: subject.term,
        context: subject.context,
        article: "a"
    )

@mixfix(pattern: "what is {subject:obj}")
explainLearnedTerm() =>
    term := subject.text
    matches := lambda(Knowledge, fact => fact.name == term)
    matchCount := array.len(data: matches)
    if matchCount == 0 then
        return ContextualAnswer(
            query: term,
            state: "unknown",
            learned: false,
            confidence: 0,
            crisp: false,
            answer: nil,
            candidates: [],
            evidence: []
        )
    else
        if matchCount == 1 then
            matched := array.get(data: matches, position: 0)
            return ContextualAnswer(
                query: term,
                state: "resolved",
                learned: true,
                confidence: 1,
                crisp: true,
                answer: matched,
                candidates: matches,
                evidence: [ResolutionEvidence(
                    source: "fact-base",
                    matched_fact: matched,
                    matched_field: "name",
                    matched_value: term,
                    context: nil,
                    relevance: 1
                )]
            )
        else
            return ContextualAnswer(
                query: term,
                state: "ambiguous",
                learned: true,
                confidence: 1 / matchCount,
                crisp: false,
                answer: nil,
                candidates: matches,
                evidence: []
            )

@mixfix(pattern: "what is {subject:obj}")
explainLearnedContext() =>
    matches := lambda(BankMeaning, fact => fact.context == subject.context)
    matchCount := array.len(data: matches)
    if matchCount == 1 then
        matched := array.get(data: matches, position: 0)
        return ContextualAnswer(
            query: subject.term,
            state: "context-resolved",
            learned: true,
            confidence: 1,
            crisp: true,
            answer: matched,
            candidates: matches,
            evidence: [ResolutionEvidence(
                source: "fact-base",
                matched_fact: matched,
                matched_field: "context",
                matched_value: subject.context,
                context: subject,
                relevance: 1
            )]
        )
    else
        return ContextualAnswer(
            query: subject.term,
            state: "unknown-context",
            learned: false,
            confidence: 0,
            crisp: false,
            answer: nil,
            candidates: matches,
            evidence: []
        )

@mixfix(pattern: "how many {property:obj} {subject:obj} have")
answerLearnedQuantity() =>
    propertyName := property.text
    subjectName := subject.text
    matches := lambda(Knowledge, fact => fact.name == subjectName)
    matchCount := array.len(data: matches)
    if matchCount == 1 then
        matched := array.get(data: matches, position: 0)
        amount := json.get(data: matched, key: propertyName)
        return QuantityAnswer(
            query: "how-many",
            state: "resolved",
            learned: true,
            confidence: 1,
            crisp: true,
            subject: matched,
            property: propertyName,
            value: amount,
            evidence: [ResolutionEvidence(
                source: "fact-base",
                matched_fact: matched,
                matched_field: propertyName,
                matched_value: subjectName,
                context: nil,
                relevance: 1
            )]
        )
    else
        return QuantityAnswer(
            query: "how-many",
            state: "unknown",
            learned: false,
            confidence: 0,
            crisp: false,
            subject: nil,
            property: propertyName,
            value: nil,
            evidence: []
        )

main() =>
    felidae := what is felidae
    unlearned := what is quantum
    ambiguousBank := what is bank
    financialBank := what is financial bank
    articleBank := what is a financial bank
    sonyLegs := how many legs sony have
    return (
        felidae: felidae,
        unlearned: unlearned,
        ambiguous_bank: ambiguousBank,
        financial_bank: financialBank,
        article_bank: articleBank,
        sony_legs: sonyLegs
    )
