[33m3f326d3[m[33m ([m[1;36mHEAD[m[33m -> [m[1;32mkalmanfilter[m[33m, [m[1;31morigin/kalmanfilter[m[33m)[m Small modifications, hopefully didnt break anything. Have not ran it through my visual tester but it seems to be behaving the exact same on a text-based output.
[33m6348562[m Lots of fixes. Only offers about a 5x speedup after adding a better algorithm for centroid threshold robustness in the tracking phase. Work in progress for sure, I think I can optimize it.
[33ma33d002[m[33m ([m[1;32msfml-kalman[m[33m)[m Kalman filter. 14x faster than LiS, down from 17x with no filter.
[33m55d873a[m[33m ([m[1;31morigin/tcetroidemode[m[33m, [m[1;32mtcetroidemode[m[33m)[m Small Fix.
[33mb918b7a[m Cuts 99.969% of instructions in the centroid + id cycle, leaving the attitude determination unchanged. Attitude determination could possibly start hot, but I am not sure how to implement just yet. Entire thing is able to be parallelized, but I have not done so yet. Code is relatively Cache Friendly. No Kalman Filter just yet.
[33m2f12516[m[33m ([m[1;31morigin/main[m[33m, [m[1;31morigin/HEAD[m[33m, [m[1;32mmain[m[33m)[m Many small fixes.
[33m387ca8a[m Small fix
[33me83d028[m Initial commit: Optimized Star Tracking Pipeline (25k ns target)
