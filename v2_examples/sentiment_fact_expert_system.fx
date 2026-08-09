# Sentiment is modelled as executable knowledge, not as a hidden model. Add
# or adjust cue and policy facts to evolve the domain expertise.

SentimentConcept(name: "sentiment")
SentimentCue extend SentimentConcept(token: "", polarity: "", strength: 0, domain: "general")
PositiveCue extend SentimentCue(token: "", polarity: "positive", strength: 0, domain: "general")
NegativeCue extend SentimentCue(token: "", polarity: "negative", strength: 0, domain: "general")
NegationCue extend SentimentCue(token: "", polarity: "negation", strength: 0, domain: "general")

PositiveCue(token: "excellent", polarity: "positive", strength: 0.95, domain: "product")
PositiveCue(token: "reliable", polarity: "positive", strength: 0.70, domain: "product")
PositiveCue(token: "easy", polarity: "positive", strength: 0.55, domain: "product")
NegativeCue(token: "terrible", polarity: "negative", strength: 0.95, domain: "product")
NegativeCue(token: "slow", polarity: "negative", strength: 0.55, domain: "product")
NegativeCue(token: "broken", polarity: "negative", strength: 0.90, domain: "product")
NegationCue(token: "not", polarity: "negation", strength: 0.70, domain: "general")

SentimentPolicy(
    name: "product-review",
    positive_weight: 0.34,
    negative_weight: 0.40,
    negation_penalty: 0.28,
    positive_threshold: 0.20,
    negative_threshold: -0.20
)

Review(id: "review-positive", text: "excellent reliable and easy", domain: "product")
Review(id: "review-negative", text: "terrible slow and broken", domain: "product")
Review(id: "review-mixed", text: "excellent but not reliable and slow", domain: "product")

SentimentAssessment(
    review: nil,
    label: "unknown",
    score: 0,
    confidence: 0,
    positive_cues: [],
    negative_cues: [],
    negation_cues: [],
    evidence: [],
    taxonomy: nil,
    explanation: ""
)

sentimentLabel(score: number, policy: SentimentPolicy) =>
    if score >= policy.positive_threshold then
        return "positive"
    else
        if score <= policy.negative_threshold then
            return "negative"
        else
            return "mixed"

sentimentExplanation(label: string, positives: number, negatives: number, negations: number) =>
    if label == "positive" then
        return "positive cue facts outweigh opposing cue facts"
    else
        if label == "negative" then
            return "negative cue facts outweigh supporting cue facts"
        else
            if negations > 0 then
                return "conflicting cue facts and negation require a qualified mixed assessment"
            else
                return "supporting and opposing cue facts are too close for a crisp label"

boundedEvidenceDegree(value: number) =>
    if value >= 1 then
        return 1
    else
        if value <= 0 then
            return 0
        else
            return value

analyseSentiment(review: Review, policy: SentimentPolicy) =>
    positives := lambda(PositiveCue, cue => cue.token != "" and str.contains(data: review.text, needle: cue.token))
    negatives := lambda(NegativeCue, cue => cue.token != "" and str.contains(data: review.text, needle: cue.token))
    negations := lambda(NegationCue, cue => cue.token != "" and str.contains(data: review.text, needle: cue.token))
    positive_count := count(positives)
    negative_count := count(negatives)
    negation_count := count(negations)
    positive_score := positive_count * policy.positive_weight
    negative_score := negative_count * policy.negative_weight
    negation_score := negation_count * policy.negation_penalty
    score := positive_score - negative_score - negation_score
    label := sentimentLabel(score: score, policy: policy)
    confidence := abs(score)
    support_degree := boundedEvidenceDegree(value: positive_score)
    oppose_degree := boundedEvidenceDegree(value: negative_score + negation_score)
    positive_taxonomy := lambda(PositiveCue, cue => cue.token == "excellent")
    negative_taxonomy := lambda(NegativeCue, cue => cue.token == "terrible")
    positive_root := array.get(data: positive_taxonomy, position: 0)
    negative_root := array.get(data: negative_taxonomy, position: 0)
    taxonomy := commonAncestors(left: positive_root, right: negative_root)
    evidence := Reasoning.grade(evidence: [
        Evidence(
            source: "positive-cue-facts",
            degree: support_degree,
            reliability: 0.90,
            polarity: "support"
        ),
        Evidence(
            source: "negative-cue-facts",
            degree: oppose_degree,
            reliability: 0.90,
            polarity: "oppose"
        )
    ])
    return SentimentAssessment(
        review: review,
        label: label,
        score: score,
        confidence: confidence,
        positive_cues: positives,
        negative_cues: negatives,
        negation_cues: negations,
        evidence: evidence,
        taxonomy: taxonomy,
        explanation: sentimentExplanation(
            label: label,
            positives: positive_count,
            negatives: negative_count,
            negations: negation_count
        )
    )

main() =>
    reviews := lambda(Review, review => review.domain == "product")
    policies := lambda(SentimentPolicy, policy => policy.name == "product-review")
    policy := array.get(data: policies, position: 0)
    return lambda(reviews, review => analyseSentiment(review: review, policy: policy))
