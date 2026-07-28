import "db"

SubmitPizzaOrder(quantity: number) =>
    if quantity <= 0 then
        return {
            quantity: 1,
            recovered: true,
            exception: {
                __type: "Exception",
                kind: "InvalidPizzaQuantity",
                message: "Pizza quantity must be greater than zero",
                source: "pizza_delivery",
                action: "correct_and_resubmit"
            }
        }
    else
        return {quantity: quantity, recovered: false, exception: nil}

ChangeOrderStatus(connection: any, orderId: number, currentStatus: string, nextStatus: string) =>
    return db.updateOne(
        connection: connection,
        conditions: {id: orderId, status: currentStatus},
        patch: {status: nextStatus}
    )

ModifyPizzaOrder(connection: any, orderId: number, items: array, subtotal: number, total: number) =>
    return db.updateOne(
        connection: connection,
        conditions: {id: orderId, status: "created"},
        patch: {items: items, subtotal: subtotal, total: total, status: "modified"}
    )

CancelPizzaOrder(connection: any, orderId: number, reason: string) =>
    # A connection changes a fact file; querying remains normal in-memory
    # Felidae after the explicit reload boundary.
    db.sync(path: connection.path)
    matchingOrders := lambda(PizzaOrder, item => item.id == orderId)
    if count(matchingOrders) == 0 then
        return {
            matched: 0,
            modified: 0,
            data: nil,
            error: {
                __type: "Exception",
                kind: "OrderNotFound",
                message: "Order does not exist",
                source: "pizza_delivery"
            }
        }
    else
        stored := array:get(data: matchingOrders, position: 0)
        if stored.status == "delivered" then
            return {
                matched: 1,
                modified: 0,
                data: stored,
                error: {
                    __type: "Exception",
                    kind: "DeliveredOrderCannotBeCancelled",
                    message: "A delivered order cannot be cancelled",
                    source: "pizza_delivery"
                }
            }
        else
            return db.updateOne(
                connection: connection,
                conditions: {id: orderId, status: stored.status},
                patch: {status: "cancelled", cancellationReason: reason}
            )

