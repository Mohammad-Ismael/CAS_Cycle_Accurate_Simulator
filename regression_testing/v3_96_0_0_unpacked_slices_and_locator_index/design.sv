// IEEE 1800-2023 §7.4.3 & §7.12.1 Unpacked Array Slices, Equality, and Locator item.index
module unpacked_slices_locator_dut (
    input  logic        clk,
    input  logic        rst_n,
    output logic [31:0] err_count
);

    int arr_a[7:0];
    int arr_b[7:0];
    int arr_c[7:0];
    int dyn[];
    int q[$];

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            err_count <= 0;
        end else begin
            int local_err;
            local_err = 0;

            // 1. Initialize arr_a with known patterns
            for (int i = 0; i < 8; i++) begin
                arr_a[i] = (i + 1) * 10;
                arr_b[i] = 0;
                arr_c[i] = 0;
            end

            // 2. Unpacked 1D array constant slice assignment: arr_b[5:3] = arr_a[2:0]
            // In [7:0] indexing: arr_a[2:0] = {arr_a[2], arr_a[1], arr_a[0]} = {30, 20, 10}
            // arr_b[5:3] receives: arr_b[5]=30, arr_b[4]=20, arr_b[3]=10
            arr_b[5:3] = arr_a[2:0];
            if (arr_b[5] !== 30 || arr_b[4] !== 20 || arr_b[3] !== 10) begin
                $display("[ERROR] Constant slice assign mismatch: arr_b[5:3] = {%0d, %0d, %0d}", arr_b[5], arr_b[4], arr_b[3]);
                local_err++;
            end

            // 3. Unpacked indexed part-select slice assignment: arr_c[4+:3] = arr_a[1+:3]
            // arr_a[1+:3] is arr_a[1..3] -> {20, 30, 40}
            // arr_c[4+:3] is arr_c[4..6]
            arr_c[4+:3] = arr_a[1+:3];
            if (arr_c[4] !== 20 || arr_c[5] !== 30 || arr_c[6] !== 40) begin
                $display("[ERROR] Indexed part-select slice mismatch: arr_c[4..6] = {%0d, %0d, %0d}", arr_c[4], arr_c[5], arr_c[6]);
                local_err++;
            end

            // 4. Unpacked array slice equality & inequality
            // arr_a[7:4] = {80, 70, 60, 50} (descending slice)
            arr_b[3] = 80;
            arr_b[2] = 70;
            arr_b[1] = 60;
            arr_b[0] = 50;
            if (!(arr_a[7:4] == arr_b[3:0])) begin
                $display("[ERROR] Slice equality expected true but got false");
                local_err++;
            end
            if (arr_a[7:4] != arr_b[3:0]) begin
                $display("[ERROR] Slice inequality expected false but got true");
                local_err++;
            end

            // 5. Dynamic array locator method find with (item == item.index) returning into queue
            dyn = new[4];
            dyn[0] = 0;
            dyn[1] = 1;
            dyn[2] = 3;
            dyn[3] = 3;

            q = dyn.find with (item == item.index);
            // Matches at index 0 (0 == 0), index 1 (1 == 1), index 3 (3 == 3) -> 3 elements
            if (q.size() != 3) begin
                $display("[ERROR] Locator find size mismatch: expected 3, got %0d", q.size());
                local_err++;
            end else if (q[0] !== 0 || q[1] !== 1 || q[2] !== 3) begin
                $display("[ERROR] Locator find values mismatch: {%0d, %0d, %0d}", q[0], q[1], q[2]);
                local_err++;
            end

            err_count <= local_err;
        end
    end

endmodule
