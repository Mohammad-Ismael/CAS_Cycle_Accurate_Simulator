// IEEE 1800-2023 §7.12.3 & §7.12.2 Array Reduction and Ordering Methods
module array_reduction_ordering_dut (
    input  logic        clk,
    input  logic        rst_n,
    output logic [31:0] err_count
);

    byte b_and[] = '{1, 3, 5, 7};
    byte b_ops[] = '{1, 2, 3, 4};
    int  ia[]    = '{4, 5, 3, 1};
    string sa[]  = '{"hello", "sad", "world"};

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            err_count <= 0;
        end else begin
            int local_err;
            byte r_and, r_or, r_xor, r_sum, r_prod;
            local_err = 0;

            // 1. Array reduction methods (parenthesis-free and with parentheses)
            r_and  = b_and.and;
            r_or   = b_ops.or;
            r_xor  = b_ops.xor;
            r_sum  = b_ops.sum();
            r_prod = b_ops.product();

            if (r_and !== 1) begin
                $display("[ERROR] b_and.and mismatch: expected 1, got %0d", r_and);
                local_err++;
            end
            if (r_or !== 7) begin
                $display("[ERROR] b_ops.or mismatch: expected 7, got %0d", r_or);
                local_err++;
            end
            if (r_xor !== 4) begin
                $display("[ERROR] b_ops.xor mismatch: expected 4, got %0d", r_xor);
                local_err++;
            end
            if (r_sum !== 10) begin
                $display("[ERROR] b_ops.sum() mismatch: expected 10, got %0d", r_sum);
                local_err++;
            end
            if (r_prod !== 24) begin
                $display("[ERROR] b_ops.product() mismatch: expected 24, got %0d", r_prod);
                local_err++;
            end

            // 2. Ordering methods on integer dynamic array
            ia.sort;
            if (ia[0] !== 1 || ia[1] !== 3 || ia[2] !== 4 || ia[3] !== 5) begin
                $display("[ERROR] ia.sort mismatch: {%0d, %0d, %0d, %0d}", ia[0], ia[1], ia[2], ia[3]);
                local_err++;
            end

            ia.rsort;
            if (ia[0] !== 5 || ia[1] !== 4 || ia[2] !== 3 || ia[3] !== 1) begin
                $display("[ERROR] ia.rsort mismatch: {%0d, %0d, %0d, %0d}", ia[0], ia[1], ia[2], ia[3]);
                local_err++;
            end

            ia.reverse;
            if (ia[0] !== 1 || ia[1] !== 3 || ia[2] !== 4 || ia[3] !== 5) begin
                $display("[ERROR] ia.reverse mismatch: {%0d, %0d, %0d, %0d}", ia[0], ia[1], ia[2], ia[3]);
                local_err++;
            end

            // 3. Ordering methods on string dynamic array
            sa.reverse;
            if (sa[0] != "world" || sa[1] != "sad" || sa[2] != "hello") begin
                $display("[ERROR] sa.reverse mismatch: {'%s', '%s', '%s'}", sa[0], sa[1], sa[2]);
                local_err++;
            end

            sa.sort;
            if (sa[0] != "hello" || sa[1] != "sad" || sa[2] != "world") begin
                $display("[ERROR] sa.sort mismatch: {'%s', '%s', '%s'}", sa[0], sa[1], sa[2]);
                local_err++;
            end

            err_count <= local_err;
        end
    end

endmodule
