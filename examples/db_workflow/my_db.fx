# Felidae multi-model fact database
# models: cart product shop
































































































product(
    id: 101,
    name: "Cup Cake",
    price: 6
)

cart(
    id: 501,
    product_id: 101,
    quantity: 2
)

shop(
    id: 1,
    name: "South Bakery"
)
