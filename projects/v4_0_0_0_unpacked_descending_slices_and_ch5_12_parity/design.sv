`timescale 1ns/1ps

module unpacked_descending_slices_dut (
    input  logic        clk,
    input  logic        rst_n,
    output logic        test_done,
    output logic [31:0] error_count
);

    int arr_desc [3:0];
    int arr_asc  [0:3];

    int sub_asc  [0:1];
    int sub_desc [3:2];

    int dyn_arr [];
    int q [$];
    string assoc [int];

    logic [31:0] errs;
    logic        done;

    assign test_done   = done;
    assign error_count = errs;

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            errs <= 32'd0;
            done <= 1'b0;
        end else begin
            // 1. Initialize descending array [3:0]
            arr_desc[0] = 32'd10;
            arr_desc[1] = 32'd20;
            arr_desc[2] = 32'd30;
            arr_desc[3] = 32'd40;

            // 2. Whole unpacked array assignment (descending to ascending)
            // Left-to-right mapping per IEEE 1800-2023 §7.6:
            // arr_asc[0] gets arr_desc[3] (40)
            // arr_asc[1] gets arr_desc[2] (30)
            // arr_asc[2] gets arr_desc[1] (20)
            // arr_asc[3] gets arr_desc[0] (10)
            arr_asc = arr_desc;

            if (arr_asc[0] != 32'd40) begin
                $display("[FAIL] arr_asc[0] expected 40, got %0d", arr_asc[0]);
                errs <= errs + 1;
            end
            if (arr_asc[1] != 32'd30) begin
                $display("[FAIL] arr_asc[1] expected 30, got %0d", arr_asc[1]);
                errs <= errs + 1;
            end
            if (arr_asc[2] != 32'd20) begin
                $display("[FAIL] arr_asc[2] expected 20, got %0d", arr_asc[2]);
                errs <= errs + 1;
            end
            if (arr_asc[3] != 32'd10) begin
                $display("[FAIL] arr_asc[3] expected 10, got %0d", arr_asc[3]);
                errs <= errs + 1;
            end

            // 3. Sub-slice assignment: descending slice to ascending slice
            // sub_asc[0:1] receives arr_desc[3:2] -> sub_asc[0] = 40, sub_asc[1] = 30
            sub_asc[0:1] = arr_desc[3:2];
            if (sub_asc[0] != 32'd40 || sub_asc[1] != 32'd30) begin
                $display("[FAIL] sub_asc[0:1] slice assign failed: got %0d, %0d", sub_asc[0], sub_asc[1]);
                errs <= errs + 1;
            end

            // sub_desc[3:2] receives arr_asc[0:1] (40, 30) -> sub_desc[3] = 40, sub_desc[2] = 30
            sub_desc[3:2] = arr_asc[0:1];
            if (sub_desc[3] != 32'd40 || sub_desc[2] != 32'd30) begin
                $display("[FAIL] sub_desc[3:2] slice assign failed: got %0d, %0d", sub_desc[3], sub_desc[2]);
                errs <= errs + 1;
            end

            // 4. Combined Aggregate Types: Dynamic array, Queue, Associative array
            dyn_arr = new[4];
            for (int i = 0; i < 4; i++) dyn_arr[i] = arr_asc[i];

            q.delete();
            q.push_back(arr_asc[0]);
            q.push_back(arr_asc[1]);
            q.push_back(arr_asc[2]);
            q.push_back(arr_asc[3]);

            if (q.size() != 4 || q[0] != 32'd40 || q[3] != 32'd10) begin
                $display("[FAIL] Queue integration check failed");
                errs <= errs + 1;
            end

            assoc[40] = "forty";
            assoc[10] = "ten";
            if (assoc.size() != 2 || assoc[arr_asc[0]] != "forty" || assoc[arr_asc[3]] != "ten") begin
                $display("[FAIL] Associative array integration check failed");
                errs <= errs + 1;
            end

            done <= 1'b1;
        end
    end

endmodule
