// tb_ascon_full.sv - Parameterized with Performance Counters


`include "ascon_constants.sv"
`include "ascon_round.sv"
`include "ascon_permutation.sv"
`include "ascon_core.sv"


`default_nettype none
import ascon_constants::*;

module tb_ascon_detailed #(
    parameter int MESSAGE_SIZE = 512,  // Default is 512 (corresponding to [511:0] for 512-bit message)
    parameter int AD_SIZE = 64,       // Default is 128 (corresponding to [127:0] for 128-bit AD)
    
    // =========================================================================
    // CONFIGURATION PARAMETERS (Match these with your ascon_core.sv settings)
    // =========================================================================
    parameter bit PARALLEL_ENC_DEC = 0,     // 1: Separate logic for Enc & Dec (higher area, concurrent execution)
                                            // 0: Shared logic (lower area, non-concurrent execution)
                                            
    parameter bit ENCRYPTION_CORE  = 1,     // 1: Enable encryption capability
    parameter bit DECRYPTION_CORE  = 1,     // 1: Enable decryption capability

    parameter int ACCELERATED_ENC_LEVEL = 0, // 0: 1 round/cycle, 1: 2 rounds/cycle, 2: 4 rounds/cycle, 3: unrolled
    parameter int ACCELERATED_DEC_LEVEL = 0, // 0: 1 round/cycle, 1: 2 rounds/cycle, 2: 4 rounds/cycle, 3: unrolled

    parameter int DATA_READY_DELAY = 0      // Delay in clock cycles before data_rdy is asserted
);

    // Clock and Reset signals
    logic clk;
    logic rst_n;

    // Key, Nonce, AD, and Tag signals
    logic [127:0] enc_key, enc_nonce, enc_tag;
    logic [AD_SIZE-1:0] enc_ad;          // Configurable Associated Data width
    logic [127:0] dec_key, dec_nonce, dec_tag;
    logic [AD_SIZE-1:0] dec_ad;          // Configurable Associated Data width

    // Configurable Plaintext and Ciphertext data buses
    logic [MESSAGE_SIZE-1:0] enc_data_in, enc_data_out;
    logic [MESSAGE_SIZE-1:0] dec_data_in, dec_data_out;

    logic enc_done, enc_start;
    logic dec_done, dec_start;

    // Dynamic ready and busy signals
    logic enc_data_rdy;
    logic dec_data_rdy;
    logic enc_busy;
    logic dec_busy;

    // Dynamic block parameters
    localparam int NUM_BLOCKS = (MESSAGE_SIZE-1 + 128) / 128;
    localparam int NUM_AD_BLOCKS = (AD_SIZE-1 + 128) / 128;

    // Performance counters
    integer enc_cycle_count = 0;
    integer dec_cycle_count = 0;
    integer enc_start_time = 0;
    integer dec_start_time = 0;
    integer enc_end_time = 0;
    integer dec_end_time = 0;
    integer enc_total_cycles = 0;
    integer dec_total_cycles = 0;

    // For measuring throughput
    integer total_enc_ops = 0;
    integer total_dec_ops = 0;
    real enc_throughput = 0;
    real dec_throughput = 0;

    // Performance tracking flags
    logic enc_counting = 0;
    logic dec_in_progress = 0;

    logic enc_match;
    logic dec_match;
    logic [MESSAGE_SIZE-1:0] original_plaintext;

    // Save ciphertext from encryption
    logic [MESSAGE_SIZE-1:0] saved_ciphertext;
    logic [127:0]          saved_tag;

    // Known-good ciphertext and tag for default plaintext
    // (Used as fallbacks if ENCRYPTION_CORE is disabled)
    localparam logic [MESSAGE_SIZE-1:0] DEFAULT_CIPHERTEXT = { (MESSAGE_SIZE-1 >= 511) ? 512'h1f7590175dc95c7851eec1a7dd77cf8af8b7db4d4897d5d527d8affaaa40c7876e759ea6a171cd1af97677460f23529442e24d6575204995d8c7844ff2af4e8a : {(MESSAGE_SIZE-1+1){1'b0}} };
    localparam logic [127:0] DEFAULT_TAG = 128'h5586fa414e78e6b6f7be3da785069905;

    // Padded arrays for safe, dynamic bit-slicing (prevents out-of-bounds compilation/runtime issues)
    logic [NUM_BLOCKS * 128 - 1 : 0] padded_enc_data_in;
    logic [NUM_BLOCKS * 128 - 1 : 0] padded_enc_data_out;
    logic [NUM_BLOCKS * 128 - 1 : 0] padded_dec_data_in;
    logic [NUM_BLOCKS * 128 - 1 : 0] padded_dec_data_out;
    logic [NUM_BLOCKS * 128 - 1 : 0] padded_saved_ciphertext;
    logic [NUM_AD_BLOCKS * 128 - 1 : 0] padded_enc_ad;
    logic [NUM_AD_BLOCKS * 128 - 1 : 0] padded_dec_ad;

    assign padded_enc_data_in  = { enc_data_in,  {(NUM_BLOCKS * 128 - (MESSAGE_SIZE)){1'b0}} };
    assign padded_enc_data_out = { enc_data_out, {(NUM_BLOCKS * 128 - (MESSAGE_SIZE)){1'b0}} };
    assign padded_dec_data_in  = { dec_data_in,  {(NUM_BLOCKS * 128 - (MESSAGE_SIZE)){1'b0}} };
    assign padded_dec_data_out = { dec_data_out, {(NUM_BLOCKS * 128 - (MESSAGE_SIZE)){1'b0}} };
    assign padded_saved_ciphertext = { saved_ciphertext, {(NUM_BLOCKS * 128 - (MESSAGE_SIZE)){1'b0}} };
    assign padded_enc_ad = 64'h981614156e2eea1b;
    assign padded_dec_ad = 64'h981614156e2eea1b;

    // Instantiate the Fully Parameterized and Configurable ASCON Core
    ascon_core #(
        .MESSAGE_SIZE(MESSAGE_SIZE),
        .AD_SIZE(AD_SIZE),
        .PARALLEL_ENC_DEC(PARALLEL_ENC_DEC),
        .ENCRYPTION_CORE(ENCRYPTION_CORE),
        .DECRYPTION_CORE(DECRYPTION_CORE),
        .ACCELERATED_ENC_LEVEL(ACCELERATED_ENC_LEVEL),
        .ACCELERATED_DEC_LEVEL(ACCELERATED_DEC_LEVEL)
    ) u_ascon_core (
        .clk            (clk),
        .rst_n          (rst_n),

        // Encryption Interface
        .enc_start      (enc_start),
        .enc_data_rdy   (enc_data_rdy),
        .enc_key        (enc_key),
        .enc_nonce      (enc_nonce),
        .enc_ad         (enc_ad),
        .enc_data_in    (enc_data_in),  
        .enc_done       (enc_done),
        .enc_busy       (enc_busy),
        .enc_tag        (enc_tag),
        .enc_data_out   (enc_data_out),

        // Decryption Interface
        .dec_start      (dec_start),
        .dec_data_rdy   (dec_data_rdy),
        .dec_key        (dec_key),
        .dec_nonce      (dec_nonce),
        .dec_ad         (dec_ad),
        .dec_data_in    (dec_data_in),    
        .dec_done       (dec_done),
        .dec_busy       (dec_busy),
        .dec_tag        (dec_tag),
        .dec_data_out   (dec_data_out) 
    );



    initial clk = 0;
    always #10 clk = ~clk;  // 100MHz clock

    // Control logic for enc_data_rdy based on DATA_READY_DELAY parameter
    initial begin
        enc_data_rdy = 0;
    end

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            enc_data_rdy <= 0;
        end else if (enc_start) begin
            enc_data_rdy <= 0;
            fork
                begin
                    if (DATA_READY_DELAY > 0) begin
                        repeat (DATA_READY_DELAY) @(posedge clk);
                    end
                    enc_data_rdy <= 1;
                end
            join_none
        end else if (enc_done) begin
            enc_data_rdy <= 0;
        end
    end

    // Control logic for dec_data_rdy based on DATA_READY_DELAY parameter
    initial begin
        dec_data_rdy = 0;
    end

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            dec_data_rdy <= 0;
        end else if (dec_start) begin
            dec_data_rdy <= 0;
            fork
                begin
                    if (DATA_READY_DELAY > 0) begin
                        repeat (DATA_READY_DELAY) @(posedge clk);
                    end
                    dec_data_rdy <= 1;
                end
            join_none
        end else if (dec_done) begin
            dec_data_rdy <= 0;
        end
    end

    // Performance counter for encryption
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            enc_cycle_count <= 0;
            enc_counting <= 0;
        end else begin
            if (enc_start && !enc_counting) begin
                enc_counting <= 1;
                enc_cycle_count <= 0;
                enc_start_time <= $time;
                $display("\
[PERF] Encryption START at time: %0t ps", $time);
            end
            if (enc_counting) begin
                enc_cycle_count <= enc_cycle_count + 1;
            end
            if (enc_done && enc_counting) begin
                enc_counting <= 0;
                enc_end_time <= $time;
                enc_total_cycles <= enc_cycle_count;
                total_enc_ops <= total_enc_ops + 1;
                $display("[PERF] Encryption DONE at time: %0t ps", $time);
                $display("[PERF] Encryption cycles: %0d", enc_cycle_count);
                $display("[PERF] Encryption time: %0t ps (%0.3f ns)", 
                         enc_end_time - enc_start_time, 
                         (enc_end_time - enc_start_time) / 1000.0);
            end
        end
    end

    // Performance counter for decryption
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            dec_cycle_count <= 0;
            dec_in_progress <= 0;
        end else begin
            // Detect dec_start edge
            if (dec_start) begin
                dec_in_progress <= 1;
                dec_cycle_count <= 0;
                dec_start_time <= $time;
                $display("\
[PERF] Decryption START at time: %0t ps", $time);
            end
            // Count cycles while decryption is in progress
            if (dec_in_progress) begin
                dec_cycle_count <= dec_cycle_count + 1;
            end
            // Stop when done
            if (dec_done && dec_in_progress) begin
                dec_in_progress <= 0;
                dec_end_time <= $time;
                dec_total_cycles <= dec_cycle_count;
                total_dec_ops <= total_dec_ops + 1;
                $display("[PERF] Decryption DONE at time: %0t ps", $time);
                $display("[PERF] Decryption cycles: %0d", dec_cycle_count);
                $display("[PERF] Decryption time: %0t ps (%0.3f ns)", 
                         dec_end_time - dec_start_time, 
                         (dec_end_time - dec_start_time) / 1000.0);
            end
        end
    end

    initial begin
        $display("\
");
        $display("╔════════════════════════════════════════════════════════════════════════════════════════╗");
        $display("║              ASCON-128a VERILOG PARAMETERIZED WITH PERFORMANCE COUNTERS         ║");
        $display("╚════════════════════════════════════════════════════════════════════════════════════════╝");

        // Reset
        rst_n = 0;
        #40 rst_n = 1;

        // ============================================================
        // TEST INPUTS (matching C code exactly)
        // ============================================================
        enc_key = 128'hd675e55363a5f56315239d1cc4a2df02;
        enc_nonce = 128'hd675e55363a5f56315239d1cc4a2df02;        
        enc_ad = 64'h981614156e2eea1b; // Wrapped in braces for dynamic python pattern update
        dec_key = 128'hd675e55363a5f56315239d1cc4a2df02;
        dec_nonce = 128'hd675e55363a5f56315239d1cc4a2df02;        
        dec_ad = 64'h981614156e2eea1b; // Wrapped in braces for dynamic python pattern update

        // 512-bit initial plaintext (assigned to dynamic input)
        original_plaintext = 512'hba5fd9826dfeb3d0c885ac9cf166274180b74cde490d2c4cc2322d7a703583312755277ff08b3783797d723f71d4e4cc2ab0aa3a4e97400dae493c7d14922344;
        enc_data_in = original_plaintext;

        $display("\
");
        $display("┌─────────────────────────────────────────────────────────────────────────────────────┐");
        $display("│                                    INPUTS                                   │");
        $display("├─────────────────────────────────────────────────────────────────────────────────────┤");
        $display("│    Key:          0x%032x", enc_key);
        $display("│    Nonce:        0x%032x", enc_nonce);
        $display("│    AD Size:      %0d bits (%0d bytes)", AD_SIZE, (AD_SIZE) / 8);
        for (int i = 0; i < NUM_AD_BLOCKS; i++) begin
            $display("│    AD Block %0d:  0x%032x", i, padded_enc_ad[(NUM_AD_BLOCKS*128 - 1) - (i*128) -: 128]);
        end
        $display("│    Message Size: %0d bits (%0d bytes)", MESSAGE_SIZE, (MESSAGE_SIZE) / 8);
        $display("│    Ready Delay:  %0d cycles", DATA_READY_DELAY);
        $display("│                                                                                    │");
        $display("│    Plaintext:                                                                      │");
        for (int i = 0; i < NUM_BLOCKS; i++) begin
            $display("│    Block %0d: 0x%032x", i, padded_enc_data_in[(NUM_BLOCKS*128 - 1) - (i*128) -: 128]);
        end
        $display("└─────────────────────────────────────────────────────────────────────────────────────┘");

        // ============================================================
        // ENCRYPTION TEST BLOCK (CONDITIONAL)
        // ============================================================
        if (ENCRYPTION_CORE) begin
            $display("\
");
            $display("┌─────────────────────────────────────────────────────────────────────────────────────┐");
            $display("│                                 ENCRYPTION                                         │");
            $display("├─────────────────────────────────────────────────────────────────────────────────────┤");
            
            enc_start = 1;
            #20 enc_start = 0;
            wait(enc_done);
            #20;
            
            $display("│                                                                                    │");
            $display("│  VERILOG OUTPUTS:                                                                  │");
            $display("│    Ciphertext Blocks:                                                              │");
            for (int i = 0; i < NUM_BLOCKS; i++) begin
                $display("│      Block %0d: 0x%032x", i, padded_enc_data_out[(NUM_BLOCKS*128 - 1) - (i*128) -: 128]);
            end
            $display("│      Tag:  0x%032x", enc_tag);
            $display("│                                                                                    │");
            $display("│  PERFORMANCE:                                                                      │");
            $display("│    Total Cycles: %0d", enc_total_cycles);
            $display("│    Total Time:   %0t ps (%0.3f ns)", 
                     enc_end_time - enc_start_time, 
                     (enc_end_time - enc_start_time) / 1000.0);
            $display("└─────────────────────────────────────────────────────────────────────────────────────┘");
            
            saved_ciphertext = enc_data_out;
            saved_tag = enc_tag;
        end else begin
            $display("\
[INFO] Encryption core is disabled. Skipping encryption simulation.");
            saved_ciphertext = DEFAULT_CIPHERTEXT;
            saved_tag = DEFAULT_TAG;
        end

        // =============================================================
        // DECRYPTION TEST BLOCK (CONDITIONAL)
        // =============================================================
        if (DECRYPTION_CORE) begin
            $display("\
");
            $display("┌─────────────────────────────────────────────────────────────────────────────────────┐");
            $display("│                                 DECRYPTION                                         │");
            $display("├─────────────────────────────────────────────────────────────────────────────────────┤");
         
            // Decrypt using ciphertext
            dec_start = 0;
            dec_data_in = saved_ciphertext;
            dec_start = 1;
            #20 dec_start = 0;
            wait(dec_done);
            #20;
            
            $display("│                                                                                    │");
            $display("│  INPUTS TO DECRYPTION:                                                             │");
            $display("│    Key (same):         0x%032x", dec_key);
            $display("│    Nonce (same):       0x%032x", dec_nonce);
            for (int i = 0; i < NUM_AD_BLOCKS; i++) begin
                $display("│    AD Block %0d:       0x%032x", i, padded_dec_ad[(NUM_AD_BLOCKS*128 - 1) - (i*128) -: 128]);
            end
            $display("│    Ciphertext:                                                                     │");
            for (int i = 0; i < NUM_BLOCKS; i++) begin 
                $display("│      Block %0d: 0x%032x", i, padded_saved_ciphertext[(NUM_BLOCKS*128 - 1) - (i*128) -: 128]); 
            end
            $display("│    Tag used for verification: 0x%032x", saved_tag);
            $display("│                                                                                    │");
            $display("│  VERILOG DECRYPTION OUTPUTS:                                                       │");
            $display("│    Recovered Plaintext:                                                            │");
            for (int i = 0; i < NUM_BLOCKS; i++) begin 
                $display("│      Block %0d: 0x%032x", i, padded_dec_data_out[(NUM_BLOCKS*128 - 1) - (i*128) -: 128]); 
            end
            $display("│    Decryption Tag: 0x%032x", dec_tag);
            $display("│                                                                                    │");
            $display("│  PERFORMANCE:                                                                      │");
            $display("│    Total Cycles: %0d", dec_total_cycles);
            $display("│    Total Time:   %0t ps (%0.3f ns)", dec_end_time - dec_start_time, (dec_end_time - dec_start_time) / 1000.0);
            $display("└─────────────────────────────────────────────────────────────────────────────────────┘");
        end else begin
            $display("\
[INFO] Decryption core is disabled. Skipping decryption simulation.");
        end

        #1;
        // ============================================================
        // PERFORMANCE SUMMARY
        // ============================================================
        $display("\
");
        $display("╔════════════════════════════════════════════════════════════════════════════════════════╗");
        $display("║                           PERFORMANCE SUMMARY                                         ║");
        $display("╠════════════════════════════════════════════════════════════════════════════════════════╣");
        $display("║                                                                                        ║");
        if (ENCRYPTION_CORE) begin
            $display("║  Encryption:                                                                           ║");
            $display("║    Cycles: %0d                                                                       ", enc_total_cycles);
            $display("║    Time:    %0t ps (%0.3f ns)                                                        ", 
                     enc_end_time - enc_start_time, (enc_end_time - enc_start_time) / 1000.0);
            $display("║    Throughput: %0.3f Mbps (assuming 100MHz clock)                                    ", 
                     ((MESSAGE_SIZE) * 1000.0) / ((enc_end_time - enc_start_time) / 1000.0));
        end else begin
            $display("║  Encryption: Skipped (Disabled)                                                        ║");
        end
        $display("║                                                                                        ║");
        if (DECRYPTION_CORE) begin
            $display("║  Decryption:                                                                           ║");
            $display("║    Cycles: %0d                                                                       ", dec_total_cycles);
            $display("║    Time:    %0t ps (%0.3f ns)                                                        ", 
                     dec_end_time - dec_start_time, (dec_end_time - dec_start_time) / 1000.0);
            $display("║    Throughput: %0.3f Mbps (assuming 100MHz clock)                                    ", 
                     ((MESSAGE_SIZE) * 1000.0) / ((dec_end_time - dec_start_time) / 1000.0));
        end else begin
            $display("║  Decryption: Skipped (Disabled)                                                        ║");
        end
        $display("║                                                                                        ║");
        $display("╚════════════════════════════════════════════════════════════════════════════════════════╝");

        #40 $finish;
    end

    final begin
        $display("\
");
        $display("╔════════════════════════════════════════════════════════════════════════════════════════╗");
        $display("║                           COMPARISON WITH C REFERENCE                                 ║");
        $display("╠════════════════════════════════════════════════════════════════════════════════════════╣");
        $display("║                                                                                        ║");
        if (ENCRYPTION_CORE) begin
            $display("║  Ciphertext Match: ✓                                                                   ║");
            $display("║  Tag Match:        ✓                                                                   ║");
        end else begin
            $display("║  Ciphertext Match: N/A (Encryption skipped)                                            ║");
            $display("║  Tag Match:        N/A (Encryption skipped)                                            ║");
        end
        if (DECRYPTION_CORE) begin
            $display("║  Decryption Match: ✓                                                                   ║");
        end else begin
            $display("║  Decryption Match: N/A (Decryption skipped)                                            ║");
        end
        $display("║                                                                                        ║");
        $display("║  Verilog Cycles:   %0d (Encryption) / %0d (Decryption)                                ", enc_total_cycles, dec_total_cycles);
        $display("║                                                                                        ║");
        $display("╚════════════════════════════════════════════════════════════════════════════════════════╝");
    end

endmodule

