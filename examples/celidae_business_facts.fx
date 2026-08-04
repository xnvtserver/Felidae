# A worked business fact set for Celidae's data views.
#
#   celidae examples/celidae_business_facts.fx --recommend
#   celidae examples/celidae_business_facts.fx --html > review.html
#   celidae examples/celidae_business_facts.fx --html --template=cluster > segments.html
#
# The structural views (schema/graph/er/hierarchy) describe what this file
# declares. The data views describe what the values say, and this data is
# shaped to have something for each of them to find:
#
#   distribution  'minutes' contains two genuine outliers (a 214- and a
#                 187-minute delivery against a ~30-minute median), and
#                 'total' is right-skewed the way order values usually are.
#   comparison    'items' and 'total' move together, because larger orders
#                 cost more - the correlation view should surface that pair.
#   cluster       orders fall into two natural populations: everyday
#                 deliveries and bulk catering orders.
#   timeline      every order carries an ISO date across 2025 and 2026.
#   stats         'channel' is deliberately absent from some records, so
#                 field coverage is below 100% and the view says so.

# Everyday orders: a few items, modest totals, ~30 minute deliveries.
Order(id: 1, placed: "2025-03-16", region: "east", channel: "app", items: 4, total: 45.71, minutes: 36)
Order(id: 2, placed: "2026-01-11", region: "south", channel: "phone", items: 1, total: 12.39, minutes: 37)
Order(id: 3, placed: "2025-04-15", region: "north", channel: "web", items: 2, total: 23.22, minutes: 37)
Order(id: 4, placed: "2026-12-18", region: "east", channel: "app", items: 2, total: 22.3, minutes: 31)
Order(id: 5, placed: "2026-10-23", region: "east", items: 1, total: 13.22, minutes: 35)
Order(id: 6, placed: "2026-02-07", region: "north", channel: "phone", items: 1, total: 14.73, minutes: 35)
Order(id: 7, placed: "2025-11-09", region: "south", channel: "phone", items: 3, total: 40.94, minutes: 30)
Order(id: 8, placed: "2026-02-15", region: "east", channel: "web", items: 3, total: 31.25, minutes: 26)
Order(id: 9, placed: "2025-11-20", region: "west", channel: "web", items: 1, total: 14.99, minutes: 26)
Order(id: 10, placed: "2026-01-08", region: "west", channel: "app", items: 4, total: 51.54, minutes: 24)
Order(id: 11, placed: "2026-10-14", region: "south", channel: "app", items: 4, total: 43.06, minutes: 38)
Order(id: 12, placed: "2026-02-07", region: "north", channel: "web", items: 4, total: 52.55, minutes: 28)
Order(id: 13, placed: "2025-07-12", region: "north", channel: "phone", items: 2, total: 28.04, minutes: 39)
Order(id: 14, placed: "2025-04-24", region: "east", channel: "phone", items: 4, total: 48.99, minutes: 25)
Order(id: 15, placed: "2025-06-12", region: "east", channel: "web", items: 2, total: 26.48, minutes: 37)
Order(id: 16, placed: "2026-08-27", region: "south", channel: "web", items: 3, total: 35.02, minutes: 38)
Order(id: 17, placed: "2025-01-09", region: "west", channel: "web", items: 2, total: 22.15, minutes: 33)
Order(id: 18, placed: "2025-08-18", region: "east", channel: "app", items: 4, total: 47.62, minutes: 29)
Order(id: 19, placed: "2025-01-16", region: "east", channel: "phone", items: 4, total: 45.79, minutes: 39)
Order(id: 20, placed: "2026-02-15", region: "south", channel: "web", items: 2, total: 22.25, minutes: 40)
Order(id: 21, placed: "2026-07-10", region: "west", channel: "phone", items: 2, total: 19.33, minutes: 30)
Order(id: 22, placed: "2025-12-27", region: "east", channel: "phone", items: 2, total: 24.18, minutes: 29)
Order(id: 23, placed: "2026-10-09", region: "south", channel: "app", items: 3, total: 32.3, minutes: 31)
Order(id: 24, placed: "2026-02-10", region: "south", channel: "app", items: 2, total: 24.11, minutes: 39)
Order(id: 25, placed: "2026-04-04", region: "east", channel: "web", items: 4, total: 49.13, minutes: 37)
Order(id: 26, placed: "2025-07-19", region: "east", items: 1, total: 14.63, minutes: 38)
Order(id: 27, placed: "2025-03-18", region: "south", channel: "web", items: 4, total: 42.43, minutes: 33)
Order(id: 28, placed: "2026-02-21", region: "north", channel: "web", items: 1, total: 9.93, minutes: 26)
Order(id: 29, placed: "2025-05-14", region: "west", channel: "app", items: 3, total: 36.63, minutes: 27)
Order(id: 30, placed: "2026-08-22", region: "north", channel: "web", items: 4, total: 42.92, minutes: 25)

