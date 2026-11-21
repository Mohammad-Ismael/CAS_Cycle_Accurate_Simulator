#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include "svdpi.h"

// Forward declaration of exported SV functions
extern int  sv_compute_checksum(int a, int b);
extern int  sv_get_status(void);
extern void sv_set_status(int status);

int c_test_context_and_export(int key_seed, int val) {
    // 1. Check scope
    svScope scope = svGetScope();
    if (!scope) {
        printf("[C ERR] svGetScope returned NULL!\n");
        return -1;
    }

    const char* sname = svGetNameFromScope(scope);
    if (!sname || strstr(sname, "tb_v4_6_0_0_context_export") == NULL) {
        printf("[C ERR] Scope name unexpected: '%s'\n", sname ? sname : "NULL");
        return -2;
    }

    // 2. User Data API
    void* dummy_key = (void*)(uintptr_t)(key_seed);
    void* user_data = (void*)0xCAFEFEED;
    if (svPutUserData(scope, dummy_key, user_data) != 0) {
        printf("[C ERR] svPutUserData failed!\n");
        return -3;
    }
    void* retrieved = svGetUserData(scope, dummy_key);
    if (retrieved != user_data) {
        printf("[C ERR] svGetUserData mismatch: got %p exp %p\n", retrieved, user_data);
        return -4;
    }

    // 3. Time API
    svTimeVal cur_time;
    if (svGetTime(scope, &cur_time) != 0) {
        printf("[C ERR] svGetTime failed!\n");
        return -5;
    }

    // 4. DPI-C Export Callback
    sv_set_status(val + 10);
    int status = sv_get_status();
    if (status != val + 10) {
        printf("[C ERR] sv_get_status mismatch: got %d exp %d\n", status, val + 10);
        return -6;
    }

    int csum = sv_compute_checksum(val, 0x1234);
    return csum;
}
