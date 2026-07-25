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
| direct main | 28.00 ms | 30.7% | 1.06 ms | 0.22 ms | 0.000 ms | 0.000 ms |
| recursive backtracking | 20.46 ms | 20.1% | 0.71 ms | 1.48 ms | 0.436 ms | 0.011 ms |
| indexed fact property | 24.28 ms | 24.1% | 3.00 ms | 0.44 ms | 0.100 ms | 0.003 ms |
| full fact scan | 33.27 ms | 23.9% | 3.67 ms | 3.48 ms | 1.213 ms | 0.083 ms |
| thread snapshot | 25.79 ms | 24.7% | 0.92 ms | 1.13 ms | 0.000 ms | 0.000 ms |
| stdlib utilities | 35.31 ms | 24.8% | 0.94 ms | 11.35 ms | 0.000 ms | 0.000 ms |
| fact reasoning | 50.99 ms | 42.2% | 4.12 ms | 29.76 ms | 0.000 ms | 0.000 ms |
| linear search | 108.99 ms | 10.4% | 1.76 ms | 81.64 ms | 31.920 ms | 0.005 ms |
| binary search | 76.61 ms | 13.4% | 1.58 ms | 50.29 ms | 3.260 ms | 0.005 ms |
