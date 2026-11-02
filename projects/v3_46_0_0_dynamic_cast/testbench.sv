`timescale 1ns/1ps

// ============================================================================
// CAS Milestone Regression Testbench
// Target Release : v3.46.0.0
// Standard Target: IEEE Std 1800-2023 §6.24.2 (Dynamic Casting $cast)
// Testbench Name : tb_v3_46_0_regression
// ============================================================================

module tb_v3_46_0_regression;
    import v3_46_0_0_pkg::*;

    int pass_count  = 0;
    int error_count = 0;

    initial begin
        base_packet      base_ref;
        telemetry_packet telem_inst;
        telemetry_packet telem_cast;
        control_packet   ctrl_cast;
        cmd_e            cmd_val;
        int              status;

        #10;
        telem_inst = new(101, 32'hABCD_0001);
        base_ref   = telem_inst; // Upcast: always valid

        // --------------------------------------------------------------------
        // Test 1: Successful Class Downcast via $cast as a Function
        // base_ref points to telemetry_packet -> $cast to telemetry_packet succeeds
        // --------------------------------------------------------------------
        status = $cast(telem_cast, base_ref);
        if (status == 1 && telem_cast != null && telem_cast.sensor_data === 32'hABCD_0001) begin
            $display("[PASS T=%0t] Successful class downcast verified: id=%0d data=%08h status=%0d",
                     $time, telem_cast.packet_id, telem_cast.sensor_data, status);
            pass_count++;
        end else begin
            $display("[FAIL T=%0t] Class downcast failed! status=%0d", $time, status);
            error_count++;
        end

        // --------------------------------------------------------------------
        // Test 2: Incompatible Class Downcast Detection via Function $cast
        // base_ref points to telemetry_packet -> casting to control_packet must fail
        // --------------------------------------------------------------------
        ctrl_cast = null;
        status = $cast(ctrl_cast, base_ref);
        if (status == 0 && ctrl_cast == null) begin
            $display("[PASS T=%0t] Incompatible class downcast safely rejected: status=%0d ctrl_cast is null",
                     $time, status);
            pass_count++;
        end else begin
            $display("[FAIL T=%0t] Incompatible class downcast was not rejected! status=%0d", $time, status);
            error_count++;
        end

        // --------------------------------------------------------------------
        // Test 3: Enum Dynamic Casting Validation & Out-of-Bounds Rejection
        // 2'b01 is CMD_WRITE -> succeeds (status = 1)
        // 2'b11 is undefined in cmd_e -> fails (status = 0)
        // --------------------------------------------------------------------
        cmd_val = CMD_IDLE;
        status = $cast(cmd_val, 2'b01);
        if (status == 1 && cmd_val == CMD_WRITE) begin
            int fail_status;
            fail_status = $cast(cmd_val, 2'b11);
            if (fail_status == 0 && cmd_val == CMD_WRITE) begin
                $display("[PASS T=%0t] Enum dynamic cast validation verified: valid status=1, invalid status=0 cmd unchanged",
                         $time);
                pass_count++;
            end else begin
                $display("[FAIL T=%0t] Enum dynamic cast failed invalid rejection! fail_status=%0d", $time, fail_status);
                error_count++;
            end
        end else begin
            $display("[FAIL T=%0t] Enum dynamic cast failed valid cast! status=%0d", $time, status);
            error_count++;
        end

        #10;
        $display("\n================================================================================");
        $display("[CAS v3.46.0.0 REGRESSION SUITE: DYNAMIC CASTING $cast §6.24.2]");
        if (error_count == 0 && pass_count == 3) begin
            $display("RESULT : >>> PASS (100%% VCS COMPLIANT) <<<");
            $display("SUMMARY: Verified IEEE 1800-2023 §6.24.2 $cast function downcasting, mismatch rejection, and enum boundary checks.");
        end else begin
            $display("RESULT : >>> FAIL <<< (passes=%0d exp 3, errors=%0d)", pass_count, error_count);
        end
        $display("================================================================================\n");

        $finish;
    end
endmodule