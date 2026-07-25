| Case | Avg | Min | Max | Peak RAM |
|---|---:|---:|---:|---:|
| normal direct main | `32.36 ms` | `20.91 ms` | `44.16 ms` | `4.25 MB max` |
| normal thread memory | `32.92 ms` | `32.07 ms` | `34.79 ms` | `4.39 MB max` |
| normal stdlib utilities | `35.02 ms` | `29.44 ms` | `49.04 ms` | `3.25 MB max` |
| debug executes main | `28.65 ms` | `22.58 ms` | `32.81 ms` | `4.41 MB max` |
| debug `--check` warnings | `31.92 ms` | `30.26 ms` | `32.81 ms` | `4.37 MB max` |
| full `felidae_test_suite.fx` | `808.48 ms` | `730.15 ms` | `882.46 ms` | `5.45 MB max` |