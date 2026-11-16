// ============================================================================
// CAS Milestone DPI-C Chandle & Open Arrays Reference Model
// Target Release : v3.14.0.0
// Standard Target: IEEE Std 1800-2023 §35.5.4 & §35.5.6.1 (chandle, open arrays)
// ============================================================================

#include <stdint.h>
#include <stdlib.h>

// Internal opaque struct managed by C
typedef struct {
    uint32_t session_id;
    uint32_t accumulator;
    uint32_t count;
} session_context_t;

// Allocate opaque context on heap and return pointer as chandle
void* c_create_session(int session_id) {
    session_context_t* ctx = (session_context_t*)malloc(sizeof(session_context_t));
    if (ctx) {
        ctx->session_id  = (uint32_t)session_id;
        ctx->accumulator = 0;
        ctx->count       = 0;
    }
    return (void*)ctx;
}

// Stateful update through chandle pointer
int c_update_session(void* handle, int val) {
    if (!handle) return -1;
    session_context_t* ctx = (session_context_t*)handle;
    ctx->accumulator += (uint32_t)val;
    ctx->count++;
    return (int)(ctx->accumulator + ctx->session_id);
}

// Release opaque context from heap
void c_destroy_session(void* handle) {
    if (handle) {
        free(handle);
    }
}