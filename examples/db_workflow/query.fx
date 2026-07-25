import ("db", "user.fx", "region.fx", "order.fx", "product.fx")

main() =>
    regions := db.all(type: "Region")
    users := db.all(type: "User")
    orders := db.all(type: "Order")

    # Remove matching identities first so rerunning this transaction remains
    # deterministic, then persist each new fact with db.insert.
    regionsWithout21 := db.delete(rows: regions, field: "region_id", equals: 21)
    newRegion := {
        region_id: 21,
        code: "south-21",
        name: "south",
        enabled: true,
        orderEnabled: true
    }
    updatedRegions := db.insert(
        path: "examples/db_workflow/region.fx",
        type: "Region",
        rows: regionsWithout21,
        row: newRegion
    )

    usersWithout21 := db.delete(rows: users, field: "id", equals: 21)
    newUser := {
        id: 21,
        name: "Mira",
        region: {name: "south", region_id: 21},
        status: "active",
        email: "mira@example.test"
    }
    updatedUsers := db.insert(
        path: "examples/db_workflow/user.fx",
        type: "User",
        rows: usersWithout21,
        row: newUser
    )

    cupCake := db.first(type: "Product", field: "sku", equals: "CUPCAKE")
    pudding := db.first(type: "Product", field: "sku", equals: "CHOCOLATE_PUDDING")
    cupCakeSubtotal := math.mul(lhs: cupCake.price, rhs: 2)
    puddingSubtotal := math.mul(lhs: pudding.price, rhs: 3)
    orderTotal := math.add(lhs: cupCakeSubtotal, rhs: puddingSubtotal)
    ordersWithout21 := db.delete(rows: orders, field: "orderId", equals: 2021)
    newOrder := {
        orderId: 2021,
        customer: {
            id: newUser.id,
            name: newUser.name,
            status: newUser.status,
            region: newUser.region
        },
        region: newRegion,
        items: [
            {
                product: {
                    product_id: cupCake.product_id,
                    sku: cupCake.sku,
                    name: cupCake.name,
                    price: cupCake.price
                },
                quantity: 2
            },
            {
                product: {
                    product_id: pudding.product_id,
                    sku: pudding.sku,
                    name: pudding.name,
                    price: pudding.price
                },
                quantity: 3
            }
        ],
        total: orderTotal,
        status: "created"
    }
    updatedOrders := db.insert(
        path: "examples/db_workflow/order.fx",
        type: "Order",
        rows: ordersWithout21,
        row: newOrder
    )

    return (
        regions: updatedRegions,
        region_count: count(updatedRegions),
        inserted_user: newUser,
        user_count: count(updatedUsers),
        inserted_order: newOrder,
        order_count: count(updatedOrders)
    )
