import "db"

main() =>
    databasePath := "examples/db_workflow/my_db.fx"
    created := db.create(models: ["cart", "product", "shop"], path: databasePath)

    productDb := db.set(model: "product ", path: databasePath)
    cartDb := db.set(model: "cart", path: databasePath)
    shopDb := db.set(model: "shop", path: databasePath)
    db.deleteOne(connection: productDb, conditions: {id: 101})
    db.deleteOne(connection: cartDb, conditions: {id: 501})
    db.deleteOne(connection: shopDb, conditions: {id: 1})
    db.insert(connection: productDb, data: {id: 101, name: "Cup Cake", price: 5})
    db.insert(connection: cartDb, data: {id: 501, product_id: 101, quantity: 2})
    db.insert(connection: shopDb, data: {id: 1, name: "South Bakery"})
    db.updateOne(connection: productDb, conditions: {id: 101}, patch: {price: 6})
    db.sync(path: databasePath)
    products := lambda("product", item => item.id == 101)
    carts := lambda("cart", item => item.id == 501)
    shops := lambda("shop", item => item.id == 1)
    source := db.read(path: databasePath)

    return (
        created: created,
        product_database: productDb,
        cart_database: cartDb,
        product_count: count(products),
        cart_count: count(carts),
        shop_count: count(shops),
        source: source
    )
