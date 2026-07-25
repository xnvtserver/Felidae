| Case | Avg | Min | Max | Peak RAM |
|---|---:|---:|---:|---:|
| normal direct main | `32.36 ms` | `20.91 ms` | `44.16 ms` | `4.25 MB max` |
| normal thread memory | `32.92 ms` | `32.07 ms` | `34.79 ms` | `4.39 MB max` |
| normal stdlib utilities | `35.02 ms` | `29.44 ms` | `49.04 ms` | `3.25 MB max` |
| debug executes main | `28.65 ms` | `22.58 ms` | `32.81 ms` | `4.41 MB max` |
| debug `--check` warnings | `31.92 ms` | `30.26 ms` | `32.81 ms` | `4.37 MB max` |
| full `felidae_test_suite.fx` | `808.48 ms` | `730.15 ms` | `882.46 ms` | `5.45 MB max` |

## Predictability run after eager incremental registration

Nine measured processes per fixture, following one warm-up. `Total CV` exposes
host process-launch and scheduling noise; load and execution are medians from
the interpreter's internal steady-clock metrics. Search cases run the
Felidae-level algorithms 10,000 times in-process; they are not interpreter
builtins.

| Case | Total median | Total CV | Load median | Execute median | First query | Repeated query |
|---|---:|---:|---:|---:|---:|---:|
| direct main | 21.34 ms | 19.2% | 1.29 ms | 0.49 ms | 0.000 ms | 0.000 ms |
| recursive backtracking | 22.27 ms | 23.5% | 0.72 ms | 1.07 ms | 0.295 ms | 0.007 ms |
| indexed fact property | 21.72 ms | 3.6% | 3.10 ms | 0.46 ms | 0.103 ms | 0.003 ms |
| full fact scan | 22.33 ms | 4.1% | 3.07 ms | 3.31 ms | 1.037 ms | 0.059 ms |
| thread snapshot | 21.86 ms | 4.7% | 1.17 ms | 1.39 ms | 0.000 ms | 0.000 ms |
| stdlib utilities | 21.80 ms | 21.3% | 1.03 ms | 7.95 ms | 0.000 ms | 0.000 ms |
| fact reasoning | 40.43 ms | 14.7% | 5.31 ms | 22.05 ms | 0.000 ms | 0.000 ms |
| linear search | 86.59 ms | 14.1% | 1.47 ms | 56.07 ms | 23.135 ms | 0.003 ms |
| binary search | 53.71 ms | 10.9% | 1.43 ms | 37.15 ms | 3.620 ms | 0.003 ms |
