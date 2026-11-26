// ascon_constants.sv
// file: ascon_constants.sv start

`default_nettype none
package ascon_constants;

    localparam int MESSAGE_SIZE     = 512;       // Message size in bits (multiple of 128)
    localparam int AD_SIZE          = 128;            // Associated Data size in bits (Arbitrary width AD_SIZE >= 0)
    
    // Configuration Parameters
    localparam bit PARALLEL_ENC_DEC = 0;     // 1: Separate logic for Enc & Dec; 0: Shared logic
    localparam bit ENCRYPTION_CORE  = 1;     // 1: Enable encryption capability
    localparam bit DECRYPTION_CORE  = 1;     // 1: Enable decryption capability

    localparam int ACCELERATED_ENC_LEVEL = 0; // 0: 1 round/cycle, 1: 2 rounds/cycle, 2: 4 rounds/cycle, 3: full permutation
    localparam int ACCELERATED_DEC_LEVEL = 0;  // 0: 1 round/cycle, 1: 2 rounds/cycle, 2: 4 rounds/cycle, 3: full permutation
    
    localparam logic [63:0] IV   = 64'h00001000808c0001;      // Ascon-128a
    localparam logic [63:0] DSEP = 64'h8000000000000000;
    
    // Round constants for 12 rounds
    localparam logic [7:0] RC12[0:11] = '{8'hf0, 8'he1, 8'hd2, 8'hc3,8'hb4, 8'ha5, 8'h96, 8'h87,8'h78, 8'h69, 8'h5a, 8'h4b};
    
    // Round constants for 8 rounds (starting from RC4)
    localparam logic [7:0] RC8[0:7] = '{8'hb4, 8'ha5, 8'h96, 8'h87,8'h78, 8'h69, 8'h5a, 8'h4b};
    
    function automatic logic [63:0] ror(input logic [63:0] x, input int n);
        return (x >> n) | (x << (64 - n));
    endfunction

    function automatic logic [63:0] byteswap64(input logic [63:0] x);
    return {x[7:0], x[15:8], x[23:16], x[31:24], 
            x[39:32], x[47:40], x[55:48], x[63:56]};
    endfunction

    // Convert from Verilog internal order to C output order
    function automatic logic [127:0] to_c_order(input logic [127:0] data);
        logic [7:0] bytes [0:15];
        for (int i = 0; i < 16; i++) begin
            bytes[i] = data[(15-i)*8 +: 8];
        end
        return {bytes[0],  bytes[1],  bytes[2],  bytes[3],
                bytes[4],  bytes[5],  bytes[6],  bytes[7],
                bytes[8],  bytes[9],  bytes[10], bytes[11],
                bytes[12], bytes[13], bytes[14], bytes[15]};
    endfunction
endpackage

// file: ascon_constants.sv end