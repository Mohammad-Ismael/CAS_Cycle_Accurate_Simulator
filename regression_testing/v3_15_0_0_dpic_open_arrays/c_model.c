// ============================================================================
// CAS Milestone DPI-C Open Arrays Reference Model
// Target Release : v3.15.0.0
// Standard Target: IEEE Std 1800-2023 §35.5.6.1 & Annex I.9 (Open Arrays)
// ============================================================================

#include <stdint.h>
#include <stdio.h>

// DPI-C Open Array handle type and inquiry functions per Annex I.9
typedef void* svOpenArrayHandle;

extern int   svSize(const svOpenArrayHandle h, int d);
extern void* svGetArrElemPtr1(const svOpenArrayHandle h, int indx);

// Compute sum of elements passed via open array
int c_sum_open_array(const svOpenArrayHandle arr_handle) {
    if (!arr_handle) return 0;

    int size = svSize(arr_handle, 1);
    int sum = 0;

    for (int i = 0; i < size; i++) {
        int* elem_ptr = (int*)svGetArrElemPtr1(arr_handle, i);
        if (elem_ptr) {
            sum += *elem_ptr;
        }
    }
    return sum;
}

// Compute XOR reduction and add size prefix
int c_xor_reduce_open_array(const svOpenArrayHandle arr_handle) {
    if (!arr_handle) return 0;

    int size = svSize(arr_handle, 1);
    int xor_val = 0;

    for (int i = 0; i < size; i++) {
        int* elem_ptr = (int*)svGetArrElemPtr1(arr_handle, i);
        if (elem_ptr) {
            xor_val ^= *elem_ptr;
        }
    }
    return (size << 16) | (xor_val & 0xFFFF);
}