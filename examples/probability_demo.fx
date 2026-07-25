import "probability"

main() =>
    weights := probability.normalize(data: [2, 3, 5])
    return (
        mean: probability.mean(data: [2, 4, 6, 8]),
        variance: probability.variance(data: [2, 4, 6, 8]),
        stddev: probability.stddev(data: [2, 4, 6, 8]),
        weights: weights,
        entropy: probability.entropy(data: weights),
        binomial: probability.binomialPmf(trials: 10, successes: 3, p: 0.5),
# poisson: probability.poissonCdf((lambda: 2.5, events: 3),
        normal: probability.normalCdf(x: 0, mean: 0, stddev: 1),
        uniform: probability.uniformPdf(x: 0.5, min: 0, max: 1),
        covariance: probability.covariance(left: [1, 2, 3], right: [2, 4, 6]),
        correlation: probability.correlation(left: [1, 2, 3], right: [2, 4, 6])
    )
