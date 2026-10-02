# Felidae word vocabulary model

`model.txt` is a line-oriented vocabulary. The physical line number is the
stable token ID, starting at zero. The first 59 lines are Felidae's fixed
grammar IDs and must not be reordered. `<0xNN>` denotes one byte so that
whitespace and arbitrary UTF-8 remain reversible. Identifier words absent
from this seed receive deterministic, parse-local IDs in memory.

The initial word entries are a small checked-in text-dictionary seed. The
runtime never rewrites this table. The directory name is retained for
repository compatibility; lookup is deterministic and whole-word based.
