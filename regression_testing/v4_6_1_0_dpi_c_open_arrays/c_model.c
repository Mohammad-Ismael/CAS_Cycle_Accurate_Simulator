#include "svdpi.h"
#include <stdio.h>

int sv_sum_array(const svOpenArrayHandle arr) {
    int dims = svDimensions(arr);
    int sz = svSize(arr, 1);
    int total = 0;
    for (int i = 0; i < sz; ++i) {
        int* elem = (int*)svGetArrElemPtr1(arr, i);
        if (elem) total += *elem;
    }
    return total;
}

void sv_scale_array(const svOpenArrayHandle arr, int factor) {
    int sz = svSize(arr, 1);
    for (int i = 0; i < sz; ++i) {
        int* elem = (int*)svGetArrElemPtr1(arr, i);
        if (elem) *elem *= factor;
    }
}

int sv_check_2d(const svOpenArrayHandle arr) {
    int dims = svDimensions(arr);
    if (dims != 2) return -1;
    int d1 = svSize(arr, 1);
    int d2 = svSize(arr, 2);
    int total = 0;
    for (int i = 0; i < d1; ++i) {
        for (int j = 0; j < d2; ++j) {
            int* elem = (int*)svGetArrElemPtr2(arr, i, j);
            if (elem) total += *elem;
        }
    }
    return total;
}
