import "db"

main() =>
    databasePath := "examples/db_workflow/my_db.fx"
    created := db.create(models: ["cart", "product", "shop"], path: databasePath)

    product := db.set(model: "product ", path: databasePath)
    cart := db.set(model: "cart", path: databasePath)
    shop := db.set(model: "shop", path: databasePath)

    products := db.insert(
        database: product,
        rows: [],
        row: {id: 101, name: "Cup Cake", price: 5}
    )
    carts := db.insert(
        database: cart,
        rows: [],
        row: {id: 501, product_id: 101, quantity: 2}
    )
    shops := db.insert(
        database: shop,
        rows: [],
        row: {id: 1, name: "South Bakery"}
    )

    updatedProducts := db.updateFile(
        database: product,
        rows: products,
        field: "id",
        equals: 101,
        patch: {price: 6}
    )
    source := db.read(path: databasePath)

    return (
        created: created,
        product_database: product,
        cart_database: cart,
        product_count: count(updatedProducts),
        cart_count: count(carts),
        shop_count: count(shops),
        source: source
    )
