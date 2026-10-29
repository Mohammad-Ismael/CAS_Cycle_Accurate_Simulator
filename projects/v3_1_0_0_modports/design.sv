// ============================================================================
// CAS (Cycle Accurate Simulator) // Milestone Regression Benchmark
// Target Release : v3.1.0.0
// Standard Target: IEEE Std 1800-2023 §25.5.3 (Interface Modports)
// Module Name    : v3_1_0_0_modport_core
// ============================================================================

interface simple_bus_if (input logic clk);
    logic        valid;
    logic        ready;
    logic [7:0]  data;

    // Modport directional views (§25.5.3)
    modport master (
        input  clk, ready,
        output valid, data
    );

    modport slave (
        input  clk, valid, data,
        output ready
    );
endinterface

// Producer typed with master modport
module bus_producer (
    simple_bus_if.master bus,
    input  logic         rst_n,
    input  logic         send_req,
    input  logic [7:0]   send_data
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

// Consumer typed with slave modport
module bus_consumer (
    simple_bus_if.slave bus,
    input  logic        rst_n,
    output logic [7:0]  received_data,
    output logic        data_valid
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