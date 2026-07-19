# Native probability/statistics declarations. Bodies are implemented by the runtime bridge.

probability.mean(data: array) => ().
probability.variance(data: array) => ().
probability.stddev(data: array) => ().
probability.normalize(data: array) => ().
probability.entropy(data: array) => ().
probability.covariance(left: array, right: array) => ().
probability.correlation(left: array, right: array) => ().
probability.bernoulli(p: number) => ().
probability.binomialPmf(trials: number, successes: number, p: number) => ().
probability.binomialCdf(trials: number, successes: number, p: number) => ().
probability.poissonPmf(lambda: number, events: number) => ().
probability.poissonCdf(lambda: number, events: number) => ().
probability.normalPdf(x: number, mean: number, stddev: number) => ().
probability.normalCdf(x: number, mean: number, stddev: number) => ().
probability.uniformPdf(x: number, min: number, max: number) => ().
probability.uniformCdf(x: number, min: number, max: number) => ().
probability.sample(data: array) => ().
probability.weightedChoice(data: array, weights: array) => ().
