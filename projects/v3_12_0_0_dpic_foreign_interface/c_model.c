// ============================================================================
// CAS Milestone DPI-C Foreign Reference Model
// Target Release : v3.12.0.0
// Standard Target: IEEE Std 1800-2023 §35 (DPI-C Foreign Functions)
// ============================================================================

#include <stdint.h>

// DPI-C pure function: calculates simple hash / polynomial
int c_calc_hash(int seed, int data) {
    return (seed ^ (data << 3)) + 0x5A5A;
}

// DPI-C standard function: verifies payload and returns status
int c_verify_packet(int cmd, int addr, int payload) {
    int expected = (cmd << 16) | (addr & 0xFFFF);
    return (payload == expected) ? 1 : 0;
}