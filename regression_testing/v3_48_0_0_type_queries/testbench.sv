`timescale 1ns/1ps

// ============================================================================
// CAS Milestone Regression Testbench
// Target Release : v3.48.0.0
// Standard Target: IEEE Std 1800-2023 §20.6 / §6.23 ($typename Type Queries)
// Testbench Name : tb_v3_48_0_regression
// ============================================================================

module tb_v3_48_0_regression;
    import v3_48_0_0_pkg::*;

    logic       clk;
    logic       rst_n;
    logic [7:0] byte_in;
    logic       valid_schema;

    int pass_count  = 0;
    int error_count = 0;

    v3_48_0_0_type_introspector dut (
        .clk         (clk),
        .rst_n       (rst_n),
        .byte_in     (byte_in),
        .valid_schema(valid_schema)
    );

    initial begin
        clk = 0;
        forever #5 clk = ~clk;
    end

    initial begin
        packet_c pkt;
        string   t_prim;
        string   t_enum;
        string   t_class;
        string   t_struct;

        rst_n   = 0;
        byte_in = 8'hA5;

        #12;
        rst_n = 1;

        // --------------------------------------------------------------------
        // Test 1: Primitive type string introspection
        // --------------------------------------------------------------------
        t_prim = $typename(int);
        if (t_prim == "int") begin
            $display("[PASS T=%0t] Primitive type introspection verified: $typename(int) = %s",
                     $time, t_prim);
            pass_count++;
        end else begin
            $display("[FAIL T=%0t] Primitive type introspection failed! got '%s' (exp 'int')",
                     $time, t_prim);
            error_count++;
        end

        // --------------------------------------------------------------------
        // Test 2: User-defined enum, struct, and class handle type queries
        // Conforms to VCS formatting with category prefix ('enum ', 'struct ', 'class ')
        // --------------------------------------------------------------------
        pkt = new(42);
        t_enum   = $typename(state_e);
        t_struct = $typename(frame_hdr_s);
        t_class  = $typename(pkt);

        if (t_enum   == "enum v3_48_0_0_pkg::state_e" &&
            t_struct == "struct v3_48_0_0_pkg::frame_hdr_s" &&
            t_class  == "class v3_48_0_0_pkg::packet_c") begin
            $display("[PASS T=%0t] UDT introspection verified: enum=%s struct=%s class=%s",
                     $time, t_enum, t_struct, t_class);
            pass_count++;
        end else begin
            $display("[FAIL T=%0t] UDT introspection failed! enum='%s' struct='%s' class='%s'",
                     $time, t_enum, t_struct, t_class);
            error_count++;
        end

        // --------------------------------------------------------------------
        // Test 3: Module DUT runtime schema check
        // --------------------------------------------------------------------
        @(posedge clk);
        #1;
        if (valid_schema === 1'b1) begin
            $display("[PASS T=%0t] DUT runtime type query schema match verified: valid_schema=%b",
                     $time, valid_schema);
            pass_count++;
        end else begin
            $display("[FAIL T=%0t] DUT runtime type query schema match failed! valid_schema=%b",
                     $time, valid_schema);
            error_count++;
        end

        #10;
        $display("\n================================================================================");
        $display("[CAS v3.48.0.0 REGRESSION SUITE: TYPE QUERIES $typename §20.6]");
        if (error_count == 0 && pass_count == 3) begin
            $display("RESULT : >>> PASS (100%% VCS COMPLIANT) <<<");
            $display("SUMMARY: Verified IEEE 1800-2023 §20.6 $typename queries on primitives, enums, structs, and classes.");
        end else begin
            $display("RESULT : >>> FAIL <<< (passes=%0d exp 3, errors=%0d)", pass_count, error_count);
        end
        $display("================================================================================\n");

        $finish;
    end
endmodule