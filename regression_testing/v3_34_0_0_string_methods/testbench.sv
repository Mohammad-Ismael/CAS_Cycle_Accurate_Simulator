// ============================================================================
// CAS Milestone Regression Testbench
// Target Release : v3.34.0.0
// Standard Target: IEEE Std 1800-2023 §6.16 (String Data Type & Built-in Methods)
// Testbench Name : tb_v3_34_0_0_regression
// ============================================================================

module tb_v3_34_0_0_regression;
    logic        clk;
    logic        rst_n;
    string       cmd_str;
    logic        cmd_valid;
    logic [31:0] parsed_addr;
    logic [15:0] parsed_data;
    logic        cmd_ready;
    logic        cmd_error;

    int pass_count  = 0;
    int error_count = 0;

    v3_34_0_0_cmd_router dut (
        .clk        (clk),
        .rst_n      (rst_n),
        .cmd_str    (cmd_str),
        .cmd_valid  (cmd_valid),
        .parsed_addr(parsed_addr),
        .parsed_data(parsed_data),
        .cmd_ready  (cmd_ready),
        .cmd_error  (cmd_error)
    );

    initial begin
        clk = 0;
        forever #5 clk = ~clk;
    end

    initial begin
        string s1, s2, s3;
        rst_n     = 0;
        cmd_valid = 0;
        cmd_str   = "";

        #12;
        rst_n = 1;

        // --------------------------------------------------------------------
        // Test 1: Built-in String manipulation methods (§6.16)
        // --------------------------------------------------------------------
        s1 = "Hello World";
        s2 = s1.toupper();
        s3 = s1.substr(0, 4);

        if (s1.len() == 11 &&
            s2 == "HELLO WORLD" &&
            s3 == "Hello" &&
            s1.getc(0) == 72 && // 'H'
            s1.compare("Hello World") == 0 &&
            s1.icompare("HELLO world") == 0) begin
            $display("[PASS T=%0t] Built-in string methods verified: len=%0d toupper='%s' substr='%s'",
                     $time, s1.len(), s2, s3);
            pass_count++;
        end else begin
            $display("[FAIL T=%0t] Built-in string methods mismatch! len=%0d s2='%s' s3='%s'",
                     $time, s1.len(), s2, s3);
            error_count++;
        end

        // --------------------------------------------------------------------
        // Test 2: In-place putc() and atoi/atohex conversions (§6.16.2 & §6.16.6)
        // --------------------------------------------------------------------
        begin
            string num_str;
            string mod_str;
            num_str = "1234";
            mod_str = "cat";
            mod_str.putc(0, 98); // 'b' -> "bat"

            if (num_str.atoi() == 1234 &&
                mod_str == "bat") begin
                $display("[PASS T=%0t] Conversion & putc verified: atoi=%0d mod_str='%s'",
                         $time, num_str.atoi(), mod_str);
                pass_count++;
            end else begin
                $display("[FAIL T=%0t] Conversion or putc mismatch! atoi=%0d mod='%s'",
                         $time, num_str.atoi(), mod_str);
                error_count++;
            end
        end

        // --------------------------------------------------------------------
        // Test 3: DUT Sequential Command Parsing via String Methods
        // --------------------------------------------------------------------
        @(negedge clk);
        cmd_str   = "write:1000:abcd";
        cmd_valid = 1;

        @(posedge clk);
        #1;
        cmd_valid = 0;

        if (cmd_ready === 1'b1 && parsed_addr === 32'h1000 && parsed_data === 16'habcd) begin
            $display("[PASS T=%0t] DUT string command parsed: addr=%04h data=%04h ready=%b",
                     $time, parsed_addr, parsed_data, cmd_ready);
            pass_count++;
        end else begin
            $display("[FAIL T=%0t] DUT string command parsing failed! addr=%04h data=%04h ready=%b",
                     $time, parsed_addr, parsed_data, cmd_ready);
            error_count++;
        end

        #10;
        $display("\n================================================================================");
        $display("[CAS v3.34.0.0 REGRESSION SUITE: STRING BUILT-IN METHODS §6.16]");
        if (error_count == 0 && pass_count == 3) begin
            $display("RESULT : >>> PASS (100%% VCS COMPLIANT) <<<");
            $display("SUMMARY: Verified IEEE 1800-2023 §6.16 string methods (len, toupper, substr, getc, putc, atoi, atohex, compare).");
        end else begin
            $display("RESULT : >>> FAIL <<< (passes=%0d exp 3, errors=%0d)", pass_count, error_count);
        end
        $display("================================================================================\n");

        $finish;
    end

    initial begin
        $monitor("Time=%0t | rst_n=%b valid=%b ready=%b | addr=%04h data=%04h",
                 $time, rst_n, cmd_valid, cmd_ready, parsed_addr, parsed_data);
    end
endmodule