main() =>
    # Layout 1: one model per file. create/connect preserves existing files.
    userDb := db.create(model: "PizzaUser", path: "examples/db_workflow/pizza_user.fx")
    regionDb := db.create(model: "PizzaRegion", path: "examples/db_workflow/pizza_region.fx")
    productDb := db.create(model: "PizzaProduct", path: "examples/db_workflow/pizza_product.fx")
    orderDb := db.create(model: "PizzaOrder", path: "examples/db_workflow/pizza_order.fx")

    # Remove only this repeatable example's known identities; no database-wide reset.
    db.deleteOne(connection: userDb, conditions: {id: 1})
    db.deleteOne(connection: regionDb, conditions: {id: 10})
    db.deleteOne(connection: productDb, conditions: {id: 101})
    db.deleteOne(connection: productDb, conditions: {id: 102})
    db.deleteOne(connection: orderDb, conditions: {id: 5001})

    db.insert(connection: regionDb, data: {
        id: 10, name: "South", deliveryEnabled: true, deliveryFee: 3
    })
    db.insert(connection: userDb, data: {
        id: 1,
        name: "Mira",
        phone: "555-0101",
        region: {id: 10, name: "South"},
        status: "active"
    })
    db.insert(connection: productDb, data: {
        id: 101, name: "Margherita Pizza", price: 12, available: true
    })
    db.insert(connection: productDb, data: {
        id: 102, name: "Pepperoni Pizza", price: 15, available: true
    })
    db.sync(path: "examples/db_workflow/pizza_product.fx")
    separateById := lambda(PizzaProduct, item => item.id == 101)
    separatePizzas := lambda(separateById, item => item.available == true)
    separatePizza := array:get(data: separatePizzas, position: 0)
    separateSubtotal := math.mul(lhs: separatePizza.price, rhs: 1)
    separateTotal := math.add(lhs: separateSubtotal, rhs: 3)
    db.insert(connection: orderDb, data: {
        id: 5001,
        customer: {id: 1, name: "Mira"},
        region: {id: 10, name: "South"},
        items: [{
            productId: separatePizza.id,
            name: separatePizza.name,
            quantity: 1,
            unitPrice: separatePizza.price
        }],
        subtotal: separateSubtotal,
        deliveryFee: 3,
        total: separateTotal,
        status: "created",
        cancellationReason: nil
    })

    confirmed := ChangeOrderStatus(connection: orderDb, orderId: 5001, currentStatus: "created", nextStatus: "confirmed")
    preparing := ChangeOrderStatus(connection: orderDb, orderId: 5001, currentStatus: "confirmed", nextStatus: "preparing")
    dispatched := ChangeOrderStatus(connection: orderDb, orderId: 5001, currentStatus: "preparing", nextStatus: "out_for_delivery")
    delivered := ChangeOrderStatus(connection: orderDb, orderId: 5001, currentStatus: "out_for_delivery", nextStatus: "delivered")
    rejectedCancellation := CancelPizzaOrder(connection: orderDb, orderId: 5001, reason: "too_late")

    # Layout 2: several models in one file.
    sharedPath := "examples/db_workflow/pizza_delivery_db.fx"
    sharedDatabase := db.create(
        models: ["PizzaUser", "PizzaRegion", "PizzaProduct", "PizzaOrder"],
        path: sharedPath
    )
    sharedUserDb := db.connect(model: "PizzaUser", path: sharedPath)
    sharedRegionDb := db.connect(model: "PizzaRegion", path: sharedPath)
    sharedProductDb := db.connect(model: "PizzaProduct", path: sharedPath)
    sharedOrderDb := db.connect(model: "PizzaOrder", path: sharedPath)

    db.deleteOne(connection: sharedUserDb, conditions: {id: 2})
    db.deleteOne(connection: sharedRegionDb, conditions: {id: 20})
    db.deleteOne(connection: sharedProductDb, conditions: {id: 201})
    db.deleteOne(connection: sharedProductDb, conditions: {id: 202})
    db.deleteOne(connection: sharedOrderDb, conditions: {id: 6001})
    db.deleteOne(connection: sharedOrderDb, conditions: {id: 6002})
    db.deleteOne(connection: sharedOrderDb, conditions: {id: 6003})

    db.insert(connection: sharedRegionDb, data: {
        id: 20, name: "North", deliveryEnabled: true, deliveryFee: 4
    })
    db.insert(connection: sharedUserDb, data: {
        id: 2,
        name: "Arun",
        phone: "555-0102",
        region: {id: 20, name: "North"},
        status: "active"
    })
    db.insert(connection: sharedProductDb, data: {
        id: 201, name: "Farmhouse Pizza", price: 14, available: true
    })
    db.insert(connection: sharedProductDb, data: {
        id: 202, name: "Paneer Pizza", price: 16, available: true
    })
    db.sync(path: sharedPath)
    farmhouseById := lambda(PizzaProduct, item => item.id == 201)
    farmhousePizzas := lambda(farmhouseById, item => item.available == true)
    paneerById := lambda(PizzaProduct, item => item.id == 202)
    paneerPizzas := lambda(paneerById, item => item.available == true)
    farmhousePizza := array:get(data: farmhousePizzas, position: 0)
    paneerPizza := array:get(data: paneerPizzas, position: 0)
    farmhouseSubtotal := math.mul(lhs: farmhousePizza.price, rhs: 1)
    farmhouseTotal := math.add(lhs: farmhouseSubtotal, rhs: 4)
    paneerSubtotal := math.mul(lhs: paneerPizza.price, rhs: 1)
    paneerTotal := math.add(lhs: paneerSubtotal, rhs: 4)
    db.insert(connection: sharedOrderDb, data: {
        id: 6001,
        customer: {id: 2, name: "Arun"},
        region: {id: 20, name: "North"},
        items: [{
            productId: farmhousePizza.id,
            name: farmhousePizza.name,
            quantity: 1,
            unitPrice: farmhousePizza.price
        }],
        subtotal: farmhouseSubtotal,
        deliveryFee: 4,
        total: farmhouseTotal,
        status: "created",
        cancellationReason: nil
    })
    db.insert(connection: sharedOrderDb, data: {
        id: 6002,
        customer: {id: 2, name: "Arun"},
        region: {id: 20, name: "North"},
        items: [{
            productId: paneerPizza.id,
            name: paneerPizza.name,
            quantity: 1,
            unitPrice: paneerPizza.price
        }],
        subtotal: paneerSubtotal,
        deliveryFee: 4,
        total: paneerTotal,
        status: "created",
        cancellationReason: nil
    })

    correctedRequest := SubmitPizzaOrder(quantity: 0)
    db.insert(connection: sharedOrderDb, data: {
        id: 6003,
        customer: {id: 2, name: "Arun"},
        region: {id: 20, name: "North"},
        items: [{
            productId: farmhousePizza.id,
            name: farmhousePizza.name,
            quantity: correctedRequest.quantity,
            unitPrice: farmhousePizza.price
        }],
        subtotal: farmhouseSubtotal,
        deliveryFee: 4,
        total: farmhouseTotal,
        status: "resubmitted",
        cancellationReason: nil
    })

    modifiedSubtotal := math.mul(lhs: paneerPizza.price, rhs: 2)
    modifiedTotal := math.add(lhs: modifiedSubtotal, rhs: 4)
    modified := ModifyPizzaOrder(
        connection: sharedOrderDb,
        orderId: 6001,
        items: [{
            productId: paneerPizza.id,
            name: paneerPizza.name,
            quantity: 2,
            unitPrice: paneerPizza.price
        }],
        subtotal: modifiedSubtotal,
        total: modifiedTotal
    )
    sharedConfirmed := ChangeOrderStatus(
        connection: sharedOrderDb, orderId: 6001, currentStatus: "modified", nextStatus: "confirmed"
    )
    sharedPreparing := ChangeOrderStatus(
        connection: sharedOrderDb, orderId: 6001, currentStatus: "confirmed", nextStatus: "preparing"
    )
    cancellation := CancelPizzaOrder(
        connection: sharedOrderDb, orderId: 6002, reason: "customer_changed_mind"
    )

    db.sync(path: sharedPath)
    sharedUsers := lambda(PizzaUser, item => item.id == 2)
    users := lambda(sharedUsers, item => item.status == "active")
    sharedRegions := lambda(PizzaRegion, item => item.id == 20)
    regions := lambda(sharedRegions, item => item.deliveryEnabled == true)
    sharedProducts := lambda(PizzaProduct, item => item.id >= 201)
    products := lambda(sharedProducts, item => item.available == true)
    orders := lambda(PizzaOrder, item => item.customer.id == 2)
    order6001s := lambda(PizzaOrder, item => item.id == 6001)
    order6002s := lambda(PizzaOrder, item => item.id == 6002)
    order6003s := lambda(PizzaOrder, item => item.id == 6003)
    order6001 := array:get(data: order6001s, position: 0)
    order6002 := array:get(data: order6002s, position: 0)
    order6003 := array:get(data: order6003s, position: 0)

    return (
        separate: {
            confirmed: confirmed,
            preparing: preparing,
            dispatched: dispatched,
            delivered: delivered,
            rejected_cancellation: rejectedCancellation
        },
        shared: {
            database: sharedDatabase,
            users_query: users,
            products_query: products,
            user_count: count(users),
            region_count: count(regions),
            product_count: count(products),
            order_count: count(orders),
            corrected_request: correctedRequest,
            modified: modified,
            confirmed: sharedConfirmed,
            preparing: sharedPreparing,
            cancellation: cancellation,
            order_6001: order6001,
            order_6002: order6002,
            order_6003: order6003
        }
    )
