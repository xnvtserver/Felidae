# Felidae multi-model fact database
# models: PizzaUser PizzaRegion PizzaProduct PizzaOrder






























































































































































































































PizzaRegion(
    id: 20,
    name: "North",
    deliveryEnabled: true,
    deliveryFee: 4
)

PizzaUser(
    id: 2,
    name: "Arun",
    phone: "555-0102",
    region: {
        id: 20,
        name: "North"
    },
    status: "active"
)

PizzaProduct(
    id: 201,
    name: "Farmhouse Pizza",
    price: 14,
    available: true
)

PizzaProduct(
    id: 202,
    name: "Paneer Pizza",
    price: 16,
    available: true
)

PizzaOrder(
    id: 6001,
    customer: {
        id: 2,
        name: "Arun"
    },
    region: {
        id: 20,
        name: "North"
    },
    items: [
        {
            productId: 202,
            name: "Paneer Pizza",
            quantity: 2,
            unitPrice: 16
        }
    ],
    subtotal: 32,
    deliveryFee: 4,
    total: 36,
    status: "preparing",
    cancellationReason: nil
)

PizzaOrder(
    id: 6002,
    customer: {
        id: 2,
        name: "Arun"
    },
    region: {
        id: 20,
        name: "North"
    },
    items: [
        {
            productId: 202,
            name: "Paneer Pizza",
            quantity: 1,
            unitPrice: 16
        }
    ],
    subtotal: 16,
    deliveryFee: 4,
    total: 20,
    status: "cancelled",
    cancellationReason: "customer_changed_mind"
)

PizzaOrder(
    id: 6003,
    customer: {
        id: 2,
        name: "Arun"
    },
    region: {
        id: 20,
        name: "North"
    },
    items: [
        {
            productId: 201,
            name: "Farmhouse Pizza",
            quantity: 1,
            unitPrice: 14
        }
    ],
    subtotal: 14,
    deliveryFee: 4,
    total: 18,
    status: "resubmitted",
    cancellationReason: nil
)
