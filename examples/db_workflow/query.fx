import ("db", "user.fx", "region.fx", "order.fx", "product.fx")

main() =>
    # db.fx changes only the backing fact files. Queries below are ordinary
    # in-memory Felidae selection after an explicit reload.
    regionDb := db.connect(model: "Region", path: "examples/db_workflow/region.fx")
    userDb := db.connect(model: "User", path: "examples/db_workflow/user.fx")
    orderDb := db.connect(model: "Order", path: "examples/db_workflow/order.fx")
    db.deleteOne(connection: regionDb, conditions: {region_id: 21})
    newRegion := {
        region_id: 21,
        code: "south-21",
        name: "south",
        enabled: true,
        orderEnabled: true
    }
    db.insert(connection: regionDb, data: newRegion, key: "region_id")

    db.deleteOne(connection: userDb, conditions: {id: 21})
    newUser := {
        id: 21,
        name: "Mira",
        region: {name: "south", region_id: 21},
        status: "active",
        email: "mira@example.test"
    }
    db.insert(connection: userDb, data: newUser)

    cupCakes := lambda(Product, item => item.sku == "CUPCAKE")
    puddings := lambda(Product, item => item.sku == "CHOCOLATE_PUDDING")
    cupCake := array:get(data: cupCakes, position: 0)
    pudding := array:get(data: puddings, position: 0)
    cupCakeSubtotal := math.mul(lhs: cupCake.price, rhs: 2)
    puddingSubtotal := math.mul(lhs: pudding.price, rhs: 3)
    orderTotal := math.add(lhs: cupCakeSubtotal, rhs: puddingSubtotal)
    db.deleteOne(connection: orderDb, conditions: {orderId: 2021})
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
    db.insert(connection: orderDb, data: newOrder, key: "orderId")
    db.sync(path: "examples/db_workflow/region.fx")
    db.sync(path: "examples/db_workflow/user.fx")
    db.sync(path: "examples/db_workflow/order.fx")
    regions := lambda(Region, item => item.name != "")
    users := lambda(User, item => item.id == 21)
    orders := lambda(Order, item => item.orderId == 2021)

    return (
        regions: regions,
        region_count: count(regions),
        inserted_user: array:get(data: users, position: 0),
        user_count: count(users),
        inserted_order: array:get(data: orders, position: 0),
        order_count: count(orders)
    )
