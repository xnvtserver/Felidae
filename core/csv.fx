# CSV operations are compiler-known declarations. The compiler emits a
# Builtin IR instruction and RegisterVm calls src/form/libs/Csv.cpp directly.

csv.parse(data: string) => ()
csv.toFacts(data: string, type: string) => ()
# The source-path overload records which file these facts came from, so a
# later db.sync(file: source) writes back only this file's facts instead of
# the whole store (see VmFactStore::recordSource / snapshotBySource). Use
# this form, not the two-argument one, for any facts meant to be synced.
csv.toFacts(data: string, type: string, source: string) => ()
csv.toText(data: array) => ()
csv.toFelidaeFacts(data: array, type: string) => ()
