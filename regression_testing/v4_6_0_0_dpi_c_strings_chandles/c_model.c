#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "svdpi.h"

typedef struct {
    uint32_t tag;
    int32_t val;
} payload_t;

// Chandle allocator
void* c_alloc_payload(int tag, int val) {
    payload_t* p = (payload_t*)malloc(sizeof(payload_t));
    if (p) {
        p->tag = (uint32_t)tag;
        p->val = (int32_t)val;
    }
    return (void*)p;
}

int c_read_payload_tag(void* handle) {
    if (!handle) return -1;
    return (int)((payload_t*)handle)->tag;
}

int c_read_payload_val(void* handle) {
    if (!handle) return -1;
    return ((payload_t*)handle)->val;
}

void c_free_payload(void* handle) {
    if (handle) {
        free(handle);
    }
}

// String test
int c_string_length(const char* str) {
    if (!str) return 0;
    return (int)strlen(str);
}

const char* c_prefix_string(const char* prefix, const char* name) {
    static char buf[256];
    snprintf(buf, sizeof(buf), "[%s] %s", prefix ? prefix : "", name ? name : "");
    return buf;
}
