// ============================================================================
// CAS Milestone DPI-C Export & Context Reference Model
// Target Release : v3.13.0.0
// Standard Target: IEEE Std 1800-2023 §35 (DPI-C context, export, and svScope)
// ============================================================================

#include <stdint.h>
#include <stdio.h>

// DPI header definition for svScope
typedef void* svScope;
extern svScope svGetScope(void);
extern svScope svSetScope(const svScope scope);

// Forward declarations of exported SystemVerilog functions
extern void sv_write_reg(int addr, int data);
extern int  sv_read_reg(int addr);

// Imported C function with context: coordinates bidirectional transaction
int c_run_bus_transaction(int start_addr, int base_data) {
    // Write across bus into SV module state
    sv_write_reg(start_addr, base_data);
    sv_write_reg(start_addr + 4, base_data ^ 0xA5A5A5A5);

    // Read back and return composite checksum
    int r0 = sv_read_reg(start_addr);
    int r1 = sv_read_reg(start_addr + 4);

    return r0 + r1;
}