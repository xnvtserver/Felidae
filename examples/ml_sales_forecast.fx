import "ml"

main() =>
    weeklySales := [
        {__type: "WeeklySale", week: 1, visitors: 100, orders: 20, revenue: 500},
        {__type: "WeeklySale", week: 2, visitors: 130, orders: 26, revenue: 650},
        {__type: "WeeklySale", week: 3, visitors: 160, orders: 32, revenue: 790},
        {__type: "WeeklySale", week: 4, visitors: 200, orders: 41, revenue: 1010}
    ]
    profile := ml.profile_facts(facts: weeklySales, features: ["visitors", "orders", "revenue"])
    visitorRevenue := ml.correlate_facts(facts: weeklySales, left: "visitors", right: "revenue")
    revenueModel := ml.train_linear_regression(facts: weeklySales, target: "revenue", feature: "visitors")
    forecast := ml.predict(model: revenueModel, input: {visitors: 180})
    generated := ml.predict_numeric_fact(facts: weeklySales, input: {visitors: 220}, target: "revenue", feature: "visitors")
    saved := ml.save_model(path: "build/generated_sales_revenue_model.fx", model: revenueModel)
    return (
        profile: profile,
        visitorRevenue: visitorRevenue,
        revenueModel: revenueModel,
        forecast: forecast,
        generated: generated,
        saved: saved
    )
