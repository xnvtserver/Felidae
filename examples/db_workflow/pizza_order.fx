# Felidae multi-model fact database
# models: PizzaOrder

PizzaOrder(
    id: 5001,
    customer: {
        id: 1,
        name: "Mira"
    },
    region: {
        id: 10,
        name: "South"
    },
    items: [
        {
            productId: 101,
            name: "Margherita Pizza",
            quantity: 1,
            unitPrice: 12
        }
    ],
    subtotal: 12,
    deliveryFee: 3,
    total: 15,
    status: "delivered",
    cancellationReason: nil
)
