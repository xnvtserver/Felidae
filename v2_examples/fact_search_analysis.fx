# Search is expressed with ordinary lazy fact predicates. Text helpers remain
# library calls and numeric confidence is data, never implicit truth.
Publication(name: "")
Book extend Publication(name: "")
Magazine extend Publication(name: "")

Catalog(title: "Alpha Guide", confidence: 0.82, category: Book.class)
Catalog(title: "beta guide", confidence: 0.74, category: Magazine.class)
Catalog(title: "Reference", confidence: 0.40, category: Publication.class)

def main() =>
    guides := lambda(Catalog, row => str.contains(
        data: str.lower(data: row.title),
        needle: "guide"
    ))
    related := lambda(Catalog, row =>
        row.category == Book.class or
        row.category == Magazine.class or
        row.category == Publication.class
    )
    confident := lambda(Catalog, row =>
        row.confidence >= 0.70 and row.confidence <= 0.90
    )
    return (
        guides: count(data: guides),
        related: count(data: related),
        confident: count(data: confident)
    )
end
