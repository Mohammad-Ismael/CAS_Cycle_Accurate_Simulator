`timescale 1ns/1ps

// ============================================================================
// CAS Milestone Regression Testbench
// Target Release : v3.42.0.0
// Standard Target: IEEE Std 1800-2023 §6.18 (Typedef Forward Class Declarations)
// Testbench Name : tb_v3_42_0_0_regression
// ============================================================================

module tb_v3_42_0_0_regression;
    import v3_42_0_0_pkg::*;

    int pass_count  = 0;
    int error_count = 0;

    // Scope-level forward typedef inside module (§6.18)
    typedef class local_agent;

    class local_manager;
        local_agent agent;
        int mgr_id;

        function new(int id);
            this.mgr_id = id;
        endfunction

        function void set_agent(local_agent a);
            this.agent = a;
        endfunction
    endclass

    class local_agent;
        int agent_id;
        function new(int id);
            this.agent_id = id;
        endfunction
    endclass

    initial begin
        producer_node prod;
        consumer_node cons;
        local_manager mgr;
        local_agent   agt;
        int token_res;

        #10;
        // Construct mutually referential instances
        prod = new(32'h0000_0005);
        cons = new(32'h0000_0010);

        // Bind cross-references
        prod.bind_consumer(cons);
        cons.bind_producer(prod);

        // --------------------------------------------------------------------
        // Test 1: Cross-node token emission via forward-declared handle
        // raw = 100 (0x64), node_id = 5 -> val + id = 105 (0x69)
        // process_token: (105 ^ 0x5A5A0000) + 16 (0x10) = 0x5A5A0069 + 0x10 = 0x5A5A0079
        // --------------------------------------------------------------------
        token_res = prod.emit_token(32'h0000_0064);
        if (token_res === 32'h5A5A_0079) begin
            $display("[PASS T=%0t] Forward typedef mutual handshake verified: token_res=%08h",
                     $time, token_res);
            pass_count++;
        end else begin
            $display("[FAIL T=%0t] Forward typedef mutual handshake failed! got %08h (exp 5A5A0079)",
                     $time, token_res);
            error_count++;
        end

        // --------------------------------------------------------------------
        // Test 2: In-module forward typedef class instantiation & binding
        // --------------------------------------------------------------------
        mgr = new(1);
        agt = new(99);
        mgr.set_agent(agt);

        if (mgr.agent != null && mgr.agent.agent_id === 99) begin
            $display("[PASS T=%0t] In-module forward typedef class verified: agent_id=%0d",
                     $time, mgr.agent.agent_id);
            pass_count++;
        end else begin
            $display("[FAIL T=%0t] In-module forward typedef class failed!", $time);
            error_count++;
        end

        // --------------------------------------------------------------------
        // Test 3: Unbound forward-declared handle safety check
        // --------------------------------------------------------------------
        begin
            producer_node orphan_prod;
            orphan_prod = new(1);
            if (orphan_prod.emit_token(50) === -1) begin
                $display("[PASS T=%0t] Unbound forward handle null check verified", $time);
                pass_count++;
            end else begin
                $display("[FAIL T=%0t] Unbound forward handle null check failed!", $time);
                error_count++;
            end
        end

        #10;
        $display("\n================================================================================");
        $display("[CAS v3.42.0.0 REGRESSION SUITE: TYPEDEF FORWARD CLASS §6.18]");
        if (error_count == 0 && pass_count == 3) begin
            $display("RESULT : >>> PASS (100%% VCS COMPLIANT) <<<");
            $display("SUMMARY: Verified IEEE 1800-2023 §6.18 typedef class forward declarations across packages and modules.");
        end else begin
            $display("RESULT : >>> FAIL <<< (passes=%0d exp 3, errors=%0d)", pass_count, error_count);
        end
        $display("================================================================================\n");

        $finish;
    end
endmodule