// ============================================================================
// CAS (Cycle Accurate Simulator) // Milestone Regression Benchmark
// Target Release : v3.0.0.0
// Standard Target: IEEE Std 1800-2023 §25 (Interfaces & Hierarchical Access)
// Module Name    : v3_0_0_0_interface_core
// ============================================================================

// Interface bundle definition (§25.3)
interface simple_bus_if (input logic clk);
    logic        valid;
    logic        ready;
    logic [7:0]  data;
endinterface

// Producer module driving interface signals
module bus_producer (
    simple_bus_if bus,
    input  logic  rst_n,
    input  logic  send_req,
    input  logic [7:0] send_data
);
    always @(posedge bus.clk or negedge rst_n) begin
        if (!rst_n) begin
            bus.valid <= 1'b0;
            bus.data  <= 8'h00;
        end else if (send_req) begin
            bus.valid <= 1'b1;
            bus.data  <= send_data;
        end else if (bus.ready) begin
            bus.valid <= 1'b0;
        end
    end
endmodule

// Consumer module accepting interface signals
module bus_consumer (
    simple_bus_if bus,
    input  logic  rst_n,
    output logic [7:0] received_data,
    output logic       data_valid
);
    always @(posedge bus.clk or negedge rst_n) begin
        if (!rst_n) begin
            bus.ready     <= 1'b1;
            received_data <= 8'h00;
            data_valid    <= 1'b0;
        end else begin
            if (bus.valid && bus.ready) begin
                received_data <= bus.data;
                data_valid    <= 1'b1;
            end else begin
                data_valid    <= 1'b0;
            end
        end
    end
endmodule