`timescale 1ns/1ps

// ============================================================================
// CAS (Cycle Accurate Simulator) // Milestone Regression Benchmark
// Target Release : v3.42.0.0
// Standard Target: IEEE Std 1800-2023 §6.18 (Typedef Forward Class Declarations)
// Design Module: Mutually Referential Node Ring (Producer / Consumer Handshake)
// ============================================================================

package v3_42_0_0_pkg;

    // IEEE Std 1800-2023 §6.18: Forward class declaration
    typedef class consumer_node;

    // Producer class declared before consumer_node is fully defined
    class producer_node;
        int              node_id;
        consumer_node    target_consumer; // Legal use of forward-declared handle

        function new(int id);
            this.node_id = id;
            this.target_consumer = null;
        endfunction

        function void bind_consumer(consumer_node c);
            this.target_consumer = c;
        endfunction

        function int emit_token(int val);
            if (this.target_consumer != null) begin
                return this.target_consumer.process_token(val + this.node_id);
            end
            return -1;
        endfunction
    endclass

    // Complete definition of consumer_node resolving the forward declaration
    class consumer_node;
        int              consumer_id;
        producer_node    parent_producer;

        function new(int id);
            this.consumer_id = id;
            this.parent_producer = null;
        endfunction

        function void bind_producer(producer_node p);
            this.parent_producer = p;
        endfunction

        function int process_token(int raw_val);
            return (raw_val ^ 32'h5A5A_0000) + this.consumer_id;
        endfunction
    endclass

endpackage