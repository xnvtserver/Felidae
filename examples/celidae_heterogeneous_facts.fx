# Deliberately heterogeneous fact data, used to test Celidae's visualization
# layer against the shapes real programs actually have.
#
# examples/celidae_business_facts.fx is a good corpus but a uniform one: one
# dominant fact type, numeric measures throughout, and a date on every record.
# Tuning a visualiser against only that produces a visualiser that assumes
# those things, and the assumptions do not survive contact with anything else.
#
# This file mixes, on purpose:
#
#   Region     three records, pure labels, no measures, no date - a lookup
#              table, which is what most reference facts are
#   Warehouse  a nominal code field ("007") that must not be averaged, and a
#              foreign key to Region that is nowhere declared as one
#   Shipment   the only type carrying a date, so time-based views must apply
#              to this type and decline for the others
#   Product    a text corpus with real vocabulary structure and no numbers a
#              histogram could use
#   Sensor     wide numeric records with a deliberate collinear pair, so
#              redundancy detection has something to find
#   Audit      a single record: every statistical view must decline rather
#              than compute a standard deviation of one value
#
# Between them the joins are Shipment -> Warehouse -> Region and
# Shipment -> Product, none of which is written down anywhere: they exist
# only as overlapping values, which is how a real converted CSV arrives.

Region(name: "north", timezone: "UTC+1", tier: "primary")
Region(name: "south", timezone: "UTC+2", tier: "primary")
Region(name: "coastal", timezone: "UTC+1", tier: "secondary")

Warehouse(code: "007", label: "North Yard", region: "north", capacity: 1200)
Warehouse(code: "013", label: "North Depot", region: "north", capacity: 800)
Warehouse(code: "021", label: "South Yard", region: "south", capacity: 1500)
Warehouse(code: "094", label: "South Depot", region: "south", capacity: 640)
Warehouse(code: "108", label: "Coastal Terminal", region: "coastal", capacity: 2200)
Warehouse(code: "117", label: "Coastal Annex", region: "coastal", capacity: 430)

Product(sku: "AX-100", title: "Steel Bearing Assembly", material: "steel")
Product(sku: "AX-140", title: "Steel Bearing Housing", material: "steel")
Product(sku: "AX-220", title: "Steel Shaft Coupling", material: "steel")
Product(sku: "PL-310", title: "Polymer Seal Ring", material: "polymer")
Product(sku: "PL-355", title: "Polymer Seal Gasket", material: "polymer")
Product(sku: "PL-390", title: "Polymer Bearing Sleeve", material: "polymer")
Product(sku: "BR-410", title: "Brass Valve Body", material: "brass")
Product(sku: "BR-455", title: "Brass Valve Stem", material: "brass")

Shipment(reference: "S-0001", warehouse: "007", product: "AX-100", placed: "2024-01-14", units: 40, weight: 512)
Shipment(reference: "S-0002", warehouse: "007", product: "PL-310", placed: "2024-01-22", units: 120, weight: 240)
Shipment(reference: "S-0003", warehouse: "013", product: "AX-140", placed: "2024-02-03", units: 35, weight: 455)
Shipment(reference: "S-0004", warehouse: "021", product: "BR-410", placed: "2024-02-17", units: 60, weight: 780)
Shipment(reference: "S-0005", warehouse: "021", product: "AX-220", placed: "2024-03-05", units: 25, weight: 325)
Shipment(reference: "S-0006", warehouse: "094", product: "PL-355", placed: "2024-03-19", units: 200, weight: 400)
Shipment(reference: "S-0007", warehouse: "108", product: "BR-455", placed: "2024-04-02", units: 45, weight: 585)
Shipment(reference: "S-0008", warehouse: "108", product: "PL-390", placed: "2024-04-21", units: 150, weight: 300)
Shipment(reference: "S-0009", warehouse: "117", product: "AX-100", placed: "2024-05-09", units: 30, weight: 384)
Shipment(reference: "S-0010", warehouse: "007", product: "AX-140", placed: "2024-05-28", units: 55, weight: 715)
Shipment(reference: "S-0011", warehouse: "013", product: "PL-310", placed: "2024-06-11", units: 180, weight: 360)
Shipment(reference: "S-0012", warehouse: "021", product: "BR-410", placed: "2024-06-30", units: 70, weight: 910)
Shipment(reference: "S-0013", warehouse: "094", product: "AX-220", placed: "2024-07-15", units: 20, weight: 260)
Shipment(reference: "S-0014", warehouse: "108", product: "PL-355", placed: "2024-08-04", units: 220, weight: 440)
Shipment(reference: "S-0015", warehouse: "117", product: "BR-455", placed: "2024-08-23", units: 50, weight: 650)
Shipment(reference: "S-0016", warehouse: "007", product: "PL-390", placed: "2024-09-10", units: 160, weight: 320)

# weight is 13x units for steel, 2x for polymer, 13x for brass - a genuine
# relationship the driver analysis should find, rather than a manufactured one.

Sensor(unit: "north", reading_c: 14, reading_f: 57, humidity: 62, pressure: 1013)
Sensor(unit: "north", reading_c: 16, reading_f: 61, humidity: 58, pressure: 1011)
Sensor(unit: "north", reading_c: 12, reading_f: 54, humidity: 71, pressure: 1018)
Sensor(unit: "south", reading_c: 24, reading_f: 75, humidity: 44, pressure: 1009)
Sensor(unit: "south", reading_c: 27, reading_f: 81, humidity: 39, pressure: 1006)
Sensor(unit: "south", reading_c: 22, reading_f: 72, humidity: 47, pressure: 1010)
Sensor(unit: "coastal", reading_c: 18, reading_f: 64, humidity: 80, pressure: 1015)
Sensor(unit: "coastal", reading_c: 19, reading_f: 66, humidity: 83, pressure: 1014)
Sensor(unit: "coastal", reading_c: 17, reading_f: 63, humidity: 78, pressure: 1016)
Sensor(unit: "coastal", reading_c: 20, reading_f: 68, humidity: 76, pressure: 1012)

# reading_c and reading_f are the same measurement twice. Redundancy detection
# should say so instead of reporting it as a discovered correlation.

Audit(event: "schema-migrated", actor: "ops", at: "2024-01-01")
