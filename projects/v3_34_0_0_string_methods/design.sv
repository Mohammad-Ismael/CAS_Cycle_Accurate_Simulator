// ============================================================================
// CAS (Cycle Accurate Simulator) // Milestone Regression Benchmark
// Target Release : v3.34.0.0
// Standard Target: IEEE Std 1800-2023 §6.16 (String Data Type & Built-in Methods)
// Design Module: String Command Parser and Register Router
// ============================================================================

module v3_34_0_0_cmd_router (
    input  logic        clk,
    input  logic        rst_n,
    input  string       cmd_str,
    input  logic        cmd_valid,
    output logic [31:0] parsed_addr,
    output logic [15:0] parsed_data,
    output logic        cmd_ready,
    output logic        cmd_error
);
    // Command format expected: "WRITE:0x1000:0xABCD" or "READ:0x2000"

    function automatic bit parse_command(
        input  string in_cmd,
        output logic [31:0] addr,
        output logic [15:0] data
    );
        string opcode;
        string upper_cmd;
        int len;

        upper_cmd = in_cmd.toupper();
        len       = upper_cmd.len();

        if (len < 5) return 0;

        // Check prefix opcode using substr
        opcode = upper_cmd.substr(0, 4); // "WRITE"

        if (opcode == "WRITE") begin
            string addr_str, data_str;
            // "WRITE:1000:ABCD"
            // substr indices: 6..9 for addr, 11..14 for data
            if (len >= 15) begin
                addr_str = upper_cmd.substr(6, 9);
                data_str = upper_cmd.substr(11, 14);
                addr     = addr_str.atohex();
                data     = data_str.atohex();
                return 1;
            end
        end
        return 0;
    endfunction

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            parsed_addr <= 32'h0;
            parsed_data <= 16'h0;
            cmd_ready   <= 1'b0;
            cmd_error   <= 1'b0;
        end else if (cmd_valid) begin
            logic [31:0] a;
            logic [15:0] d;
            if (parse_command(cmd_str, a, d)) begin
                parsed_addr <= a;
                parsed_data <= d;
                cmd_ready   <= 1'b1;
                cmd_error   <= 1'b0;
            end else begin
                cmd_ready   <= 1'b0;
                cmd_error   <= 1'b1;
            end
        end else begin
            cmd_ready <= 1'b0;
            cmd_error <= 1'b0;
        end
    end

endmodule