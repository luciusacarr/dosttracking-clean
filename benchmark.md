| Command | Mean [s] | Min [s] | Max [s] | Relative |
|:---|---:|---:|---:|---:|
| ` ./lost pipeline --image-dir "sky_density_test/images/" --fov 25 --centroid-algo cog --database "my-database.dat" --star-id-algo py --attitude-algo dqm --print-attitude "output.txt"  --time-between-frame .1 --tracking-mode false` | 3.952 ± 0.261 | 3.729 | 4.473 | 1.11 ± 0.10 |
| `./lost pipeline --image-dir "sky_density_test/images/" --fov 25 --centroid-algo cog --database "my-database.dat" --star-id-algo py --attitude-algo dqm --print-attitude "output.txt"  --time-between-frame .1 --tracking-mode true` | 3.568 ± 0.213 | 3.366 | 3.885 | 1.00 |
