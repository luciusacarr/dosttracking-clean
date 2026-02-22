| Command | Mean [s] | Min [s] | Max [s] | Relative |
|:---|---:|---:|---:|---:|
| ` ./lost pipeline --image-dir "sky_density_test/images/" --fov 25 --centroid-algo cog --database "my-database.dat" --star-id-algo py --attitude-algo dqm --print-attitude "output.txt"  --time-between-frame .1 --tracking-mode false` | 4.930 ± 0.086 | 4.835 | 5.133 | 1.24 ± 0.03 |
| `./lost pipeline --image-dir "sky_density_test/images/" --fov 25 --centroid-algo cog --database "my-database.dat" --star-id-algo py --attitude-algo dqm --print-attitude "output.txt" --last-ra -1 --last-dec -1 --last-roll -1 --ra-velocity 0 --dec-velocity 0 --roll-velocity 0 --time-between-frame .1 --tracking-mode true` | 3.971 ± 0.073 | 3.901 | 4.135 | 1.00 |
