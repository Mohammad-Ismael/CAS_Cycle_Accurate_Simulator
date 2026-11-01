// ============================================================================
// CAS (Cycle Accurate Simulator) // Milestone Regression Benchmark
// Target Release : v3.38.0.0
// Standard Target: IEEE Std 1800-2023 §8.21 (Abstract Classes & Pure Virtuals)
// Design Module: Polymorphic Driver Framework & Protocol Engine
// ============================================================================

// IEEE Std 1800-2023 §8.21: Virtual abstract base class
virtual class base_driver;
    string name;
    int    trans_count;

    function new(string name);
        this.name = name;
        this.trans_count = 0;
    endfunction

    // Pure virtual prototypes (must be implemented by concrete subclasses)
    pure virtual function int encode_payload(int raw_data);
    pure virtual task send_packet(input int raw_data, output int packet_out);

    // Concrete virtual method in abstract base
    virtual function void log_transaction();
        trans_count++;
    endfunction
endclass

// Concrete derived class 1: AXI Driver
class axi_driver extends base_driver;
    int base_offset;

    function new(string name, int offset);
        super.new(name);
        this.base_offset = offset;
    endfunction

    virtual function int encode_payload(int raw_data);
        return raw_data + base_offset;
    endfunction

    virtual task send_packet(input int raw_data, output int packet_out);
        packet_out = encode_payload(raw_data) ^ 32'hAAAA_0000;
        log_transaction();
    endtask
endclass

// Concrete derived class 2: APB Driver
class apb_driver extends base_driver;
    function new(string name);
        super.new(name);
    endfunction

    virtual function int encode_payload(int raw_data);
        return raw_data << 2;
    endfunction

    virtual task send_packet(input int raw_data, output int packet_out);
        packet_out = encode_payload(raw_data) | 32'h0000_5555;
        log_transaction();
    endtask
endclass