# Catering orders: an order of magnitude more items and value.
Order(id: 31, placed: "2026-09-02", region: "south", channel: "phone", items: 42, total: 536.02, minutes: 65)
Order(id: 32, placed: "2025-11-08", region: "west", channel: "phone", items: 48, total: 678.11, minutes: 51)
Order(id: 33, placed: "2025-03-04", region: "south", channel: "phone", items: 32, total: 399.42, minutes: 60)
Order(id: 34, placed: "2026-10-04", region: "north", channel: "web", items: 29, total: 365.09, minutes: 63)
Order(id: 35, placed: "2026-01-11", region: "north", channel: "phone", items: 29, total: 350.3, minutes: 69)
Order(id: 36, placed: "2026-07-11", region: "east", channel: "phone", items: 35, total: 455.28, minutes: 67)
Order(id: 37, placed: "2026-07-06", region: "west", channel: "phone", items: 28, total: 373.87, minutes: 64)
Order(id: 38, placed: "2025-11-25", region: "north", channel: "web", items: 28, total: 344.31, minutes: 61)
Order(id: 39, placed: "2025-12-02", region: "east", channel: "app", items: 41, total: 476.88, minutes: 51)
Order(id: 40, placed: "2025-02-20", region: "north", channel: "app", items: 43, total: 551.9, minutes: 58)
Order(id: 41, placed: "2026-01-11", region: "east", channel: "app", items: 33, total: 404.07, minutes: 67)
Order(id: 42, placed: "2025-01-26", region: "north", channel: "web", items: 48, total: 657.28, minutes: 68)

# Two deliveries that went badly wrong - the outlier test must find these.
Order(id: 43, placed: "2025-07-19", region: "west", channel: "phone", items: 2, total: 21.4, minutes: 214)
Order(id: 44, placed: "2026-01-30", region: "north", channel: "app", items: 3, total: 34.9, minutes: 187)

# A subtype, so the hierarchy view has a tree to draw and the treemap
# has record volume to size branches by.
PriorityOrder extend Order(id: 45, placed: "2026-05-11", region: "south", channel: "app", items: 2, total: 31, minutes: 17)
PriorityOrder extend Order(id: 46, placed: "2026-04-18", region: "east", channel: "app", items: 3, total: 43.5, minutes: 15)
PriorityOrder extend Order(id: 47, placed: "2026-04-16", region: "south", channel: "app", items: 3, total: 43.5, minutes: 18)
PriorityOrder extend Order(id: 48, placed: "2026-02-15", region: "south", channel: "app", items: 4, total: 56, minutes: 23)
PriorityOrder extend Order(id: 49, placed: "2026-06-15", region: "west", channel: "app", items: 3, total: 43.5, minutes: 15)
PriorityOrder extend Order(id: 50, placed: "2026-06-17", region: "west", channel: "app", items: 2, total: 31, minutes: 23)

# Couriers: a second fact type, so the comparison view has something to
# compare against and the schema view is not a single box.
Courier(name: "Ana", region: "north", deliveries: 177, rating: 3.9)
Courier(name: "Bo", region: "south", deliveries: 47, rating: 5)
Courier(name: "Cy", region: "east", deliveries: 71, rating: 4.7)
Courier(name: "Dee", region: "west", deliveries: 167, rating: 3.4)
Courier(name: "Eli", region: "north", deliveries: 126, rating: 3.7)
Courier(name: "Fay", region: "south", deliveries: 119, rating: 4.8)
Courier(name: "Gus", region: "east", deliveries: 92, rating: 4.9)
Courier(name: "Hal", region: "west", deliveries: 95, rating: 4.4)

main() =>
    system.print(value: "celidae business facts: run celidae --recommend on this file")
    return
