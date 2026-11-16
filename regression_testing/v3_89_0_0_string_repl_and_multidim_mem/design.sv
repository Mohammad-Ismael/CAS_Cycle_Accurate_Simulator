`timescale 1ns/1ps
/* verilator lint_off MULTIDRIVEN */
/* verilator lint_off DECLFILENAME */

//==============================================================================
// Module: v3_89_0_0_string_repl_and_multidim_mem
// Standard: IEEE 1800-2023 SystemVerilog (§11.4.12.2 string repl, §11.5.2 multi-dim array)
// Description:
//   Verification module exercising:
//   1. IEEE 1800 §11.4.12.2 string replication ({4{"test"}}, {3{"ab_"}}, {0{"skip"}})
//      and string equality comparison.
//   2. IEEE 1800 §11.5.2 multi-dimensional unpacked arrays:
//      - 2D array: logic [7:0] mem2d [0:15][0:3]
//      - 3D array: logic [15:0] mem3d [0:3][0:3][0:1]
//      - Blocking (=) and non-blocking (<=) writes
//      - Reads across multiple [i][j] and [i][j][k] coordinates
//      - Packed bit-select after unpacked indexing (mem2d[5][2][3], mem3d[2][1][0][7])
//==============================================================================

module v3_89_0_0_string_repl_and_multidim_mem (
    input  logic         clk,
    input  logic         rst_n,

    // String replication match outputs
    output logic         str_match_test4,
    output logic         str_match_ab3,
    output logic         str_match_skip0,

    // Blocking assign verification flags
    output logic [7:0]   blk_2d_val_0_0,
    output logic [7:0]   blk_2d_val_5_2,
    output logic [7:0]   blk_2d_val_15_3,
    output logic         blk_2d_bit_5_2_3,

    output logic [15:0]  blk_3d_val_0_0_0,
    output logic [15:0]  blk_3d_val_2_1_0,
    output logic [15:0]  blk_3d_val_3_3_1,
    output logic         blk_3d_bit_2_1_0_7,

    // NBA write control ports
    input  logic         nba_we_2d,
    input  logic [3:0]   nba_waddr_2d_i,
    input  logic [1:0]   nba_waddr_2d_j,
    input  logic [7:0]   nba_wdata_2d,

    input  logic         nba_we_3d,
    input  logic [1:0]   nba_waddr_3d_i,
    input  logic [1:0]   nba_waddr_3d_j,
    input  logic         nba_waddr_3d_k,
    input  logic [15:0]  nba_wdata_3d,

    // NBA readback ports
    input  logic [3:0]   raddr_2d_i,
    input  logic [1:0]   raddr_2d_j,
    output logic [7:0]   rdata_2d,
    output logic         rdata_2d_bit,

    input  logic [1:0]   raddr_3d_i,
    input  logic [1:0]   raddr_3d_j,
    input  logic         raddr_3d_k,
    output logic [15:0]  rdata_3d,
    output logic         rdata_3d_bit
);

    //--------------------------------------------------------------------------
    // 1. IEEE 1800 §11.4.12.2 String Replication
    //--------------------------------------------------------------------------
    string s_test4;
    string s_ab3;
    string s_skip0;

    initial begin
        s_test4 = {4{"test"}};
        s_ab3   = {3{"ab_"}};
        s_skip0 = {0{"skip"}};

        str_match_test4 = (s_test4 == "testtesttesttest");
        str_match_ab3   = (s_ab3   == "ab_ab_ab_");
        str_match_skip0 = (s_skip0 == "");
    end

    //--------------------------------------------------------------------------
    // 2. IEEE 1800 §11.5.2 Multi-Dimensional Unpacked Arrays
    //--------------------------------------------------------------------------
    // 2D: 16 rows, 4 columns of 8-bit bytes
    logic [7:0]  mem2d [0:15][0:3];

    // 3D: 4 planes, 4 rows, 2 depths of 16-bit words
    logic [15:0] mem3d [0:3][0:3][0:1];

    // Procedural blocking writes and reads in initial block
    initial begin
        // Blocking writes to distinct coordinates
        mem2d[0][0]   = 8'hA1;
        mem2d[5][2]   = 8'hB8; // 8'b10111000 -> bit 3 is 1
        mem2d[15][3]  = 8'hFE;

        mem3d[0][0][0] = 16'h1234;
        mem3d[2][1][0] = 16'h8080; // bit 7 is 1
        mem3d[3][3][1] = 16'hDEAD;

        // Blocking reads
        blk_2d_val_0_0   = mem2d[0][0];
        blk_2d_val_5_2   = mem2d[5][2];
        blk_2d_val_15_3  = mem2d[15][3];
        blk_2d_bit_5_2_3 = mem2d[5][2][3];

        blk_3d_val_0_0_0   = mem3d[0][0][0];
        blk_3d_val_2_1_0   = mem3d[2][1][0];
        blk_3d_val_3_3_1   = mem3d[3][3][1];
        blk_3d_bit_2_1_0_7 = mem3d[2][1][0][7];
    end

    // Synchronous non-blocking writes
    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            // reset logic if needed
        end else begin
            if (nba_we_2d) begin
                mem2d[nba_waddr_2d_i][nba_waddr_2d_j] <= nba_wdata_2d;
            end
            if (nba_we_3d) begin
                mem3d[nba_waddr_3d_i][nba_waddr_3d_j][nba_waddr_3d_k] <= nba_wdata_3d;
            end
        end
    end

    // Procedural/combinational reads for dynamic index readback
    always_comb begin
        rdata_2d     = mem2d[raddr_2d_i][raddr_2d_j];
        rdata_2d_bit = mem2d[raddr_2d_i][raddr_2d_j][3];

        rdata_3d     = mem3d[raddr_3d_i][raddr_3d_j][raddr_3d_k];
        rdata_3d_bit = mem3d[raddr_3d_i][raddr_3d_j][raddr_3d_k][7];
    end

endmodule
