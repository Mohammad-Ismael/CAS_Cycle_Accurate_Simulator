// ============================================================================
// CAS (Cycle Accurate Simulator) // Milestone Regression Benchmark
// Target Release : v3.32.0.0
// Standard Target: IEEE Std 1800-2023 §8.26 (Interface Classes)
// Design Module: Polymorphic Packet Router with Multiple Interface Contracts
// ============================================================================

// Interface Contract 1: Checksum verification
interface class i_checksum;
    pure virtual function bit [15:0] compute_crc();
    pure virtual function bit is_valid();
endclass

// Interface Contract 2: Display & Inspection
interface class i_inspectable;
    pure virtual function void format_header(output string desc);
endclass

// Base Transaction class
class base_trans;
    int trans_id;
    function new(int id = 0);
        this.trans_id = id;
    endfunction
endclass

// Concrete Class implementing MULTIPLE interfaces (§8.26.1)
class eth_packet extends base_trans implements i_checksum, i_inspectable;
    bit [7:0]  payload[];
    bit [15:0] cached_crc;

    function new(int id, int length);
        super.new(id);
        payload = new[length];
        for (int i = 0; i < length; i++) begin
            payload[i] = 8'h10 + i;
        end
        cached_crc = compute_crc();
    endfunction

    // Implementing i_checksum contract
    virtual function bit [15:0] compute_crc();
        bit [15:0] crc = 16'hFFFF;
        foreach (payload[i]) begin
            crc ^= (payload[i] << 8);
        end
        return crc;
    endfunction

    virtual function bit is_valid();
        return (cached_crc == compute_crc());
    endfunction

    // Implementing i_inspectable contract
    virtual function void format_header(output string desc);
        desc = $sformatf("ETH_PKT ID=%0d LEN=%0d CRC=%04h", trans_id, payload.size(), cached_crc);
    endfunction
endclass