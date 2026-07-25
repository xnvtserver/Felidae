# Native CSV stdlib declarations. Bodies are implemented by the native module bridge.

csv.parse(data: string, access: array) => ()
csv.toFacts(data: string, type: string, access: array) => ()
csv.toText(data: array, access: string) => ()
csv.toFelidaeFacts(data: array, type: string, access: string) => ()
csv.addRow(data: array, row: any, access: array) => ()
csv.findRows(data: array, key: string, value: string, access: array) => ()
csv.updateRows(data: array, key: string, value: string, patch: any, access: array) => ()
csv.deleteRows(data: array, key: string, value: string, access: array) => ()
