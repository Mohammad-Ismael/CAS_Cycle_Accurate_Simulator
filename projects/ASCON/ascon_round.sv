// ascon_round.sv
// file: ascon_round.sv start

// ascon_round.sv - EXACT SEQUENTIAL MATCH to C

`default_nettype none
import ascon_constants::*;

module ascon_round (
    input  logic [63:0] x0_in, x1_in, x2_in, x3_in, x4_in,
    input  logic [7:0]  rc,
    output logic [63:0] x0_out, x1_out, x2_out, x3_out, x4_out
);
    logic [63:0] x0, x1, x2, x3, x4;
    logic [63:0] t;
    
    always_comb begin
        x0 = x0_in;
        x1 = x1_in;
        x2 = x2_in;
        x3 = x3_in;
        x4 = x4_in;
        
        // Round constant
        x2[7:0] = x2[7:0] ^ rc;
        
        // S-box (sequential as in C)
        x0 = x0 ^ x4;
        x4 = x4 ^ x3;
        x2 = x2 ^ x1;
        t = x0 & ~x4;
        x0 = x0 ^ (x2 & ~x1);
        x2 = x2 ^ (x4 & ~x3);
        x4 = x4 ^ (x1 & ~x0);
        x1 = x1 ^ (x3 & ~x2);
        x3 = x3 ^ t;
        x1 = x1 ^ x0;
        x3 = x3 ^ x2;
        x0 = x0 ^ x4;
        
        // Linear layer (from C: xtemp = x0 ^ ROR(x0,9); x0 ^= ROR(xtemp,19))
        t = x0 ^ ror(x0, 9);
        x0 = x0 ^ ror(t, 19);
        
        t = x1 ^ ror(x1, 22);
        x1 = x1 ^ ror(t, 39);
        
        t = x2 ^ ror(x2, 5);
        x2 = x2 ^ ror(t, 1);
        
        t = x3 ^ ror(x3, 7);
        x3 = x3 ^ ror(t, 10);
        
        t = x4 ^ ror(x4, 34);
        x4 = x4 ^ ror(t, 7);
        
        // Final NOT
        x2 = ~x2;
        
        x0_out = x0;
        x1_out = x1;
        x2_out = x2;
        x3_out = x3;
        x4_out = x4;
    end
endmodule

// file: ascon_round.sv end