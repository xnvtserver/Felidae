# Type.search provides one fact-native path for SQL-LIKE text filtering,
# hierarchy properties, regular expressions, and bounded degree evidence.
Publication(name: "")
Book extend Publication(name: "")
Magazine extend Publication(name: "")

Catalog(title: "Alpha Guide", confidence: 0.82, category: Book)
Catalog(title: "beta guide", confidence: 0.74, category: Magazine)
Catalog(title: "Reference", confidence: 0.40, category: Publication)

main() =>
    guides := Catalog.search(
        field: "title",
        query: "%guide",
        mode: "like",
        case: "insensitive"
    )
    related := Catalog.search(
        field: "category",
        query: Publication,
        mode: "hierarchy",
        direction: "descendants",
        includeSelf: 0.0
    )
    confident := Catalog.search(
        field: "confidence",
        mode: "degree",
        minimum: 0.70,
        maximum: 0.90
    )
    return (
        guides: count(data: guides),
        related: count(data: related),
        confident: count(data: confident)
    )
end
