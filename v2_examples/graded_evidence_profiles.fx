# A non-boolean knowledge workload.  Every judgement is a typed fact carrying
# a continuous membership degree; no result is used as a branch condition.
# The five profiles overlap deliberately and are inputs to the future
# Gaussian evaluator, rather than crisp true/false buckets.

Evidence(name: "", subject: "", fx.observed_at: 0, priority: 0)
Assessment extend Evidence(name: "", subject: "", fx.observed_at: 0, priority: 0, degree: 0)
RatingProfile extend Assessment(
    name: "",
    subject: "",
    fx.observed_at: 0,
    priority: 0,
    degree: 0,
    peak: 0,
    fades_in: 0,
    fades_out: 0
)

profile(name: string, peak: number, fadesIn: number, fadesOut: number) =>
    return RatingProfile(
        name: name,
        subject: "quality",
        fx.observed_at: 20260817,
        priority: peak,
        degree: peak,
        peak: peak,
        fades_in: fadesIn,
        fades_out: fadesOut
    )

makeReport(score: number) =>
    critical := profile(name: "Critical / Strongly Disagree", peak: 0, fadesIn: 0, fadesOut: 30)
    subpar := profile(name: "Subpar / Disagree", peak: 30, fadesIn: 10, fadesOut: 50)
    acceptable := profile(name: "Acceptable / Neutral", peak: 50, fadesIn: 30, fadesOut: 70)
    strong := profile(name: "Strong / Agree", peak: 75, fadesIn: 50, fadesOut: 90)
    exceptional := profile(name: "Exceptional / Strongly Agree", peak: 100, fadesIn: 75, fadesOut: 100)
    return {
        score: score,
        scale: [critical, subpar, acceptable, strong, exceptional],
        interpretation: RatingProfile(
            name: "continuous-evidence",
            subject: "quality",
            fx.observed_at: 20260817,
            priority: score,
            degree: score,
            peak: 75,
            fades_in: 50,
            fades_out: 90
        )
    }

main() =>
    return makeReport(score: 68)
