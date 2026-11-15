// IEEE 1800-2023 §7.9 & §7.12.1 Associative Array Traversal, Locators, and Defaults
module assoc_traversal_locators_dut (
    input  logic        clk,
    input  logic        rst_n,
    output logic [31:0] err_count
);

    // 1. IEEE 1800 §7.9.4–§7.9.7 Traversal on string-indexed & byte-indexed arrays
    int smap[string];
    string bmap[byte];

    // 2. IEEE 1800 §7.12.1 Array locator methods
    int s[] = '{1, 2, 2, 4, 3};
    int qi[$];

    // 3. IEEE 1800 §7.9.11 Defaults & §13.4 Pass-by-value task port
    string words[int] = '{default: "hello"};
    string arraya[int];

    task automatic check_assoc(string arrayb[int], output int pass_flag);
        arrayb[1] = "modified";
        pass_flag = (arrayb[0] == "orig_0" && arrayb[1] == "modified" && arrayb[2] == "orig_2") ? 1 : 0;
    endtask

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            err_count <= 0;
        end else begin
            int local_err;
            string str_key;
            byte byte_key;
            int rc;
            int task_pass;
            local_err = 0;

            // --- 1. Associative Traversal Methods ---
            smap.delete();
            // Traversal on empty array returns 0
            rc = smap.first(str_key);
            if (rc != 0) begin
                $display("[ERROR] smap.first() on empty array returned %0d", rc);
                local_err++;
            end
            rc = smap.last(str_key);
            if (rc != 0) begin
                $display("[ERROR] smap.last() on empty array returned %0d", rc);
                local_err++;
            end

            // Populate smap
            smap["apple"] = 10;
            smap["banana"] = 20;
            smap["cherry"] = 30;

            // first()
            rc = smap.first(str_key);
            if (rc != 1 || str_key != "apple") begin
                $display("[ERROR] smap.first() failed: rc=%0d, key=%s", rc, str_key);
                local_err++;
            end

            // next()
            rc = smap.next(str_key);
            if (rc != 1 || str_key != "banana") begin
                $display("[ERROR] smap.next() failed: rc=%0d, key=%s", rc, str_key);
                local_err++;
            end

            // next() to end
            rc = smap.next(str_key);
            if (rc != 1 || str_key != "cherry") begin
                $display("[ERROR] smap.next() to cherry failed: rc=%0d, key=%s", rc, str_key);
                local_err++;
            end

            // next() past end returns 0
            rc = smap.next(str_key);
            if (rc != 0) begin
                $display("[ERROR] smap.next() past end returned %0d", rc);
                local_err++;
            end

            // last()
            rc = smap.last(str_key);
            if (rc != 1 || str_key != "cherry") begin
                $display("[ERROR] smap.last() failed: rc=%0d, key=%s", rc, str_key);
                local_err++;
            end

            // prev()
            rc = smap.prev(str_key);
            if (rc != 1 || str_key != "banana") begin
                $display("[ERROR] smap.prev() failed: rc=%0d, key=%s", rc, str_key);
                local_err++;
            end

            // prev() to start
            rc = smap.prev(str_key);
            if (rc != 1 || str_key != "apple") begin
                $display("[ERROR] smap.prev() to apple failed: rc=%0d, key=%s", rc, str_key);
                local_err++;
            end

            // prev() before start returns 0
            rc = smap.prev(str_key);
            if (rc != 0) begin
                $display("[ERROR] smap.prev() before start returned %0d", rc);
                local_err++;
            end

            // Traversal on byte-indexed array
            bmap.delete();
            bmap[8'd1] = "one";
            bmap[8'd5] = "five";
            bmap[8'd10] = "ten";

            rc = bmap.first(byte_key);
            if (rc != 1 || byte_key != 8'd1) begin
                $display("[ERROR] bmap.first() failed: rc=%0d, key=%0d", rc, byte_key);
                local_err++;
            end

            rc = bmap.last(byte_key);
            if (rc != 1 || byte_key != 8'd10) begin
                $display("[ERROR] bmap.last() failed: rc=%0d, key=%0d", rc, byte_key);
                local_err++;
            end

            rc = bmap.prev(byte_key);
            if (rc != 1 || byte_key != 8'd5) begin
                $display("[ERROR] bmap.prev() failed: rc=%0d, key=%0d", rc, byte_key);
                local_err++;
            end

            // --- 2. IEEE 1800 §7.12.1 Array Locator Methods ---
            // min (parenthesis-free)
            qi = s.min;
            if (qi.size() != 1 || qi[0] != 1) begin
                $display("[ERROR] s.min failed: size=%0d, val=%0d", qi.size(), (qi.size() > 0 ? qi[0] : -1));
                local_err++;
            end

            // max (parenthesized)
            qi = s.max();
            if (qi.size() != 1 || qi[0] != 4) begin
                $display("[ERROR] s.max() failed: size=%0d, val=%0d", qi.size(), (qi.size() > 0 ? qi[0] : -1));
                local_err++;
            end

            // unique
            qi = s.unique;
            if (qi.size() != 4 || qi[0] != 1 || qi[1] != 2 || qi[2] != 4 || qi[3] != 3) begin
                $display("[ERROR] s.unique failed: size=%0d", qi.size());
                local_err++;
            end

            // unique_index
            qi = s.unique_index();
            if (qi.size() != 4 || qi[0] != 0 || qi[1] != 1 || qi[2] != 3 || qi[3] != 4) begin
                $display("[ERROR] s.unique_index() failed: size=%0d", qi.size());
                local_err++;
            end

            // --- 3. Associative Default Value & Subroutine Passing ---
            // Unset key returns default value
            words.delete();
            if (words[123] != "hello") begin
                $display("[ERROR] words[123] default lookup failed: got %s", words[123]);
                local_err++;
            end
            words[123] = "custom";
            if (words[123] != "custom" || words[456] != "hello") begin
                $display("[ERROR] words default vs explicit failed");
                local_err++;
            end

            // Task pass-by-value isolation
            arraya[0] = "orig_0";
            arraya[1] = "orig_1";
            arraya[2] = "orig_2";
            check_assoc(arraya, task_pass);
            if (task_pass != 1 || arraya[1] != "orig_1") begin
                $display("[ERROR] check_assoc pass-by-value failed: task_pass=%0d, arraya[1]=%s", task_pass, arraya[1]);
                local_err++;
            end

            err_count <= local_err;
        end
    end

endmodule
