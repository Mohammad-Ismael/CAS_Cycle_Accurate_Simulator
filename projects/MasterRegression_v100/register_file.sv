// register_file.sv - Dual-port RAM array with synchronous write & asynchronous read
module register_file #(
    parameter DATA_WIDTH = 8,
    parameter ADDR_WIDTH = 4,
    parameter DEPTH      = 16
)(
    input  logic                  clk,
    input  logic                  rst_n,
    input  logic                  wr_en,
    input  logic [ADDR_WIDTH-1:0] wr_addr,
    input  logic [DATA_WIDTH-1:0] wr_data,
    input  logic [ADDR_WIDTH-1:0] rd_addr,
    output logic [DATA_WIDTH-1:0] rd_data
);
    logic [DATA_WIDTH-1:0] mem [0:DEPTH-1];

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            // Reset clears bottom storage cell
            mem[0] <= 0;
        end else if (wr_en) begin
            mem[wr_addr] <= wr_data;
        end
    end

    // Asynchronous memory array read
    assign rd_data = mem[rd_addr];

endmodule