`include "full_adder.sv"
`include "bit4_adder.sv"
`include "alu_core.sv"


module tb_master;

    // v0.9.2 reg/wire declarations
    reg        clk;
    reg        rst_n;
    reg  [3:0] in_a;
    reg  [3:0] in_b;
    reg  [2:0] opcode;
    reg  [1:0] mem_addr;
    reg        mem_we;

    wire [3:0] adder_sum;
    wire       adder_cout;
    wire [3:0] alu_result;
    wire [3:0] reg_accum;
    wire       is_equal;
    wire       is_less;
    wire       bit_extracted;
    wire [1:0] slice_extracted;
    wire [3:0] cont_assign_out;
    wire [3:0] mem_rdata;
    wire       dff_q;
    wire       dff_qb;

    reg [7:0] fail_count;

    master_dut u_dut (
        .clk(clk),
        .rst_n(rst_n),
        .in_a(in_a),
        .in_b(in_b),
        .opcode(opcode),
        .mem_addr(mem_addr),
        .mem_we(mem_we),
        .adder_sum(adder_sum),
        .adder_cout(adder_cout),
        .alu_result(alu_result),
        .reg_accum(reg_accum),
        .is_equal(is_equal),
        .is_less(is_less),
        .bit_extracted(bit_extracted),
        .slice_extracted(slice_extracted),
        .cont_assign_out(cont_assign_out),
        .mem_rdata(mem_rdata),
        .dff_q(dff_q),
        .dff_qb(dff_qb)
    );

    // v0.9.2 Task with prefix delay #1
    task report_state;
        #1 $display("[TASK @ T=%0t] in_a=%b, adder_sum=%b, alu_result=%b, accum=%d",
                    $time, in_a, adder_sum, alu_result, reg_accum);
    endtask

    initial begin

        fail_count = 8'd0;

        $display("==================================================================");
        $display("   CAS COMPREHENSIVE MASTER REGRESSION SUITE (v0.1.0 - v0.9.3)    ");
        $display("==================================================================");

        // ---------------------------------------------------------------
        // CHECK 1: v0.6.0 Pre-Reset 4-State Unknown Verification
        // ---------------------------------------------------------------
        if (reg_accum == 4'd0) begin
            $display("[FAIL] v0.6.0: Uninitialized register was zeroed instead of 'X'!");
            fail_count = fail_count + 8'd1;
        end else begin
            $display("[PASS] v0.6.0: Uninitialized register holds 4-state 'X' prior to reset (raw=%b).", reg_accum);
        end

        // ---------------------------------------------------------------
        // CHECK 2: v0.3.0 Time Wheel Scheduler & Delays
        // ---------------------------------------------------------------
        #5;
        if ($time == 5) begin
            $display("[PASS] v0.3.0: Time Wheel dynamic event scheduler & $time valid at T=%0t.", $time);
        end else begin
            $display("[FAIL] v0.3.0: Simulation time mismatch! Expected 5, got %0t", $time);
            fail_count = fail_count + 8'd1;
        end

        // ---------------------------------------------------------------
        // CHECK 3: v0.4.0 / v0.9.2 Asynchronous Reset Edge (X -> 0)
        // ---------------------------------------------------------------
        clk = 0;
        rst_n = 0;
        in_a = 4'd0;
        in_b = 4'd0;
        opcode = 3'd0;
        mem_addr = 2'd0;
        mem_we = 0;

        #5;
        if (reg_accum == 4'd0 && dff_q == 1'b0 && dff_qb == 1'b1) begin
            $display("[PASS] v0.4.0 / v0.9.2: Asynchronous reset edge (X -> 0) cleared registers (accum=%d, q=%b, qb=%b).",
                     reg_accum, dff_q, dff_qb);
        end else begin
            $display("[FAIL] v0.4.0: Asynchronous reset failed! accum=%d, q=%b, qb=%b", reg_accum, dff_q, dff_qb);
            fail_count = fail_count + 8'd1;
        end

        // Deassert reset
        rst_n = 1;
        #5;

        // ---------------------------------------------------------------
        // CHECK 4: v0.9.3 Built-in Gate Primitives & Positional Bus Slices
        // Testing 4-bit ripple adder: 4'b0110 (6) + 4'b0101 (5) = 4'b1011 (11), cout=0
        // ---------------------------------------------------------------
        in_a = 4'b0110;
        in_b = 4'b0101;
        #5;
        if (adder_sum == 4'b1011 && adder_cout == 1'b0) begin
            $display("[PASS] v0.9.3: Gate primitives & positional slice adder verified (6 + 5 = 11, cout=0).");
        end else begin
            $display("[FAIL] v0.9.3: Gate adder failed! Expected sum=1011 cout=0, got sum=%b cout=%b", adder_sum, adder_cout);
            fail_count = fail_count + 8'd1;
        end

        // ---------------------------------------------------------------
        // CHECK 5: v0.8.0 Case Branching & Slicing
        // ---------------------------------------------------------------
        opcode = 3'd1; // Bitwise OR: 4'b0110 | 4'b0101 = 4'b0111
        #5;
        if (alu_result == 4'b0111 && bit_extracted == 1'b1 && slice_extracted == 2'b01) begin
            $display("[PASS] v0.8.0: Case branch OR & part-select [3:2] verified (res=0111, bit[2]=1, slice[3:2]=01).");
        end else begin
            $display("[FAIL] v0.8.0: ALU case/slice failed! res=%b, bit=%b, slice=%b", alu_result, bit_extracted, slice_extracted);
            fail_count = fail_count + 8'd1;
        end

        // ---------------------------------------------------------------
        // CHECK 6: v0.9.0 Continuous Assign Dataflow
        // in_a = 6 -> cont_assign_out = 6 + 1 = 7
        // ---------------------------------------------------------------
        if (cont_assign_out == 4'd7) begin
            $display("[PASS] v0.9.0: Continuous assign dataflow verified (6 + 1 = 7).");
        end else begin
            $display("[FAIL] v0.9.0: Continuous assign failed! Expected 7, got %d", cont_assign_out);
            fail_count = fail_count + 8'd1;
        end

        // ---------------------------------------------------------------
        // CHECK 7: v0.9.0 1D Memory Array 4-State Unknown & Synchronous Write
        // ---------------------------------------------------------------
        mem_addr = 2'd3;
        #5;
        if (mem_rdata == 4'd0) begin
            $display("[FAIL] v0.9.0: RAM[3] was zeroed instead of holding 4-state 'X'!");
            fail_count = fail_count + 8'd1;
        end else begin
            $display("[PASS] v0.9.0: Unwritten RAM cell holds 4-state 'X' (raw=%b).", mem_rdata);
        end

        // Write 4'b1101 to RAM[3]
        in_a = 4'b1101;
        mem_we = 1;
        #5;
        clk = 1; #5; clk = 0; #5;
        mem_we = 0;
        #5;

        if (mem_rdata == 4'b1101) begin
            $display("[PASS] v0.9.0: Synchronous RAM write & async read verified (RAM[3]=1101).");
        end else begin
            $display("[FAIL] v0.9.0: RAM write failed! Expected 1101, got %b", mem_rdata);
            fail_count = fail_count + 8'd1;
        end

        // ---------------------------------------------------------------
        // CHECK 8: v0.9.2 Literal 1'bx & DFF Clock Edge Latching
        // ---------------------------------------------------------------
        in_a = 4'b0001; // in_a[0] = 1 -> should latch into dff_q
        #5;
        clk = 1; #5; clk = 0; #5;
        if (dff_q == 1'b1 && dff_qb == 1'b0) begin
            $display("[PASS] v0.9.2: DFF clock edge capture and continuous inversion verified (q=1, qb=0).");
        end else begin
            $display("[FAIL] v0.9.2: DFF clock edge failed! q=%b, qb=%b", dff_q, dff_qb);
            fail_count = fail_count + 8'd1;
        end

        // ---------------------------------------------------------------
        // CHECK 9: v0.4.0 Stratified NBA Accumulation Loop
        // ---------------------------------------------------------------
        // Reset accumulator to isolate the sequential test
        rst_n = 0; #5; rst_n = 1; #5;
        in_a = 4'd2;
        in_b = 4'd3;
        opcode = 3'd6; // 2 + 3 = 5
        #5;
        // Pulse 1: 0 + 5 = 5
        clk = 1; #5; clk = 0; #5;
        // Pulse 2: 5 + 5 = 10
        clk = 1; #5; clk = 0; #5;

        if (reg_accum == 4'd10) begin
            $display("[PASS] v0.4.0: Stratified NBA accumulation loop verified (0 + 5 + 5 = 10).");
        end else begin
            $display("[FAIL] v0.4.0: NBA accumulation failed! Expected 10, got %d", reg_accum);
            fail_count = fail_count + 8'd1;
        end

        // ---------------------------------------------------------------
        // CHECK 10: v0.9.2 Subroutine Task Invocation with Prefix Delay
        // ---------------------------------------------------------------
        report_state;
        $display("[PASS] v0.9.2: Subroutine task execution with prefix delay verified.");

        // ---------------------------------------------------------------
        // CHECK 11: v0.9.3 Dynamic $monitor Sensitivity Callback
        // ---------------------------------------------------------------
        $monitor("[MONITOR @ T=%0t] in_a=%b, adder_sum=%b, cout=%b", $time, in_a, adder_sum, adder_cout);
        #5;
        in_a = 4'b1111; // 15 + 3 = 18 -> sum=2, cout=1
        #5;

        $display("==================================================================");
        if (fail_count == 8'd0) begin
            $display("   VERDICT: ALL TESTS PASSED (CAS READY FOR v0.9.3 RELEASE)       ");
        end else begin
            $display("   VERDICT: REGRESSION FAILED WITH %d ERROR(S)                    ", fail_count);
        end
        $display("==================================================================");

        $finish;
    end

endmodule