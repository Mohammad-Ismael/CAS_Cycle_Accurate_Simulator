`timescale 1ns/1ps

// ============================================================================
// CAS (Cycle Accurate Simulator) // Milestone Regression Benchmark
// Target Release : v3.46.0.0
// Standard Target: IEEE Std 1800-2023 §6.24.2 (Dynamic Casting $cast)
// Design Module: Polymorphic Dispatcher and Protocol Packet Classifier
// ============================================================================

package v3_46_0_0_pkg;

    typedef enum logic [1:0] {
        CMD_IDLE  = 2'b00,
        CMD_WRITE = 2'b01,
        CMD_RESET = 2'b10
        // Notice 2'b11 is NOT a valid enumerator in cmd_e
    } cmd_e;

    // Base transaction class
    class base_packet;
        int packet_id;
        function new(int id);
            this.packet_id = id;
        endfunction
        virtual function string get_type();
            return "BASE";
        endfunction
    endclass

    // Derived telemetry packet
    class telemetry_packet extends base_packet;
        int sensor_data;
        function new(int id, int data);
            super.new(id);
            this.sensor_data = data;
        endfunction
        virtual function string get_type();
            return "TELEMETRY";
        endfunction
    endclass

    // Derived control packet
    class control_packet extends base_packet;
        int ctrl_flags;
        function new(int id, int flags);
            super.new(id);
            this.ctrl_flags = flags;
        endfunction
        virtual function string get_type();
            return "CONTROL";
        endfunction
    endclass

endpackage