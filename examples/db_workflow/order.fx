Order(
    id: 1,
    name: "Alice",
    region: "south",
    status: "active",
    spend: 240,
    orderId: 1001,
    customerId: 1,
    customer: "Alice",
    amount: 240
)
Order(
    id: 2,
    name: "Bob",
    region: "south",
    status: "active",
    spend: 170,
    orderId: 1002,
    customerId: 2,
    customer: "Bob",
    amount: 170
)
Order(
    id: 5,
    name: "Esha",
    region: "south",
    status: "active",
    spend: 25,
    orderId: 1005,
    customerId: 5,
    customer: "Esha",
    amount: 25
)
Order(
    orderId: 2021,
    customer: {
        id: 21,
        name: "Mira",
        status: "active",
        region: {
            name: "south",
            region_id: 21
        }
    },
    region: {
        region_id: 21,
        code: "south-21",
        name: "south",
        enabled: true,
        orderEnabled: true
    },
    items: [
        {
            product: {
                product_id: 101,
                sku: "CUPCAKE",
                name: "Cup Cake",
                price: 4
            },
            quantity: 2
        },
        {
            product: {
                product_id: 102,
                sku: "CHOCOLATE_PUDDING",
                name: "Chocolate Pudding",
                price: 6
            },
            quantity: 3
        }
    ],
    total: 26,
    status: "created"
)
