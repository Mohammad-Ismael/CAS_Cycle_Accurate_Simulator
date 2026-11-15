`timescale 1ns/1ps

module tb_v4_2_0_regression;

    logic        clk = 0;
    logic [31:0] pkg_add_out;
    logic [31:0] itor_out;
    logic [63:0] real_bits_out;
    logic [31:0] shortreal_bits_out;
    logic        unb_out;
    logic        isunk_out;
    logic [31:0] math_res_out;

    int errors = 0;

    sys_tasks_file_io_and_packages_dut dut (
        .clk(clk),
        .pkg_add_out(pkg_add_out),
        .itor_out(itor_out),
        .real_bits_out(real_bits_out),
        .shortreal_bits_out(shortreal_bits_out),
        .unb_out(unb_out),
        .isunk_out(isunk_out),
        .math_res_out(math_res_out)
    );

    int mon_val = 100;

    initial begin
        #1;

        // 1. Verify Package sticky port function result
        if (pkg_add_out !== 42) begin
            $display("ERROR: pkg_add_out mismatch: expected 42, got %0d", pkg_add_out);
            errors++;
        end

        // 2. Verify $isunbounded
        if (unb_out !== 1'b1) begin
            $display("ERROR: unb_out mismatch: expected 1, got %0b", unb_out);
            errors++;
        end

        // 3. Verify $itor and $isunknown
        if (itor_out !== 20) begin
            $display("ERROR: itor_out mismatch: expected 20, got %0d", itor_out);
            errors++;
        end

        if (isunk_out !== 1'b1) begin
            $display("ERROR: isunk_out mismatch: expected 1, got %0b", isunk_out);
            errors++;
        end

        // 4. Verify math functions sum (4+2+3+0+2 = 11)
        if (math_res_out !== 11) begin
            $display("ERROR: math_res_out mismatch: expected 11, got %0d", math_res_out);
            errors++;
        end

        // 5. Verify real bits round-trip
        if ($bitstoreal(real_bits_out) != 12.45) begin
            $display("ERROR: real_bits roundtrip mismatch");
            errors++;
        end

`ifndef VERILATOR
        // 6. Verify shortreal bits round-trip
        begin
            shortreal recov_sr = $bitstoshortreal(shortreal_bits_out);
            if (recov_sr < 12.44 || recov_sr > 12.46) begin
                $display("ERROR: shortreal roundtrip mismatch");
                errors++;
            end
        end
`endif

        // 7. Formatless $display and monitor controls
        $monitoron;
        $monitor(mon_val);
        $monitorb(mon_val);
        $monitoro(mon_val);
        $monitorh(mon_val);
        $monitoroff;

        begin
            int disp_val = 1234;
            $display(disp_val);
        end

        // 8. File I/O system tasks & functions
        begin
            int fd;
            string err_str;
            integer errno_val;
            int eof_val;
            longint pos0, pos12, pos_rew;
            int unget_res, fget_res;

            fd = $fopen("tb_tmp.txt", "w");
            if (fd == 0) begin
                $display("ERROR: $fopen failed");
                errors++;
            end

            pos0 = $ftell(fd);
            if (pos0 !== 0) begin
                $display("ERROR: initial ftell mismatch: %0d", pos0);
                errors++;
            end

            $fseek(fd, 12, 0);
            pos12 = $ftell(fd);
            if (pos12 !== 12) begin
                $display("ERROR: seek 12 ftell mismatch: %0d", pos12);
                errors++;
            end

            $rewind(fd);
            pos_rew = $ftell(fd);
            if (pos_rew !== 0) begin
                $display("ERROR: rewind ftell mismatch: %0d", pos_rew);
                errors++;
            end

            eof_val = $feof(fd);
            if (eof_val !== 0) begin
                $display("ERROR: feof mismatch: %0d", eof_val);
                errors++;
            end

            errno_val = $ferror(fd, err_str);
            if (errno_val !== 0) begin
                $display("ERROR: ferror mismatch: %0d", errno_val);
                errors++;
            end

            $fwrite(fd, "z");
            $fclose(fd);

            // Reopen for reading: verify $ungetc and $fgetc
            fd = $fopen("tb_tmp.txt", "r");
            unget_res = $ungetc(123, fd);
            if (unget_res !== 0) begin
                $display("ERROR: ungetc mismatch: expected 0, got %0d", unget_res);
                errors++;
            end

            fget_res = $fgetc(fd);
            if (fget_res !== 123) begin
                $display("ERROR: fgetc mismatch: expected 123, got %0d", fget_res);
                errors++;
            end
            $fclose(fd);
        end

        // 9. $sscanf task / function
        begin
            string scan_str = "9876";
            int parsed_val = 0;
            $sscanf(scan_str, "%d", parsed_val);
            if (parsed_val !== 9876) begin
                $display("ERROR: $sscanf parsed_val mismatch: expected 9876, got %0d", parsed_val);
                errors++;
            end
        end

        // Final Assessment
        if (errors == 0) begin
            $display("RESULT : >>> PASS (100%% VCS COMPLIANT) <<<");
        end else begin
            $display("RESULT : >>> FAIL (%0d ERRORS) <<<", errors);
        end

        $finish;
    end

endmodule
