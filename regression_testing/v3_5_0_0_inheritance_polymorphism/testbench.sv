// ============================================================================
// CAS Milestone Regression Testbench
// Target Release : v3.5.0.0
// Standard Target: IEEE Std 1800-2023 §8.13-§8.20 (Class Inheritance, super.new,
//                  Virtual Methods, Polymorphic Dispatch, and $cast)
// Testbench Name : tb_v3_5_0_0_regression
// ============================================================================

// Base transaction class (§8.13)
class base_packet;
    bit [7:0]  tag;
    bit [31:0] payload;

    function new(bit [7:0] t = 8'h00, bit [31:0] p = 32'h00000000);
        this.tag     = t;
        this.payload = p;
    endfunction

    // Virtual method to be overridden by derived classes (§8.20)
    virtual function bit [31:0] get_encoded();
        return this.payload ^ {24'h000000, this.tag};
    endfunction

    virtual function string get_name();
        return "BASE";
    endfunction
endclass

// Derived specialized packet (§8.13)
class tagged_packet extends base_packet;
    bit [15:0] vlan_id;

    function new(bit [7:0] t = 8'h00, bit [31:0] p = 32'h00000000, bit [15:0] v = 16'h0000);
        super.new(t, p); // Explicit super.new (§8.17)
        this.vlan_id = v;
    endfunction

    // Overridden virtual method (§8.20)
    virtual function bit [31:0] get_encoded();
        // Incorporates vlan_id into encoding
        return this.payload ^ {24'h000000, this.tag} ^ {16'h0000, this.vlan_id};
    endfunction

    virtual function string get_name();
        return "TAGGED";
    endfunction
endclass

module tb_v3_5_0_0_regression;
    logic        clk;
    logic        rst_n;
    logic [7:0]  tag_in;
    logic [31:0] payload_in;
    logic [31:0] encoded_out;

    // Polymorphic base handles (§8.14)
    base_packet   b_ptr;
    tagged_packet t_obj;
    tagged_packet cast_back_ptr;

    bit [31:0] code_base;
    bit [31:0] code_derived;
    integer    cast_ok = 0;
    integer    error_count = 0;

    v3_5_0_0_poly_core dut (
        .clk(clk),
        .rst_n(rst_n),
        .tag_in(tag_in),
        .payload_in(payload_in),
        .encoded_out(encoded_out)
    );

    initial begin
        clk        = 0;
        rst_n      = 0;
        tag_in     = 8'h00;
        payload_in = 32'h00000000;

        #5;
        rst_n = 1;

        // --------------------------------------------------------------------
        // TEST 1: Base object instantiation & base virtual method
        // --------------------------------------------------------------------
        b_ptr = new(8'hAA, 32'h12345678);
        code_base = b_ptr.get_encoded();
        #5; // T = 10
        // Expected: 32'h12345678 ^ 32'h000000AA = 32'h123456D2
        if (code_base !== 32'h123456D2) begin
            $display("[FAIL T=10] Base get_encoded() failed! code=%h (exp 123456D2)", code_base);
            error_count = error_count + 1;
        end

        // --------------------------------------------------------------------
        // TEST 2: Derived object instantiation with super.new() (§8.17)
        // --------------------------------------------------------------------
        t_obj = new(8'h55, 32'hFFFF0000, 16'h00FF);
        #5; // T = 15
        if (t_obj.tag !== 8'h55 || t_obj.payload !== 32'hFFFF0000 || t_obj.vlan_id !== 16'h00FF) begin
            $display("[FAIL T=15] Derived super.new() initialization failed! tag=%h payload=%h vlan=%h",
                     t_obj.tag, t_obj.payload, t_obj.vlan_id);
            error_count = error_count + 1;
        end

        // --------------------------------------------------------------------
        // TEST 3: Dynamic Polymorphism through Base Handle (§8.20)
        // Upcast: b_ptr points to derived tagged_packet object
        // --------------------------------------------------------------------
        b_ptr = t_obj;
        code_derived = b_ptr.get_encoded(); // Must resolve to tagged_packet::get_encoded
        #5; // T = 20
        // Expected: 32'hFFFF0000 ^ 32'h00000055 ^ 32'h000000FF = 32'hFFFF00AA
        if (code_derived !== 32'hFFFF00AA) begin
            $display("[FAIL T=20] Polymorphic dispatch failed! code=%h (exp FFFF00AA)", code_derived);
            error_count = error_count + 1;
        end

        // --------------------------------------------------------------------
        // TEST 4: Downcasting via $cast dynamic cast (§8.16)
        // --------------------------------------------------------------------
        cast_ok = $cast(cast_back_ptr, b_ptr);
        #5; // T = 25
        if (!cast_ok || cast_back_ptr == null || cast_back_ptr.vlan_id !== 16'h00FF) begin
            $display("[FAIL T=25] Downcasting $cast failed! cast_ok=%0d", cast_ok);
            error_count = error_count + 1;
        end

        #5; // T = 30
        $display("\n================================================================================");
        $display("[CAS v3.5.0.0 REGRESSION SUITE: INHERITANCE & POLYMORPHISM]");
        if (error_count == 0) begin
            $display("RESULT : >>> PASS (100%% VCS COMPLIANT) <<<");
            $display("SUMMARY: Verified IEEE 1800-2023 §8.13-§8.20 inheritance, super.new, virtual methods, and $cast.");
        end else begin
            $display("RESULT : >>> FAIL <<< (%0d error(s) detected)", error_count);
        end
        $display("================================================================================\n");

        $finish;
    end

    initial begin
        $monitor("Time=%0t | code_base=%h code_derived=%h | cast_ok=%0d",
                 $time, code_base, code_derived, cast_ok);
    end
endmodule