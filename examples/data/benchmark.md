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
the interpreter's internal steady-clock metrics.

| Case | Total median | Total CV | Load median | Execute median | First query | Repeated query |
|---|---:|---:|---:|---:|---:|---:|
| direct main | 18.94 ms | 20.5% | 0.91 ms | 0.17 ms | 0.000 ms | 0.000 ms |
| recursive backtracking | 32.83 ms | 23.4% | 1.09 ms | 1.63 ms | 0.475 ms | 0.010 ms |
| indexed fact property | 18.94 ms | 23.2% | 3.26 ms | 0.60 ms | 0.131 ms | 0.004 ms |
| full fact scan | 19.84 ms | 24.4% | 2.72 ms | 3.46 ms | 1.155 ms | 0.082 ms |
| thread snapshot | 20.94 ms | 18.3% | 1.00 ms | 1.16 ms | 0.000 ms | 0.000 ms |
| stdlib utilities | 32.73 ms | 19.0% | 0.80 ms | 7.33 ms | 0.000 ms | 0.000 ms |
| fact reasoning | 50.26 ms | 21.0% | 3.73 ms | 22.61 ms | 0.000 ms | 0.000 ms |
| linear search | 18.80 ms | 28.7% | 0.72 ms | 4.56 ms | 0.206 ms | 0.004 ms |
| binary search | 19.46 ms | 27.3% | 0.65 ms | 3.29 ms | 0.179 ms | 0.003 ms |
