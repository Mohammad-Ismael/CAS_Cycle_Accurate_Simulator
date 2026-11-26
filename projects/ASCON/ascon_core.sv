// ascon_core.sv
// file: ascon_core.sv
// Description: Fully Parameterized and Configurable ASCON-128a Core with 4-level Acceleration.
//              Supports both Parallel and Shared architectures for Encryption and Decryption.
//              Generalized to support arbitrary-width Associated Data (AD_SIZE >= 0).
`default_nettype none
import ascon_constants::*;

module ascon_core #(
    parameter int MESSAGE_SIZE = 512,       // Message size in bits (multiple of 128)
    parameter int AD_SIZE = 128,            // Associated Data size in bits (Arbitrary width AD_SIZE >= 0)
    
    // Configuration Parameters
    parameter bit PARALLEL_ENC_DEC = 0,     // 1: Separate logic for Enc & Dec, 0: Shared logic
    parameter bit ENCRYPTION_CORE  = 1,     // 1: Enable encryption capability
    parameter bit DECRYPTION_CORE  = 1,     // 1: Enable decryption capability

    parameter int ACCELERATED_ENC_LEVEL = 0, // 0: 1 round/cycle, 1: 2 rounds/cycle, 2: 4 rounds/cycle, 3: full permutation
    parameter int ACCELERATED_DEC_LEVEL = 0  // 0: 1 round/cycle, 1: 2 rounds/cycle, 2: 4 rounds/cycle, 3: full permutation
) (
    input  logic                    clk,
    input  logic                    rst_n,

    // Encryption Interface
    input  logic                    enc_start,
    input  logic                    enc_data_rdy,     // Indicates plaintext data is ready
    input  logic [127:0]            enc_key,
    input  logic [127:0]            enc_nonce,
    input  logic [AD_SIZE-1:0]      enc_ad,           // Configurable Associated Data input
    input  logic [MESSAGE_SIZE-1:0] enc_data_in,      // Plaintext input
    output logic                    enc_done,
    output logic                    enc_busy,         // Busy indicator for encryption
    output logic [127:0]            enc_tag,          // Generated tag
    output logic [MESSAGE_SIZE-1:0] enc_data_out,     // Ciphertext output

    // Decryption Interface
    input  logic                    dec_start,
    input  logic                    dec_data_rdy,     // Indicates ciphertext data is ready
    input  logic [127:0]            dec_key,
    input  logic [127:0]            dec_nonce,
    input  logic [AD_SIZE-1:0]      dec_ad,           // Configurable Associated Data input
    input  logic [MESSAGE_SIZE-1:0] dec_data_in,      // Ciphertext input
    output logic                    dec_done,
    output logic                    dec_busy,         // Busy indicator for decryption
    output logic [127:0]            dec_tag,          // Decryption tag
    output logic [MESSAGE_SIZE-1:0] dec_data_out      // Recovered plaintext output
);

    // --- State Machine Enum ---
    typedef enum logic [4:0] {
        IDLE, INIT_LOAD,
        INIT_PERM, INIT_ROUND,
        INIT_XOR,
        AD_ABSORB,
        AD_PERM1, AD_PERM2,
        AD_ROUND, AD_ROUND2,
        DOMAIN_SEP,
        WAIT_DATA,                         
        DATA_ABSORB,
        DATA_PERM, DATA_ROUND,
        DATA_STORE,
        FINAL_XOR1,
        FINAL_PERM, FINAL_ROUND,
        FINAL_XOR2,
        DONE_STATE
    } state_t;

    // --- Dynamic Parameters ---
    localparam int NUM_BLOCKS = (MESSAGE_SIZE + 127) / 128;
    localparam int REM_BITS = MESSAGE_SIZE % 128;
    localparam int SAFE_REM_BITS = (REM_BITS > 0) ? REM_BITS : 1;
    localparam int CNT_WIDTH  = (NUM_BLOCKS > 1) ? $clog2(NUM_BLOCKS) : 1;

    // Arbitrary AD Parameters
    localparam int NUM_AD_BLOCKS = (AD_SIZE == 0) ? 0 : (AD_SIZE + 127) / 128;
    localparam int AD_CNT_WIDTH  = (NUM_AD_BLOCKS > 1) ? $clog2(NUM_AD_BLOCKS) : 1;
    localparam int SAFE_NUM_AD_BLOCKS = (NUM_AD_BLOCKS > 0) ? NUM_AD_BLOCKS : 1;

    logic enc_busy_internal;
    logic dec_busy_internal;
    assign enc_busy = enc_busy_internal;
    assign dec_busy = dec_busy_internal;

    generate
        if (PARALLEL_ENC_DEC) begin : gen_parallel
            
            // =================================================================
            // ENCRYPTION PATH (INDEPENDENT CORES)
            // =================================================================
            if (ENCRYPTION_CORE) begin : gen_enc_path
                state_t enc_state, enc_next_state;
                logic [CNT_WIDTH-1:0] enc_block_cnt;
                logic [AD_CNT_WIDTH-1:0] enc_ad_block_cnt;
                logic [3:0] enc_round_cnt;
                logic [3:0] enc_num_rounds;
                
                logic [63:0] enc_x0, enc_x1, enc_x2, enc_x3, enc_x4;
                logic [127:0] enc_data_out_block;
                logic [127:0] enc_reordered_block;
                logic [127:0] enc_reordered_mask;

                // AD Signals
                logic [SAFE_NUM_AD_BLOCKS * 128 - 1 : 0] enc_padded_ad;
                logic [127:0] enc_raw_ad_block;
                logic [63:0]  enc_ad_data_x0, enc_ad_data_x1;
                logic [63:0]  enc_ad_pad_x0,  enc_ad_pad_x1;
                logic         enc_ad_needs_pad0;
                int           enc_rem_ad_bits;

                assign enc_padded_ad = { enc_ad, {(SAFE_NUM_AD_BLOCKS * 128 - AD_SIZE){1'b0}} };

                always_comb begin
                    enc_raw_ad_block = (NUM_AD_BLOCKS > 0) ? enc_padded_ad[(SAFE_NUM_AD_BLOCKS * 128 - 1) - (enc_ad_block_cnt * 128) -: 128] : 128'h0;
                    enc_rem_ad_bits  = AD_SIZE - (enc_ad_block_cnt * 128);
                    
                    if (enc_rem_ad_bits >= 128) begin
                        enc_ad_data_x0    = enc_raw_ad_block[127:64];
                        enc_ad_data_x1    = enc_raw_ad_block[63:0];
                        enc_ad_pad_x0     = 64'h0;
                        enc_ad_pad_x1     = 64'h0;
                        enc_ad_needs_pad0 = 1'b1;
                    end else if (enc_rem_ad_bits >= 64) begin
                        enc_ad_data_x0    = enc_raw_ad_block[127:64];
                        enc_ad_data_x1    = enc_raw_ad_block[63:0];
                        enc_ad_pad_x0     = 64'h0;
                        enc_ad_pad_x1     = (64'h1 << (enc_rem_ad_bits - 64));
                        enc_ad_needs_pad0 = 1'b0;
                    end else if (enc_rem_ad_bits > 0) begin
                        enc_ad_data_x0    = enc_raw_ad_block[127:64];
                        enc_ad_data_x1    = 64'h0;
                        enc_ad_pad_x0     = (64'h1 << enc_rem_ad_bits);
                        enc_ad_pad_x1     = 64'h0;
                        enc_ad_needs_pad0 = 1'b0;
                    end else begin
                        enc_ad_data_x0    = 64'h0;
                        enc_ad_data_x1    = 64'h0;
                        enc_ad_pad_x0     = 64'h0;
                        enc_ad_pad_x1     = 64'h0;
                        enc_ad_needs_pad0 = 1'b0;
                    end
                end

                // Outputs
                logic enc_done_reg;
                logic [127:0] enc_tag_reg;
                logic [MESSAGE_SIZE-1:0] enc_data_out_reg;

                logic [127:0] enc_raw_data_block;
                logic [127:0] enc_raw_mask_block;
                logic enc_run_perm;

                localparam int LOCAL_REM_BITS_ENC = MESSAGE_SIZE % 128;
                logic [127:0] enc_tmp_data;
                logic [127:0] enc_tmp_mask;

                logic [NUM_BLOCKS * 128 - 1 : 0] enc_padded_data_in;
                logic [NUM_BLOCKS * 128 - 1 : 0] enc_padded_data_out_reg;

                assign enc_padded_data_in = { enc_data_in, {(NUM_BLOCKS * 128 - MESSAGE_SIZE){1'b0}} };
                assign enc_data_out_reg   = enc_padded_data_out_reg[NUM_BLOCKS * 128 - 1 -: MESSAGE_SIZE];

                assign enc_reordered_block = {
                    enc_raw_data_block[71:64], enc_raw_data_block[79:72], enc_raw_data_block[87:80], enc_raw_data_block[95:88],
                    enc_raw_data_block[103:96],enc_raw_data_block[111:104],enc_raw_data_block[119:112],enc_raw_data_block[127:120],
                    enc_raw_data_block[7:0],   enc_raw_data_block[15:8],   enc_raw_data_block[23:16], enc_raw_data_block[31:24],
                    enc_raw_data_block[39:32], enc_raw_data_block[47:40], enc_raw_data_block[55:48], enc_raw_data_block[63:56]
                };

                assign enc_run_perm = (enc_block_cnt < (MESSAGE_SIZE / 128));
                assign enc_done = enc_done_reg;
                assign enc_tag  = enc_tag_reg;
                assign enc_data_out = enc_data_out_reg;
                assign enc_busy_internal = (enc_state != IDLE);

                always_comb begin
                    enc_raw_data_block = 128'h0;
                    enc_raw_mask_block = 128'h0;
                    enc_tmp_data = 128'h0;
                    enc_tmp_mask = 128'h0;
                    if (enc_block_cnt < NUM_BLOCKS) begin
                        if (enc_block_cnt < (MESSAGE_SIZE / 128)) begin
                            enc_raw_data_block = enc_padded_data_in[(NUM_BLOCKS * 128 - 1) - (enc_block_cnt * 128) -: 128];
                            enc_raw_mask_block = {128{1'b1}};
                        end else begin
                            for (int bit_idx = 0; bit_idx < 128; bit_idx++) begin
                                if (bit_idx < LOCAL_REM_BITS_ENC) begin
                                    enc_tmp_data[127 - bit_idx] = enc_padded_data_in[(NUM_BLOCKS * 128 - 1) - bit_idx];
                                    enc_tmp_mask[127 - bit_idx] = 1'b1;
                                end else if (bit_idx == (LOCAL_REM_BITS_ENC + 7)) begin
                                    enc_tmp_data[127 - bit_idx] = 1'b1;
                                    enc_tmp_mask[127 - bit_idx] = 1'b0;
                                end else begin
                                    enc_tmp_data[127 - bit_idx] = 1'b0;
                                    enc_tmp_mask[127 - bit_idx] = 1'b0;
                                end
                            end
                            enc_raw_data_block = enc_tmp_data;
                            enc_raw_mask_block = enc_tmp_mask;
                        end
                    end
                end

                // Instantiate permutations/rounds for Encryption
                logic [63:0] enc_x0_n, enc_x1_n, enc_x2_n, enc_x3_n, enc_x4_n;          
                logic [63:0] enc_x0_8_n, enc_x1_8_n, enc_x2_8_n, enc_x3_8_n, enc_x4_8_n; 
                logic [63:0] enc_x0_r_n, enc_x1_r_n, enc_x2_r_n, enc_x3_r_n, enc_x4_r_n; 

                localparam bit ACCELERATED_ENC = (ACCELERATED_ENC_LEVEL == 3);
                localparam int ROUNDS_PER_CYCLE_ENC = (ACCELERATED_ENC_LEVEL == 0) ? 1 : 
                                                      (ACCELERATED_ENC_LEVEL == 1) ? 2 : 4;
                localparam int INIT_LIMIT_ENC = (12 / ROUNDS_PER_CYCLE_ENC) - 1;
                localparam int DATA_LIMIT_ENC = (8 / ROUNDS_PER_CYCLE_ENC) - 1;

                if (ACCELERATED_ENC) begin : gen_enc_acc
                    ascon_permutation #(.NUM_ROUNDS(12)) u_enc_perm_12 (
                        .x0_in(enc_x0), .x1_in(enc_x1), .x2_in(enc_x2), .x3_in(enc_x3), .x4_in(enc_x4),
                        .x0_out(enc_x0_n), .x1_out(enc_x1_n), .x2_out(enc_x2_n), .x3_out(enc_x3_n), .x4_out(enc_x4_n)
                    );
                    ascon_permutation #(.NUM_ROUNDS(8)) u_enc_perm_8 (
                        .x0_in(enc_x0), .x1_in(enc_x1), .x2_in(enc_x2), .x3_in(enc_x3), .x4_in(enc_x4),
                        .x0_out(enc_x0_8_n), .x1_out(enc_x1_8_n), .x2_out(enc_x2_8_n), .x3_out(enc_x3_8_n), .x4_out(enc_x4_8_n)
                    );
                    assign enc_x0_r_n = 0; assign enc_x1_r_n = 0; assign enc_x2_r_n = 0; assign enc_x3_r_n = 0; assign enc_x4_r_n = 0;
                end else begin : gen_enc_seq
                    logic [7:0] enc_rc0, enc_rc1, enc_rc2, enc_rc3;
                    if (ACCELERATED_ENC_LEVEL == 0) begin : gen_enc_seq_l0
                        always_comb begin
                            enc_rc0 = (enc_num_rounds == 12) ? RC12[enc_round_cnt] : RC8[enc_round_cnt];
                        end
                        ascon_round u_enc_round (
                            .x0_in(enc_x0), .x1_in(enc_x1), .x2_in(enc_x2), .x3_in(enc_x3), .x4_in(enc_x4),
                            .rc(enc_rc0),
                            .x0_out(enc_x0_r_n), .x1_out(enc_x1_r_n), .x2_out(enc_x2_r_n), .x3_out(enc_x3_r_n), .x4_out(enc_x4_r_n)
                        );
                    end else if (ACCELERATED_ENC_LEVEL == 1) begin : gen_enc_seq_l1
                        always_comb begin
                            if (enc_num_rounds == 12) begin
                                enc_rc0 = RC12[2 * enc_round_cnt];
                                enc_rc1 = RC12[2 * enc_round_cnt + 1];
                            end else begin
                                enc_rc0 = RC8[2 * enc_round_cnt];
                                enc_rc1 = RC8[2 * enc_round_cnt + 1];
                            end
                        end
                        ascon_round_2 u_enc_round_2 (
                            .x0_in(enc_x0), .x1_in(enc_x1), .x2_in(enc_x2), .x3_in(enc_x3), .x4_in(enc_x4),
                            .rc0(enc_rc0), .rc1(enc_rc1),
                            .x0_out(enc_x0_r_n), .x1_out(enc_x1_r_n), .x2_out(enc_x2_r_n), .x3_out(enc_x3_r_n), .x4_out(enc_x4_r_n)
                        );
                    end else if (ACCELERATED_ENC_LEVEL == 2) begin : gen_enc_seq_l2
                        always_comb begin
                            if (enc_num_rounds == 12) begin
                                enc_rc0 = RC12[4 * enc_round_cnt];
                                enc_rc1 = RC12[4 * enc_round_cnt + 1];
                                enc_rc2 = RC12[4 * enc_round_cnt + 2];
                                enc_rc3 = RC12[4 * enc_round_cnt + 3];
                            end else begin
                                enc_rc0 = RC8[4 * enc_round_cnt];
                                enc_rc1 = RC8[4 * enc_round_cnt + 1];
                                enc_rc2 = RC8[4 * enc_round_cnt + 2];
                                enc_rc3 = RC8[4 * enc_round_cnt + 3];
                            end
                        end
                        ascon_round_4 u_enc_round_4 (
                            .x0_in(enc_x0), .x1_in(enc_x1), .x2_in(enc_x2), .x3_in(enc_x3), .x4_in(enc_x4),
                            .rc0(enc_rc0), .rc1(enc_rc1), .rc2(enc_rc2), .rc3(enc_rc3),
                            .x0_out(enc_x0_r_n), .x1_out(enc_x1_r_n), .x2_out(enc_x2_r_n), .x3_out(enc_x3_r_n), .x4_out(enc_x4_r_n)
                        );
                    end
                    assign enc_x0_n = 0; assign enc_x1_n = 0; assign enc_x2_n = 0; assign enc_x3_n = 0; assign enc_x4_n = 0;
                    assign enc_x0_8_n = 0; assign enc_x1_8_n = 0; assign enc_x2_8_n = 0; assign enc_x3_8_n = 0; assign enc_x4_8_n = 0;
                end

                // FSM transition logic
                always_comb begin
                    enc_next_state = enc_state;
                    case (enc_state)
                        IDLE:       if (enc_start) enc_next_state = INIT_LOAD;
                        INIT_LOAD:  enc_next_state = (ACCELERATED_ENC) ? INIT_PERM : INIT_ROUND;
                        INIT_PERM:  enc_next_state = INIT_XOR;
                        INIT_ROUND: if (enc_round_cnt == INIT_LIMIT_ENC) enc_next_state = INIT_XOR;
                        
                        INIT_XOR:   enc_next_state = (NUM_AD_BLOCKS == 0) ? DOMAIN_SEP : AD_ABSORB;
                        
                        AD_ABSORB:  enc_next_state = (ACCELERATED_ENC) ? AD_PERM1 : AD_ROUND;
                        
                        AD_PERM1:   if (enc_ad_block_cnt == (NUM_AD_BLOCKS - 1)) begin
                                        if (enc_ad_needs_pad0) enc_next_state = AD_PERM2;
                                        else enc_next_state = DOMAIN_SEP;
                                    end else begin
                                        enc_next_state = AD_ABSORB;
                                    end
                        AD_PERM2:   enc_next_state = DOMAIN_SEP;
                        
                        AD_ROUND:   if (enc_round_cnt == DATA_LIMIT_ENC) begin
                                        if (enc_ad_block_cnt == (NUM_AD_BLOCKS - 1)) begin
                                            if (enc_ad_needs_pad0) enc_next_state = AD_ROUND2;
                                            else enc_next_state = DOMAIN_SEP;
                                        end else begin
                                            enc_next_state = AD_ABSORB;
                                        end
                                    end
                        AD_ROUND2:  if (enc_round_cnt == DATA_LIMIT_ENC) enc_next_state = DOMAIN_SEP;
                        
                        DOMAIN_SEP: enc_next_state = (enc_data_rdy) ? DATA_ABSORB : WAIT_DATA;
                        WAIT_DATA:  if (enc_data_rdy) enc_next_state = DATA_ABSORB;
                        
                        DATA_ABSORB:enc_next_state = (enc_run_perm) ? ((ACCELERATED_ENC) ? DATA_PERM : DATA_ROUND) : DATA_STORE;
                        DATA_PERM:  enc_next_state = DATA_STORE;
                        DATA_ROUND: if (enc_round_cnt == DATA_LIMIT_ENC) enc_next_state = DATA_STORE;
                        
                        DATA_STORE: if (enc_block_cnt == (NUM_BLOCKS - 1)) enc_next_state = FINAL_XOR1;
                                    else enc_next_state = (enc_data_rdy) ? DATA_ABSORB : WAIT_DATA;
                        
                        FINAL_XOR1: enc_next_state = (ACCELERATED_ENC) ? FINAL_PERM : FINAL_ROUND;
                        FINAL_PERM: enc_next_state = FINAL_XOR2;
                        FINAL_ROUND:if (enc_round_cnt == INIT_LIMIT_ENC) enc_next_state = FINAL_XOR2;
                        FINAL_XOR2: enc_next_state = DONE_STATE;
                        DONE_STATE: enc_next_state = IDLE;
                        default:    enc_next_state = IDLE;
                    endcase
                end

                // FSM sequential logic
                always_ff @(posedge clk or negedge rst_n) begin
                    if (!rst_n) begin
                        enc_state <= IDLE;
                        enc_block_cnt <= 0;
                        enc_ad_block_cnt <= 0;
                        enc_round_cnt <= 0;
                        enc_num_rounds <= 12;
                        enc_x0 <= 0; enc_x1 <= 0; enc_x2 <= 0; enc_x3 <= 0; enc_x4 <= 0;
                        enc_done_reg <= 0;
                        enc_tag_reg <= 0;
                        enc_padded_data_out_reg <= 0;
                        enc_data_out_block <= 0;
                    end else begin
                        enc_state <= enc_next_state;
                        case (enc_state)
                            IDLE: begin
                                enc_done_reg <= 0;
                                enc_block_cnt <= 0;
                                enc_ad_block_cnt <= 0;
                                enc_round_cnt <= 0;
                            end

                            INIT_LOAD: begin
                                enc_x0 <= IV;
                                enc_x1 <= enc_key[127:64];
                                enc_x2 <= enc_key[63:0];
                                enc_x3 <= enc_nonce[127:64];
                                enc_x4 <= enc_nonce[63:0];
                                enc_num_rounds <= 12;
                                enc_round_cnt <= 0;
                                enc_ad_block_cnt <= 0;
                            end

                            INIT_PERM: begin
                                enc_x0 <= enc_x0_n; enc_x1 <= enc_x1_n; enc_x2 <= enc_x2_n;
                                enc_x3 <= enc_x3_n; enc_x4 <= enc_x4_n;
                            end

                            INIT_ROUND: begin
                                enc_x0 <= enc_x0_r_n; enc_x1 <= enc_x1_r_n; enc_x2 <= enc_x2_r_n;
                                enc_x3 <= enc_x3_r_n; enc_x4 <= enc_x4_r_n;
                                enc_round_cnt <= enc_round_cnt + 1;
                            end

                            INIT_XOR: begin
                                enc_x3 <= enc_x3 ^ enc_key[127:64];
                                enc_x4 <= enc_x4 ^ enc_key[63:0];
                                enc_round_cnt <= 0;
                                enc_ad_block_cnt <= 0;
                            end

                            AD_ABSORB: begin
                                enc_x0 <= enc_x0 ^ enc_ad_data_x0 ^ enc_ad_pad_x0;
                                enc_x1 <= enc_x1 ^ enc_ad_data_x1 ^ enc_ad_pad_x1;
                                enc_num_rounds <= 8;
                                enc_round_cnt <= 0;
                            end

                            AD_PERM1: begin
                                if (enc_ad_block_cnt == (NUM_AD_BLOCKS - 1)) begin
                                    if (enc_ad_needs_pad0) enc_x0 <= enc_x0_8_n ^ 64'h0000000000000001;
                                    else enc_x0 <= enc_x0_8_n;
                                end else begin
                                    enc_x0 <= enc_x0_8_n;
                                end
                                enc_x1 <= enc_x1_8_n;
                                enc_x2 <= enc_x2_8_n;
                                enc_x3 <= enc_x3_8_n;
                                enc_x4 <= enc_x4_8_n;
                                
                                if (enc_ad_block_cnt < (NUM_AD_BLOCKS - 1)) begin
                                    enc_ad_block_cnt <= enc_ad_block_cnt + 1;
                                end
                            end

                            AD_PERM2: begin
                                enc_x0 <= enc_x0_8_n;
                                enc_x1 <= enc_x1_8_n;
                                enc_x2 <= enc_x2_8_n;
                                enc_x3 <= enc_x3_8_n;
                                enc_x4 <= enc_x4_8_n;
                            end

                            AD_ROUND: begin
                                enc_x0 <= enc_x0_r_n; enc_x1 <= enc_x1_r_n; enc_x2 <= enc_x2_r_n;
                                enc_x3 <= enc_x3_r_n; enc_x4 <= enc_x4_r_n;
                                if (enc_round_cnt == DATA_LIMIT_ENC) begin
                                    if (enc_ad_block_cnt == (NUM_AD_BLOCKS - 1)) begin
                                        if (enc_ad_needs_pad0) begin
                                            enc_x0 <= enc_x0_r_n ^ 64'h0000000000000001;
                                            enc_round_cnt <= 0;
                                        end else begin
                                            enc_round_cnt <= 0;
                                        end
                                    end else begin
                                        enc_ad_block_cnt <= enc_ad_block_cnt + 1;
                                        enc_round_cnt <= 0;
                                    end
                                end else begin
                                    enc_round_cnt <= enc_round_cnt + 1;
                                end
                            end

                            AD_ROUND2: begin
                                enc_x0 <= enc_x0_r_n; enc_x1 <= enc_x1_r_n; enc_x2 <= enc_x2_r_n;
                                enc_x3 <= enc_x3_r_n; enc_x4 <= enc_x4_r_n;
                                enc_round_cnt <= enc_round_cnt + 1;
                            end

                            DOMAIN_SEP: begin
                                enc_x4 <= enc_x4 ^ DSEP;
                            end

                            WAIT_DATA: begin
                            end

                            DATA_ABSORB: begin
                                enc_data_out_block <= {enc_x0 ^ enc_reordered_block[127:64], enc_x1 ^ enc_reordered_block[63:0]};
                                enc_x0 <= enc_x0 ^ enc_reordered_block[127:64];
                                enc_x1 <= enc_x1 ^ enc_reordered_block[63:0];
                                enc_num_rounds <= 8;
                                enc_round_cnt <= 0;
                            end

                            DATA_PERM: begin
                                if (enc_block_cnt == (NUM_BLOCKS - 1)) begin
                                    enc_x0 <= enc_x0_8_n ^ 64'h0000000000000001;
                                end else begin
                                    enc_x0 <= enc_x0_8_n;
                                end
                                enc_x1 <= enc_x1_8_n;
                                enc_x2 <= enc_x2_8_n;
                                enc_x3 <= enc_x3_8_n;
                                enc_x4 <= enc_x4_8_n;
                            end

                            DATA_ROUND: begin
                                enc_x0 <= enc_x0_r_n; enc_x1 <= enc_x1_r_n; enc_x2 <= enc_x2_r_n;
                                enc_x3 <= enc_x3_r_n; enc_x4 <= enc_x4_r_n;
                                enc_round_cnt <= enc_round_cnt + 1;
                                if (enc_round_cnt == DATA_LIMIT_ENC && enc_block_cnt == (NUM_BLOCKS - 1)) begin
                                    enc_x0 <= enc_x0_r_n ^ 64'h0000000000000001;
                                end
                            end

                            DATA_STORE: begin
                                logic [127:0] output_block;
                                output_block = {
                                    enc_data_out_block[71:64], enc_data_out_block[79:72], enc_data_out_block[87:80], enc_data_out_block[95:88],
                                    enc_data_out_block[103:96],enc_data_out_block[111:104],enc_data_out_block[119:112],enc_data_out_block[127:120],
                                    enc_data_out_block[7:0],   enc_data_out_block[15:8],   enc_data_out_block[23:16], enc_data_out_block[31:24],
                                    enc_data_out_block[39:32], enc_data_out_block[47:40], enc_data_out_block[55:48], enc_data_out_block[63:56]
                                };
                                if (enc_block_cnt < (MESSAGE_SIZE / 128)) begin
                                    enc_padded_data_out_reg[(NUM_BLOCKS * 128 - 1) - (enc_block_cnt * 128) -: 128] <= output_block;
                                end else if (REM_BITS > 0) begin
                                    enc_padded_data_out_reg[(NUM_BLOCKS * 128 - 1) - (enc_block_cnt * 128) -: SAFE_REM_BITS] <= output_block[127 -: SAFE_REM_BITS];
                                end
                                enc_block_cnt <= enc_block_cnt + 1;
                            end

                            FINAL_XOR1: begin
                                enc_x2 <= enc_x2 ^ enc_key[127:64];
                                enc_x3 <= enc_x3 ^ enc_key[63:0];
                                enc_num_rounds <= 12;
                                enc_round_cnt <= 0;
                            end

                            FINAL_PERM: begin
                                enc_x0 <= enc_x0_n; enc_x1 <= enc_x1_n; enc_x2 <= enc_x2_n;
                                enc_x3 <= enc_x3_n; enc_x4 <= enc_x4_n;
                            end

                            FINAL_ROUND: begin
                                enc_x0 <= enc_x0_r_n; enc_x1 <= enc_x1_r_n; enc_x2 <= enc_x2_r_n;
                                enc_x3 <= enc_x3_r_n; enc_x4 <= enc_x4_r_n;
                                enc_round_cnt <= enc_round_cnt + 1;
                            end

                            FINAL_XOR2: begin
                                enc_tag_reg <= {byteswap64(enc_x3 ^ enc_key[127:64]), byteswap64(enc_x4 ^ enc_key[63:0])};
                            end

                            DONE_STATE: begin
                                enc_done_reg <= 1;
                            end
                            default: ;
                        endcase
                    end
                end
            end else begin : gen_no_enc_path
                assign enc_done = 0;
                assign enc_tag = 0;
                assign enc_data_out = 0;
                assign enc_busy_internal = 0;
            end

            // =================================================================
            // DECRYPTION PATH (INDEPENDENT CORES)
            // =================================================================
            if (DECRYPTION_CORE) begin : gen_dec_path
                state_t dec_state, dec_next_state;
                logic [CNT_WIDTH-1:0] dec_block_cnt;
                logic [AD_CNT_WIDTH-1:0] dec_ad_block_cnt;
                logic [3:0] dec_round_cnt;
                logic [3:0] dec_num_rounds;
                
                logic [63:0] dec_x0, dec_x1, dec_x2, dec_x3, dec_x4;
                logic [127:0] dec_data_out_block;
                logic [127:0] dec_reordered_block;
                logic [127:0] dec_reordered_mask;

                // AD Signals
                logic [SAFE_NUM_AD_BLOCKS * 128 - 1 : 0] dec_padded_ad;
                logic [127:0] dec_raw_ad_block;
                logic [63:0]  dec_ad_data_x0, dec_ad_data_x1;
                logic [63:0]  dec_ad_pad_x0,  dec_ad_pad_x1;
                logic         dec_ad_needs_pad0;
                int           dec_rem_ad_bits;

                assign dec_padded_ad = { dec_ad, {(SAFE_NUM_AD_BLOCKS * 128 - AD_SIZE){1'b0}} };

                always_comb begin
                    dec_raw_ad_block = (NUM_AD_BLOCKS > 0) ? dec_padded_ad[(SAFE_NUM_AD_BLOCKS * 128 - 1) - (dec_ad_block_cnt * 128) -: 128] : 128'h0;
                    dec_rem_ad_bits  = AD_SIZE - (dec_ad_block_cnt * 128);
                    
                    if (dec_rem_ad_bits >= 128) begin
                        dec_ad_data_x0    = dec_raw_ad_block[127:64];
                        dec_ad_data_x1    = dec_raw_ad_block[63:0];
                        dec_ad_pad_x0     = 64'h0;
                        dec_ad_pad_x1     = 64'h0;
                        dec_ad_needs_pad0 = 1'b1;
                    end else if (dec_rem_ad_bits >= 64) begin
                        dec_ad_data_x0    = dec_raw_ad_block[127:64];
                        dec_ad_data_x1    = dec_raw_ad_block[63:0];
                        dec_ad_pad_x0     = 64'h0;
                        dec_ad_pad_x1     = (64'h1 << (dec_rem_ad_bits - 64));
                        dec_ad_needs_pad0 = 1'b0;
                    end else if (dec_rem_ad_bits > 0) begin
                        dec_ad_data_x0    = dec_raw_ad_block[127:64];
                        dec_ad_data_x1    = 64'h0;
                        dec_ad_pad_x0     = (64'h1 << dec_rem_ad_bits);
                        dec_ad_pad_x1     = 64'h0;
                        dec_ad_needs_pad0 = 1'b0;
                    end else begin
                        dec_ad_data_x0    = 64'h0;
                        dec_ad_data_x1    = 64'h0;
                        dec_ad_pad_x0     = 64'h0;
                        dec_ad_pad_x1     = 64'h0;
                        dec_ad_needs_pad0 = 1'b0;
                    end
                end

                // Outputs
                logic dec_done_reg;
                logic [127:0] dec_tag_reg;
                logic [MESSAGE_SIZE-1:0] dec_data_out_reg;

                logic [127:0] dec_raw_data_block;
                logic [127:0] dec_raw_mask_block;
                logic dec_run_perm;

                localparam int LOCAL_REM_BITS_DEC = MESSAGE_SIZE % 128;
                logic [127:0] dec_tmp_data;
                logic [127:0] dec_tmp_mask;

                logic [NUM_BLOCKS * 128 - 1 : 0] dec_padded_data_in;
                logic [NUM_BLOCKS * 128 - 1 : 0] dec_padded_data_out_reg;

                assign dec_padded_data_in = { dec_data_in, {(NUM_BLOCKS * 128 - MESSAGE_SIZE){1'b0}} };
                assign dec_data_out_reg   = dec_padded_data_out_reg[NUM_BLOCKS * 128 - 1 -: MESSAGE_SIZE];

                assign dec_reordered_block = {
                    dec_raw_data_block[71:64], dec_raw_data_block[79:72], dec_raw_data_block[87:80], dec_raw_data_block[95:88],
                    dec_raw_data_block[103:96],dec_raw_data_block[111:104],dec_raw_data_block[119:112],dec_raw_data_block[127:120],
                    dec_raw_data_block[7:0],   dec_raw_data_block[15:8],   dec_raw_data_block[23:16], dec_raw_data_block[31:24],
                    dec_raw_data_block[39:32], dec_raw_data_block[47:40], dec_raw_data_block[55:48], dec_raw_data_block[63:56]
                };
                assign dec_reordered_mask = {
                    dec_raw_mask_block[71:64], dec_raw_mask_block[79:72], dec_raw_mask_block[87:80], dec_raw_mask_block[95:88],
                    dec_raw_mask_block[103:96],dec_raw_mask_block[111:104],dec_raw_mask_block[119:112],dec_raw_mask_block[127:120],
                    dec_raw_mask_block[7:0],   dec_raw_mask_block[15:8],   dec_raw_mask_block[23:16], dec_raw_mask_block[31:24],
                    dec_raw_mask_block[39:32], dec_raw_mask_block[47:40], dec_raw_mask_block[55:48], dec_raw_mask_block[63:56]
                };

                assign dec_run_perm = (dec_block_cnt < (MESSAGE_SIZE / 128));
                assign dec_done = dec_done_reg;
                assign dec_tag  = dec_tag_reg;
                assign dec_data_out = dec_data_out_reg;
                assign dec_busy_internal = (dec_state != IDLE);

                always_comb begin
                    dec_raw_data_block = 128'h0;
                    dec_raw_mask_block = 128'h0;
                    dec_tmp_data = 128'h0;
                    dec_tmp_mask = 128'h0;
                    if (dec_block_cnt < NUM_BLOCKS) begin
                        if (dec_block_cnt < (MESSAGE_SIZE / 128)) begin
                            dec_raw_data_block = dec_padded_data_in[(NUM_BLOCKS * 128 - 1) - (dec_block_cnt * 128) -: 128];
                            dec_raw_mask_block = {128{1'b1}};
                        end else begin
                            for (int bit_idx = 0; bit_idx < 128; bit_idx++) begin
                                if (bit_idx < LOCAL_REM_BITS_DEC) begin
                                    dec_tmp_data[127 - bit_idx] = dec_padded_data_in[(NUM_BLOCKS * 128 - 1) - bit_idx];
                                    dec_tmp_mask[127 - bit_idx] = 1'b1;
                                end else if (bit_idx == (LOCAL_REM_BITS_DEC + 7)) begin
                                    dec_tmp_data[127 - bit_idx] = 1'b1;
                                    dec_tmp_mask[127 - bit_idx] = 1'b0;
                                end else begin
                                    dec_tmp_data[127 - bit_idx] = 1'b0;
                                    dec_tmp_mask[127 - bit_idx] = 1'b0;
                                end
                            end
                            dec_raw_data_block = dec_tmp_data;
                            dec_raw_mask_block = dec_tmp_mask;
                        end
                    end
                end

                // Instantiate permutations/rounds for Decryption
                logic [63:0] dec_x0_n, dec_x1_n, dec_x2_n, dec_x3_n, dec_x4_n;          
                logic [63:0] dec_x0_8_n, dec_x1_8_n, dec_x2_8_n, dec_x3_8_n, dec_x4_8_n; 
                logic [63:0] dec_x0_r_n, dec_x1_r_n, dec_x2_r_n, dec_x3_r_n, dec_x4_r_n; 

                localparam bit ACCELERATED_DEC = (ACCELERATED_DEC_LEVEL == 3);
                localparam int ROUNDS_PER_CYCLE_DEC = (ACCELERATED_DEC_LEVEL == 0) ? 1 : 
                                                      (ACCELERATED_DEC_LEVEL == 1) ? 2 : 4;
                localparam int INIT_LIMIT_DEC = (12 / ROUNDS_PER_CYCLE_DEC) - 1;
                localparam int DATA_LIMIT_DEC = (8 / ROUNDS_PER_CYCLE_DEC) - 1;

                if (ACCELERATED_DEC) begin : gen_dec_acc
                    ascon_permutation #(.NUM_ROUNDS(12)) u_dec_perm_12 (
                        .x0_in(dec_x0), .x1_in(dec_x1), .x2_in(dec_x2), .x3_in(dec_x3), .x4_in(dec_x4),
                        .x0_out(dec_x0_n), .x1_out(dec_x1_n), .x2_out(dec_x2_n), .x3_out(dec_x3_n), .x4_out(dec_x4_n)
                    );
                    ascon_permutation #(.NUM_ROUNDS(8)) u_dec_perm_8 (
                        .x0_in(dec_x0), .x1_in(dec_x1), .x2_in(dec_x2), .x3_in(dec_x3), .x4_in(dec_x4),
                        .x0_out(dec_x0_8_n), .x1_out(dec_x1_8_n), .x2_out(dec_x2_8_n), .x3_out(dec_x3_8_n), .x4_out(dec_x4_8_n)
                    );
                    assign dec_x0_r_n = 0; assign dec_x1_r_n = 0; assign dec_x2_r_n = 0; assign dec_x3_r_n = 0; assign dec_x4_r_n = 0;
                end else begin : gen_dec_seq
                    logic [7:0] dec_rc0, dec_rc1, dec_rc2, dec_rc3;
                    if (ACCELERATED_DEC_LEVEL == 0) begin : gen_dec_seq_l0
                        always_comb begin
                            dec_rc0 = (dec_num_rounds == 12) ? RC12[dec_round_cnt] : RC8[dec_round_cnt];
                        end
                        ascon_round u_dec_round (
                            .x0_in(dec_x0), .x1_in(dec_x1), .x2_in(dec_x2), .x3_in(dec_x3), .x4_in(dec_x4),
                            .rc(dec_rc0),
                            .x0_out(dec_x0_r_n), .x1_out(dec_x1_r_n), .x2_out(dec_x2_r_n), .x3_out(dec_x3_r_n), .x4_out(dec_x4_r_n)
                        );
                    end else if (ACCELERATED_DEC_LEVEL == 1) begin : gen_dec_seq_l1
                        always_comb begin
                            if (dec_num_rounds == 12) begin
                                dec_rc0 = RC12[2 * dec_round_cnt];
                                dec_rc1 = RC12[2 * dec_round_cnt + 1];
                            end else begin
                                dec_rc0 = RC8[2 * dec_round_cnt];
                                dec_rc1 = RC8[2 * dec_round_cnt + 1];
                            end
                        end
                        ascon_round_2 u_dec_round_2 (
                            .x0_in(dec_x0), .x1_in(dec_x1), .x2_in(dec_x2), .x3_in(dec_x3), .x4_in(dec_x4),
                            .rc0(dec_rc0), .rc1(dec_rc1),
                            .x0_out(dec_x0_r_n), .x1_out(dec_x1_r_n), .x2_out(dec_x2_r_n), .x3_out(dec_x3_r_n), .x4_out(dec_x4_r_n)
                        );
                    end else if (ACCELERATED_DEC_LEVEL == 2) begin : gen_dec_seq_l2
                        always_comb begin
                            if (dec_num_rounds == 12) begin
                                dec_rc0 = RC12[4 * dec_round_cnt];
                                dec_rc1 = RC12[4 * dec_round_cnt + 1];
                                dec_rc2 = RC12[4 * dec_round_cnt + 2];
                                dec_rc3 = RC12[4 * dec_round_cnt + 3];
                            end else begin
                                dec_rc0 = RC8[4 * dec_round_cnt];
                                dec_rc1 = RC8[4 * dec_round_cnt + 1];
                                dec_rc2 = RC8[4 * dec_round_cnt + 2];
                                dec_rc3 = RC8[4 * dec_round_cnt + 3];
                            end
                        end
                        ascon_round_4 u_dec_round_4 (
                            .x0_in(dec_x0), .x1_in(dec_x1), .x2_in(dec_x2), .x3_in(dec_x3), .x4_in(dec_x4),
                            .rc0(dec_rc0), .rc1(dec_rc1), .rc2(dec_rc2), .rc3(dec_rc3),
                            .x0_out(dec_x0_r_n), .x1_out(dec_x1_r_n), .x2_out(dec_x2_r_n), .x3_out(dec_x3_r_n), .x4_out(dec_x4_r_n)
                        );
                    end
                    assign dec_x0_n = 0; assign dec_x1_n = 0; assign dec_x2_n = 0; assign dec_x3_n = 0; assign dec_x4_n = 0;
                    assign dec_x0_8_n = 0; assign dec_x1_8_n = 0; assign dec_x2_8_n = 0; assign dec_x3_8_n = 0; assign dec_x4_8_n = 0;
                end

                // FSM transition logic
                always_comb begin
                    dec_next_state = dec_state;
                    case (dec_state)
                        IDLE:       if (dec_start) dec_next_state = INIT_LOAD;
                        INIT_LOAD:  dec_next_state = (ACCELERATED_DEC) ? INIT_PERM : INIT_ROUND;
                        INIT_PERM:  dec_next_state = INIT_XOR;
                        INIT_ROUND: if (dec_round_cnt == INIT_LIMIT_DEC) dec_next_state = INIT_XOR;
                        
                        INIT_XOR:   dec_next_state = (NUM_AD_BLOCKS == 0) ? DOMAIN_SEP : AD_ABSORB;
                        
                        AD_ABSORB:  dec_next_state = (ACCELERATED_DEC) ? AD_PERM1 : AD_ROUND;
                        
                        AD_PERM1:   if (dec_ad_block_cnt == (NUM_AD_BLOCKS - 1)) begin
                                        if (dec_ad_needs_pad0) dec_next_state = AD_PERM2;
                                        else dec_next_state = DOMAIN_SEP;
                                    end else begin
                                        dec_next_state = AD_ABSORB;
                                    end
                        AD_PERM2:   dec_next_state = DOMAIN_SEP;
                        
                        AD_ROUND:   if (dec_round_cnt == DATA_LIMIT_DEC) begin
                                        if (dec_ad_block_cnt == (NUM_AD_BLOCKS - 1)) begin
                                            if (dec_ad_needs_pad0) dec_next_state = AD_ROUND2;
                                            else dec_next_state = DOMAIN_SEP;
                                        end else begin
                                            dec_next_state = AD_ABSORB;
                                        end
                                    end
                        AD_ROUND2:  if (dec_round_cnt == DATA_LIMIT_DEC) dec_next_state = DOMAIN_SEP;
                        
                        DOMAIN_SEP: dec_next_state = (dec_data_rdy) ? DATA_ABSORB : WAIT_DATA;
                        WAIT_DATA:  if (dec_data_rdy) dec_next_state = DATA_ABSORB;
                        
                        DATA_ABSORB:dec_next_state = (dec_run_perm) ? ((ACCELERATED_DEC) ? DATA_PERM : DATA_ROUND) : DATA_STORE;
                        DATA_PERM:  dec_next_state = DATA_STORE;
                        DATA_ROUND: if (dec_round_cnt == DATA_LIMIT_DEC) dec_next_state = DATA_STORE;
                        
                        DATA_STORE: if (dec_block_cnt == (NUM_BLOCKS - 1)) dec_next_state = FINAL_XOR1;
                                    else dec_next_state = (dec_data_rdy) ? DATA_ABSORB : WAIT_DATA;
                        
                        FINAL_XOR1: dec_next_state = (ACCELERATED_DEC) ? FINAL_PERM : FINAL_ROUND;
                        FINAL_PERM: dec_next_state = FINAL_XOR2;
                        FINAL_ROUND:if (dec_round_cnt == INIT_LIMIT_DEC) dec_next_state = FINAL_XOR2;
                        FINAL_XOR2: dec_next_state = DONE_STATE;
                        DONE_STATE: dec_next_state = IDLE;
                        default:    dec_next_state = IDLE;
                    endcase
                end

                // FSM sequential logic
                always_ff @(posedge clk or negedge rst_n) begin
                    if (!rst_n) begin
                        dec_state <= IDLE;
                        dec_block_cnt <= 0;
                        dec_ad_block_cnt <= 0;
                        dec_round_cnt <= 0;
                        dec_num_rounds <= 12;
                        dec_x0 <= 0; dec_x1 <= 0; dec_x2 <= 0; dec_x3 <= 0; dec_x4 <= 0;
                        dec_done_reg <= 0;
                        dec_tag_reg <= 0;
                        dec_padded_data_out_reg <= 0;
                        dec_data_out_block <= 0;
                    end else begin
                        dec_state <= dec_next_state;
                        case (dec_state)
                            IDLE: begin
                                dec_done_reg <= 0;
                                dec_block_cnt <= 0;
                                dec_ad_block_cnt <= 0;
                                dec_round_cnt <= 0;
                            end

                            INIT_LOAD: begin
                                dec_x0 <= IV;
                                dec_x1 <= dec_key[127:64];
                                dec_x2 <= dec_key[63:0];
                                dec_x3 <= dec_nonce[127:64];
                                dec_x4 <= dec_nonce[63:0];
                                dec_num_rounds <= 12;
                                dec_round_cnt <= 0;
                                dec_ad_block_cnt <= 0;
                            end

                            INIT_PERM: begin
                                dec_x0 <= dec_x0_n; dec_x1 <= dec_x1_n; dec_x2 <= dec_x2_n;
                                dec_x3 <= dec_x3_n; dec_x4 <= dec_x4_n;
                            end

                            INIT_ROUND: begin
                                dec_x0 <= dec_x0_r_n; dec_x1 <= dec_x1_r_n; dec_x2 <= dec_x2_r_n;
                                dec_x3 <= dec_x3_r_n; dec_x4 <= dec_x4_r_n;
                                dec_round_cnt <= dec_round_cnt + 1;
                            end

                            INIT_XOR: begin
                                dec_x3 <= dec_x3 ^ dec_key[127:64];
                                dec_x4 <= dec_x4 ^ dec_key[63:0];
                                dec_round_cnt <= 0;
                                dec_ad_block_cnt <= 0;
                            end

                            AD_ABSORB: begin
                                dec_x0 <= dec_x0 ^ dec_ad_data_x0 ^ dec_ad_pad_x0;
                                dec_x1 <= dec_x1 ^ dec_ad_data_x1 ^ dec_ad_pad_x1;
                                dec_num_rounds <= 8;
                                dec_round_cnt <= 0;
                            end

                            AD_PERM1: begin
                                if (dec_ad_block_cnt == (NUM_AD_BLOCKS - 1)) begin
                                    if (dec_ad_needs_pad0) dec_x0 <= dec_x0_8_n ^ 64'h0000000000000001;
                                    else dec_x0 <= dec_x0_8_n;
                                end else begin
                                    dec_x0 <= dec_x0_8_n;
                                end
                                dec_x1 <= dec_x1_8_n;
                                dec_x2 <= dec_x2_8_n;
                                dec_x3 <= dec_x3_8_n;
                                dec_x4 <= dec_x4_8_n;
                                
                                if (dec_ad_block_cnt < (NUM_AD_BLOCKS - 1)) begin
                                    dec_ad_block_cnt <= dec_ad_block_cnt + 1;
                                end
                            end

                            AD_PERM2: begin
                                dec_x0 <= dec_x0_8_n;
                                dec_x1 <= dec_x1_8_n;
                                dec_x2 <= dec_x2_8_n;
                                dec_x3 <= dec_x3_8_n;
                                dec_x4 <= dec_x4_8_n;
                            end

                            AD_ROUND: begin
                                dec_x0 <= dec_x0_r_n; dec_x1 <= dec_x1_r_n; dec_x2 <= dec_x2_r_n;
                                dec_x3 <= dec_x3_r_n; dec_x4 <= dec_x4_r_n;
                                if (dec_round_cnt == DATA_LIMIT_DEC) begin
                                    if (dec_ad_block_cnt == (NUM_AD_BLOCKS - 1)) begin
                                        if (dec_ad_needs_pad0) begin
                                            dec_x0 <= dec_x0_r_n ^ 64'h0000000000000001;
                                            dec_round_cnt <= 0;
                                        end else begin
                                            dec_round_cnt <= 0;
                                        end
                                    end else begin
                                        dec_ad_block_cnt <= dec_ad_block_cnt + 1;
                                        dec_round_cnt <= 0;
                                    end
                                end else begin
                                    dec_round_cnt <= dec_round_cnt + 1;
                                end
                            end

                            AD_ROUND2: begin
                                dec_x0 <= dec_x0_r_n; dec_x1 <= dec_x1_r_n; dec_x2 <= dec_x2_r_n;
                                dec_x3 <= dec_x3_r_n; dec_x4 <= dec_x4_r_n;
                                dec_round_cnt <= dec_round_cnt + 1;
                            end

                            DOMAIN_SEP: begin
                                dec_x4 <= dec_x4 ^ DSEP;
                            end

                            WAIT_DATA: begin
                            end

                            DATA_ABSORB: begin
                                dec_data_out_block <= {dec_x0 ^ dec_reordered_block[127:64], dec_x1 ^ dec_reordered_block[63:0]};
                                dec_x0 <= (dec_x0 & ~dec_reordered_mask[127:64]) ^ dec_reordered_block[127:64];
                                dec_x1 <= (dec_x1 & ~dec_reordered_mask[63:0]) ^ dec_reordered_block[63:0];
                                dec_num_rounds <= 8;
                                dec_round_cnt <= 0;
                            end

                            DATA_PERM: begin
                                if (dec_block_cnt == (NUM_BLOCKS - 1)) begin
                                    dec_x0 <= dec_x0_8_n ^ 64'h0000000000000001;
                                end else begin
                                    dec_x0 <= dec_x0_8_n;
                                end
                                dec_x1 <= dec_x1_8_n;
                                dec_x2 <= dec_x2_8_n;
                                dec_x3 <= dec_x3_8_n;
                                dec_x4 <= dec_x4_8_n;
                            end

                            DATA_ROUND: begin
                                dec_x0 <= dec_x0_r_n; dec_x1 <= dec_x1_r_n; dec_x2 <= dec_x2_r_n;
                                dec_x3 <= dec_x3_r_n; dec_x4 <= dec_x4_r_n;
                                dec_round_cnt <= dec_round_cnt + 1;
                                if (dec_round_cnt == DATA_LIMIT_DEC && dec_block_cnt == (NUM_BLOCKS - 1)) begin
                                    dec_x0 <= dec_x0_r_n ^ 64'h0000000000000001;
                                end
                            end

                            DATA_STORE: begin
                                logic [127:0] output_block;
                                output_block = {
                                    dec_data_out_block[71:64], dec_data_out_block[79:72], dec_data_out_block[87:80], dec_data_out_block[95:88],
                                    dec_data_out_block[103:96],dec_data_out_block[111:104],dec_data_out_block[119:112],dec_data_out_block[127:120],
                                    dec_data_out_block[7:0],   dec_data_out_block[15:8],   dec_data_out_block[23:16], dec_data_out_block[31:24],
                                    dec_data_out_block[39:32], dec_data_out_block[47:40], dec_data_out_block[55:48], dec_data_out_block[63:56]
                                };
                                if (dec_block_cnt < (MESSAGE_SIZE / 128)) begin
                                    dec_padded_data_out_reg[(NUM_BLOCKS * 128 - 1) - (dec_block_cnt * 128) -: 128] <= output_block;
                                end else if (REM_BITS > 0) begin
                                    dec_padded_data_out_reg[(NUM_BLOCKS * 128 - 1) - (dec_block_cnt * 128) -: SAFE_REM_BITS] <= output_block[127 -: SAFE_REM_BITS];
                                end
                                dec_block_cnt <= dec_block_cnt + 1;
                            end

                            FINAL_XOR1: begin
                                dec_x2 <= dec_x2 ^ dec_key[127:64];
                                dec_x3 <= dec_x3 ^ dec_key[63:0];
                                dec_num_rounds <= 12;
                                dec_round_cnt <= 0;
                            end

                            FINAL_PERM: begin
                                dec_x0 <= dec_x0_n; dec_x1 <= dec_x1_n; dec_x2 <= dec_x2_n;
                                dec_x3 <= dec_x3_n; dec_x4 <= dec_x4_n;
                            end

                            FINAL_ROUND: begin
                                dec_x0 <= dec_x0_r_n; dec_x1 <= dec_x1_r_n; dec_x2 <= dec_x2_r_n;
                                dec_x3 <= dec_x3_r_n; dec_x4 <= dec_x4_r_n;
                                dec_round_cnt <= dec_round_cnt + 1;
                            end

                            FINAL_XOR2: begin
                                dec_tag_reg <= {byteswap64(dec_x3 ^ dec_key[127:64]), byteswap64(dec_x4 ^ dec_key[63:0])};
                            end

                            DONE_STATE: begin
                                dec_done_reg <= 1;
                            end
                            default: ;
                        endcase
                    end
                end
            end else begin : gen_no_dec_path
                assign dec_done = 0;
                assign dec_tag = 0;
                assign dec_data_out = 0;
                assign dec_busy_internal = 0;
            end

        end else begin : gen_shared
            
            // =================================================================
            // SHARED PATH (NON-PARALLEL)
            // =================================================================
            state_t state, next_state;
            logic active_encrypt;

            logic [CNT_WIDTH-1:0] block_cnt;
            logic [AD_CNT_WIDTH-1:0] ad_block_cnt;
            logic [3:0] round_cnt;
            logic [3:0] num_rounds;

            logic [63:0] x0, x1, x2, x3, x4;
            logic [127:0] data_out_block;
            logic [127:0] reordered_block;
            logic [127:0] reordered_mask;
            logic run_perm;
            assign run_perm = (block_cnt < (MESSAGE_SIZE / 128));

            // AD Signals for Shared Core
            logic [SAFE_NUM_AD_BLOCKS * 128 - 1 : 0] shared_padded_ad;
            logic [127:0] shared_raw_ad_block;
            logic [63:0]  shared_ad_data_x0, shared_ad_data_x1;
            logic [63:0]  shared_ad_pad_x0,  shared_ad_pad_x1;
            logic         shared_ad_needs_pad0;
            int           shared_rem_ad_bits;

            assign shared_padded_ad = (active_encrypt) ?
                                      { enc_ad, {(SAFE_NUM_AD_BLOCKS * 128 - AD_SIZE){1'b0}} } :
                                      { dec_ad, {(SAFE_NUM_AD_BLOCKS * 128 - AD_SIZE){1'b0}} };

            always_comb begin
                shared_raw_ad_block = (NUM_AD_BLOCKS > 0) ? shared_padded_ad[(SAFE_NUM_AD_BLOCKS * 128 - 1) - (ad_block_cnt * 128) -: 128] : 128'h0;
                shared_rem_ad_bits  = AD_SIZE - (ad_block_cnt * 128);
                
                if (shared_rem_ad_bits >= 128) begin
                    shared_ad_data_x0    = shared_raw_ad_block[127:64];
                    shared_ad_data_x1    = shared_raw_ad_block[63:0];
                    shared_ad_pad_x0     = 64'h0;
                    shared_ad_pad_x1     = 64'h0;
                    shared_ad_needs_pad0 = 1'b1;
                end else if (shared_rem_ad_bits >= 64) begin
                    shared_ad_data_x0    = shared_raw_ad_block[127:64];
                    shared_ad_data_x1    = shared_raw_ad_block[63:0];
                    shared_ad_pad_x0     = 64'h0;
                    shared_ad_pad_x1     = (64'h1 << (shared_rem_ad_bits - 64));
                    shared_ad_needs_pad0 = 1'b0;
                end else if (shared_rem_ad_bits > 0) begin
                    shared_ad_data_x0    = shared_raw_ad_block[127:64];
                    shared_ad_data_x1    = 64'h0;
                    shared_ad_pad_x0     = (64'h1 << shared_rem_ad_bits);
                    shared_ad_pad_x1     = 64'h0;
                    shared_ad_needs_pad0 = 1'b0;
                end else begin
                    shared_ad_data_x0    = 64'h0;
                    shared_ad_data_x1    = 64'h0;
                    shared_ad_pad_x0     = 64'h0;
                    shared_ad_pad_x1     = 64'h0;
                    shared_ad_needs_pad0 = 1'b0;
                end
            end

            // Padded signals and safe logic for SIOB-safe bit indexing
            logic [NUM_BLOCKS * 128 - 1 : 0] shared_padded_data_in;
            logic [NUM_BLOCKS * 128 - 1 : 0] shared_padded_data_out_reg;

            logic [127:0] data_block;
            logic [127:0] data_mask;
            localparam int LOCAL_REM_BITS_SHARED = MESSAGE_SIZE % 128;
            logic [127:0] shared_tmp_data;
            logic [127:0] shared_tmp_mask;

            assign shared_padded_data_in = (active_encrypt) ? 
                                           { enc_data_in, {(NUM_BLOCKS * 128 - MESSAGE_SIZE){1'b0}} } :
                                           { dec_data_in, {(NUM_BLOCKS * 128 - MESSAGE_SIZE){1'b0}} };

            always_comb begin
                data_block = 128'h0;
                data_mask = 128'h0;
                shared_tmp_data = 128'h0;
                shared_tmp_mask = 128'h0;
                if (block_cnt < NUM_BLOCKS) begin
                    if (block_cnt < (MESSAGE_SIZE / 128)) begin
                        data_block = shared_padded_data_in[(NUM_BLOCKS * 128 - 1) - (block_cnt * 128) -: 128];
                        data_mask = {128{1'b1}};
                    end else begin
                        for (int bit_idx = 0; bit_idx < 128; bit_idx++) begin
                            if (bit_idx < LOCAL_REM_BITS_SHARED) begin
                                shared_tmp_data[127 - bit_idx] = shared_padded_data_in[(NUM_BLOCKS * 128 - 1) - bit_idx];
                                shared_tmp_mask[127 - bit_idx] = 1'b1;
                            end else if (bit_idx == (LOCAL_REM_BITS_SHARED + 7)) begin
                                shared_tmp_data[127 - bit_idx] = 1'b1;
                                shared_tmp_mask[127 - bit_idx] = 1'b0;
                            end else begin
                                shared_tmp_data[127 - bit_idx] = 1'b0;
                                shared_tmp_mask[127 - bit_idx] = 1'b0;
                            end
                        end
                        data_block = shared_tmp_data;
                        data_mask = shared_tmp_mask;
                    end
                end
            end

            assign reordered_block = {
                data_block[71:64], data_block[79:72], data_block[87:80], data_block[95:88],
                data_block[103:96],data_block[111:104],data_block[119:112],data_block[127:120],
                data_block[7:0],   data_block[15:8],   data_block[23:16], data_block[31:24],
                data_block[39:32], data_block[47:40], data_block[55:48], data_block[63:56]
            };
            assign reordered_mask = {
                data_mask[71:64], data_mask[79:72], data_mask[87:80], data_mask[95:88],
                data_mask[103:96],data_mask[111:104],data_mask[119:112],data_mask[127:120],
                data_mask[7:0],   data_mask[15:8],   data_mask[23:16], data_mask[31:24],
                data_mask[39:32], data_mask[47:40], data_mask[55:48], data_mask[63:56]
            };

            // Outputs
            logic done;
            logic [127:0] tag;
            logic [MESSAGE_SIZE-1:0] data_out;
            assign data_out = shared_padded_data_out_reg[NUM_BLOCKS * 128 - 1 -: MESSAGE_SIZE];

            // Direct route to external interface
            assign enc_done = (active_encrypt && ENCRYPTION_CORE) ? done : 1'b0;
            assign enc_tag  = (active_encrypt && ENCRYPTION_CORE) ? tag  : 128'b0;
            assign enc_data_out = (active_encrypt && ENCRYPTION_CORE) ? data_out : {MESSAGE_SIZE{1'b0}};

            assign dec_done = (!active_encrypt && DECRYPTION_CORE) ? done : 1'b0;
            assign dec_tag  = (!active_encrypt && DECRYPTION_CORE) ? tag  : 128'b0;
            assign dec_data_out = (!active_encrypt && DECRYPTION_CORE) ? data_out : {MESSAGE_SIZE{1'b0}};
            
            assign enc_busy_internal = (state != IDLE) && active_encrypt;
            assign dec_busy_internal = (state != IDLE) && !active_encrypt;

            // Determine active acceleration
            logic current_accelerated;
            logic [2:0] rounds_per_cycle;
            logic [3:0] init_limit;
            logic [3:0] data_limit;

            always_comb begin
                if (active_encrypt) begin
                    current_accelerated = (ACCELERATED_ENC_LEVEL == 3);
                    rounds_per_cycle = (ACCELERATED_ENC_LEVEL == 0) ? 1 : 
                                       (ACCELERATED_ENC_LEVEL == 1) ? 2 : 4;
                end else begin
                    current_accelerated = (ACCELERATED_DEC_LEVEL == 3);
                    rounds_per_cycle = (ACCELERATED_DEC_LEVEL == 0) ? 1 : 
                                       (ACCELERATED_DEC_LEVEL == 1) ? 2 : 4;
                end
                init_limit = (12 / rounds_per_cycle) - 1;
                data_limit = (8 / rounds_per_cycle) - 1;
            end

            localparam bit NEED_ACCELERATED_SHARED = (ENCRYPTION_CORE && ACCELERATED_ENC_LEVEL == 3) || (DECRYPTION_CORE && ACCELERATED_DEC_LEVEL == 3);
            localparam bit NEED_ROUND_SHARED  = (ENCRYPTION_CORE && ACCELERATED_ENC_LEVEL == 0) || (DECRYPTION_CORE && ACCELERATED_DEC_LEVEL == 0);
            localparam bit NEED_ROUND2_SHARED = (ENCRYPTION_CORE && ACCELERATED_ENC_LEVEL == 1) || (DECRYPTION_CORE && ACCELERATED_DEC_LEVEL == 1);
            localparam bit NEED_ROUND4_SHARED = (ENCRYPTION_CORE && ACCELERATED_ENC_LEVEL == 2) || (DECRYPTION_CORE && ACCELERATED_DEC_LEVEL == 2);

            logic [63:0] x0_n, x1_n, x2_n, x3_n, x4_n;          // 12-round
            logic [63:0] x0_8_n, x1_8_n, x2_8_n, x3_8_n, x4_8_n; // 8-round

            if (NEED_ACCELERATED_SHARED) begin : gen_shared_acc
                ascon_permutation #(.NUM_ROUNDS(12)) u_shared_perm_12 (
                    .x0_in(x0), .x1_in(x1), .x2_in(x2), .x3_in(x3), .x4_in(x4),
                    .x0_out(x0_n), .x1_out(x1_n), .x2_out(x2_n), .x3_out(x3_n), .x4_out(x4_n)
                );
                ascon_permutation #(.NUM_ROUNDS(8)) u_shared_perm_8 (
                    .x0_in(x0), .x1_in(x1), .x2_in(x2), .x3_in(x3), .x4_in(x4),
                    .x0_out(x0_8_n), .x1_out(x1_8_n), .x2_out(x2_8_n), .x3_out(x3_8_n), .x4_out(x4_8_n)
                );
            end else begin : gen_shared_no_acc
                assign x0_n = 0; assign x1_n = 0; assign x2_n = 0; assign x3_n = 0; assign x4_n = 0;
                assign x0_8_n = 0; assign x1_8_n = 0; assign x2_8_n = 0; assign x3_8_n = 0; assign x4_8_n = 0;
            end

            logic [63:0] x0_r1, x1_r1, x2_r1, x3_r1, x4_r1;
            logic [63:0] x0_r2, x1_r2, x2_r2, x3_r2, x4_r2;
            logic [63:0] x0_r4, x1_r4, x2_r4, x3_r4, x4_r4;

            if (NEED_ROUND_SHARED) begin : gen_shared_r1
                logic [7:0] rc0;
                always_comb begin
                    rc0 = (num_rounds == 12) ? RC12[round_cnt] : RC8[round_cnt];
                end
                ascon_round u_round (
                    .x0_in(x0), .x1_in(x1), .x2_in(x2), .x3_in(x3), .x4_in(x4),
                    .rc(rc0),
                    .x0_out(x0_r1), .x1_out(x1_r1), .x2_out(x2_r1), .x3_out(x3_r1), .x4_out(x4_r1)
                );
            end else begin : gen_shared_no_r1
                assign x0_r1 = 0; assign x1_r1 = 0; assign x2_r1 = 0; assign x3_r1 = 0; assign x4_r1 = 0;
            end

            if (NEED_ROUND2_SHARED) begin : gen_shared_r2
                logic [7:0] rc0, rc1;
                always_comb begin
                    rc0 = (num_rounds == 12) ? RC12[2 * round_cnt] : RC8[2 * round_cnt];
                    rc1 = (num_rounds == 12) ? RC12[2 * round_cnt + 1] : RC8[2 * round_cnt + 1];
                end
                ascon_round_2 u_round2 (
                    .x0_in(x0), .x1_in(x1), .x2_in(x2), .x3_in(x3), .x4_in(x4),
                    .rc0(rc0), .rc1(rc1),
                    .x0_out(x0_r2), .x1_out(x1_r2), .x2_out(x2_r2), .x3_out(x3_r2), .x4_out(x4_r2)
                );
            end else begin : gen_shared_no_r2
                assign x0_r2 = 0; assign x1_r2 = 0; assign x2_r2 = 0; assign x3_r2 = 0; assign x4_r2 = 0;
            end

            if (NEED_ROUND4_SHARED) begin : gen_shared_r4
                logic [7:0] rc0, rc1, rc2, rc3;
                always_comb begin
                    rc0 = (num_rounds == 12) ? RC12[4 * round_cnt] : RC8[4 * round_cnt];
                    rc1 = (num_rounds == 12) ? RC12[4 * round_cnt + 1] : RC8[4 * round_cnt + 1];
                    rc2 = (num_rounds == 12) ? RC12[4 * round_cnt + 2] : RC8[4 * round_cnt + 2];
                    rc3 = (num_rounds == 12) ? RC12[4 * round_cnt + 3] : RC8[4 * round_cnt + 3];
                end
                ascon_round_4 u_round4 (
                    .x0_in(x0), .x1_in(x1), .x2_in(x2), .x3_in(x3), .x4_in(x4),
                    .rc0(rc0), .rc1(rc1), .rc2(rc2), .rc3(rc3),
                    .x0_out(x0_r4), .x1_out(x1_r4), .x2_out(x2_r4), .x3_out(x3_r4), .x4_out(x4_r4)
                );
            end else begin : gen_shared_no_r4
                assign x0_r4 = 0; assign x1_r4 = 0; assign x2_r4 = 0; assign x3_r4 = 0; assign x4_r4 = 0;
            end

            logic [63:0] x0_r_n, x1_r_n, x2_r_n, x3_r_n, x4_r_n;

            // Runtime multiplexing of sequential round outputs
            always_comb begin
                x0_r_n = 0; x1_r_n = 0; x2_r_n = 0; x3_r_n = 0; x4_r_n = 0;
                if (rounds_per_cycle == 1) begin
                    x0_r_n = x0_r1; x1_r_n = x1_r1; x2_r_n = x2_r1; x3_r_n = x3_r1; x4_r_n = x4_r1;
                end else if (rounds_per_cycle == 2) begin
                    x0_r_n = x0_r2; x1_r_n = x1_r2; x2_r_n = x2_r2; x3_r_n = x3_r2; x4_r_n = x4_r2;
                end else if (rounds_per_cycle == 4) begin
                    x0_r_n = x0_r4; x1_r_n = x1_r4; x2_r_n = x2_r4; x3_r_n = x3_r4; x4_r_n = x4_r4;
                end
            end

            // Shared transition logic
            always_comb begin
                next_state = state;
                case (state)
                    IDLE: begin
                        if (ENCRYPTION_CORE && enc_start)
                            next_state = INIT_LOAD;
                        else if (DECRYPTION_CORE && dec_start)
                            next_state = INIT_LOAD;
                    end
                    INIT_LOAD:      next_state = (current_accelerated) ? INIT_PERM : INIT_ROUND;
                    INIT_PERM:      next_state = INIT_XOR;
                    INIT_ROUND:     if (round_cnt == init_limit) next_state = INIT_XOR;
                    
                    INIT_XOR:       next_state = (NUM_AD_BLOCKS == 0) ? DOMAIN_SEP : AD_ABSORB;
                    
                    AD_ABSORB:      next_state = (current_accelerated) ? AD_PERM1 : AD_ROUND;
                    
                    AD_PERM1:       if (ad_block_cnt == (NUM_AD_BLOCKS - 1)) begin
                                        if (shared_ad_needs_pad0) next_state = AD_PERM2;
                                        else next_state = DOMAIN_SEP;
                                    end else begin
                                        next_state = AD_ABSORB;
                                    end
                    AD_PERM2:       next_state = DOMAIN_SEP;
                    
                    AD_ROUND:       if (round_cnt == data_limit) begin
                                        if (ad_block_cnt == (NUM_AD_BLOCKS - 1)) begin
                                            if (shared_ad_needs_pad0) next_state = AD_ROUND2;
                                            else next_state = DOMAIN_SEP;
                                        end else begin
                                            next_state = AD_ABSORB;
                                        end
                                    end
                    AD_ROUND2:      if (round_cnt == data_limit) next_state = DOMAIN_SEP;
                    
                    DOMAIN_SEP:     next_state = (active_encrypt ? enc_data_rdy : dec_data_rdy) ? DATA_ABSORB : WAIT_DATA;
                    WAIT_DATA:      if (active_encrypt) begin
                                        if (enc_data_rdy) next_state = DATA_ABSORB;
                                        else next_state = WAIT_DATA;
                                    end else begin
                                        if (dec_data_rdy) next_state = DATA_ABSORB;
                                        else next_state = WAIT_DATA;
                                    end
                    DATA_ABSORB:    next_state = (run_perm) ? ((current_accelerated) ? DATA_PERM : DATA_ROUND) : DATA_STORE;
                    DATA_PERM:      next_state = DATA_STORE;
                    DATA_ROUND:     if (round_cnt == data_limit) next_state = DATA_STORE;
                    DATA_STORE:     if (block_cnt == (NUM_BLOCKS - 1)) next_state = FINAL_XOR1;
                                    else next_state = (active_encrypt ? enc_data_rdy : dec_data_rdy) ? DATA_ABSORB : WAIT_DATA;
                    FINAL_XOR1:     next_state = (current_accelerated) ? FINAL_PERM : FINAL_ROUND;
                    FINAL_PERM:     next_state = FINAL_XOR2;
                    FINAL_ROUND:    if (round_cnt == init_limit) next_state = FINAL_XOR2;
                    FINAL_XOR2:     next_state = DONE_STATE;
                    DONE_STATE:     next_state = IDLE;
                    default:        next_state = IDLE;
                endcase
            end

            // Shared sequential logic
            always_ff @(posedge clk or negedge rst_n) begin
                if (!rst_n) begin
                    state <= IDLE;
                    active_encrypt <= 0;
                    block_cnt <= 0;
                    ad_block_cnt <= 0;
                    round_cnt <= 0;
                    num_rounds <= 12;
                    x0 <= 0; x1 <= 0; x2 <= 0; x3 <= 0; x4 <= 0;
                    done <= 0;
                    tag <= 0;
                    shared_padded_data_out_reg <= 0;
                    data_out_block <= 0;
                end else begin
                    state <= next_state;
                    case (state)
                        IDLE: begin
                            done <= 0;
                            block_cnt <= 0;
                            ad_block_cnt <= 0;
                            round_cnt <= 0;
                            if (ENCRYPTION_CORE && enc_start) begin
                                active_encrypt <= 1;
                            end else if (DECRYPTION_CORE && dec_start) begin
                                active_encrypt <= 0;
                            end
                        end

                        INIT_LOAD: begin
                            x0 <= IV;
                            if (active_encrypt) begin
                                x1 <= enc_key[127:64];
                                x2 <= enc_key[63:0];
                                x3 <= enc_nonce[127:64];
                                x4 <= enc_nonce[63:0];
                            end else begin
                                x1 <= dec_key[127:64];
                                x2 <= dec_key[63:0];
                                x3 <= dec_nonce[127:64];
                                x4 <= dec_nonce[63:0];
                            end
                            num_rounds <= 12;
                            round_cnt <= 0;
                            ad_block_cnt <= 0;
                        end

                        INIT_PERM: begin
                            x0 <= x0_n; x1 <= x1_n; x2 <= x2_n;
                            x3 <= x3_n; x4 <= x4_n;
                        end

                        INIT_ROUND: begin
                            x0 <= x0_r_n; x1 <= x1_r_n; x2 <= x2_r_n;
                            x3 <= x3_r_n; x4 <= x4_r_n;
                            round_cnt <= round_cnt + 1;
                        end

                        INIT_XOR: begin
                            if (active_encrypt) begin
                                x3 <= x3 ^ enc_key[127:64];
                                x4 <= x4 ^ enc_key[63:0];
                            end else begin
                                x3 <= x3 ^ dec_key[127:64];
                                x4 <= x4 ^ dec_key[63:0];
                            end
                            round_cnt <= 0;
                            ad_block_cnt <= 0;
                        end

                        AD_ABSORB: begin
                            x0 <= x0 ^ shared_ad_data_x0 ^ shared_ad_pad_x0;
                            x1 <= x1 ^ shared_ad_data_x1 ^ shared_ad_pad_x1;
                            num_rounds <= 8;
                            round_cnt <= 0;
                        end

                        AD_PERM1: begin
                            if (ad_block_cnt == (NUM_AD_BLOCKS - 1)) begin
                                if (shared_ad_needs_pad0) x0 <= x0_8_n ^ 64'h0000000000000001;
                                else x0 <= x0_8_n;
                            end else begin
                                x0 <= x0_8_n;
                            end
                            x1 <= x1_8_n;
                            x2 <= x2_8_n;
                            x3 <= x3_8_n;
                            x4 <= x4_8_n;
                            
                            if (ad_block_cnt < (NUM_AD_BLOCKS - 1)) begin
                                ad_block_cnt <= ad_block_cnt + 1;
                            end
                        end

                        AD_PERM2: begin
                            x0 <= x0_8_n;
                            x1 <= x1_8_n;
                            x2 <= x2_8_n;
                            x3 <= x3_8_n;
                            x4 <= x4_8_n;
                        end

                        AD_ROUND: begin
                            x0 <= x0_r_n; x1 <= x1_r_n; x2 <= x2_r_n;
                            x3 <= x3_r_n; x4 <= x4_r_n;
                            if (round_cnt == data_limit) begin
                                if (ad_block_cnt == (NUM_AD_BLOCKS - 1)) begin
                                    if (shared_ad_needs_pad0) begin
                                        x0 <= x0_r_n ^ 64'h0000000000000001;
                                        round_cnt <= 0;
                                    end else begin
                                        round_cnt <= 0;
                                    end
                                end else begin
                                    ad_block_cnt <= ad_block_cnt + 1;
                                    round_cnt <= 0;
                                end
                            end else begin
                                round_cnt <= round_cnt + 1;
                            end
                        end

                        AD_ROUND2: begin
                            x0 <= x0_r_n; x1 <= x1_r_n; x2 <= x2_r_n;
                            x3 <= x3_r_n; x4 <= x4_r_n;
                            round_cnt <= round_cnt + 1;
                        end

                        DOMAIN_SEP: begin
                            x4 <= x4 ^ DSEP;
                        end

                        WAIT_DATA: begin
                        end

                        DATA_ABSORB: begin
                            data_out_block <= {x0 ^ reordered_block[127:64], x1 ^ reordered_block[63:0]};
                            if (active_encrypt) begin
                                x0 <= x0 ^ reordered_block[127:64];
                                x1 <= x1 ^ reordered_block[63:0];
                            end else begin
                                x0 <= (x0 & ~reordered_mask[127:64]) ^ reordered_block[127:64];
                                x1 <= (x1 & ~reordered_mask[63:0]) ^ reordered_block[63:0];
                            end
                            num_rounds <= 8;
                            round_cnt <= 0;
                        end

                        DATA_PERM: begin
                            if (block_cnt == (NUM_BLOCKS - 1)) begin
                                x0 <= x0_8_n ^ 64'h0000000000000001;
                            end else begin
                                x0 <= x0_8_n;
                            end
                            x1 <= x1_8_n;
                            x2 <= x2_8_n;
                            x3 <= x3_8_n;
                            x4 <= x4_8_n;
                        end

                        DATA_ROUND: begin
                            x0 <= x0_r_n; x1 <= x1_r_n; x2 <= x2_r_n;
                            x3 <= x3_r_n; x4 <= x4_r_n;
                            round_cnt <= round_cnt + 1;
                            if (round_cnt == data_limit && block_cnt == (NUM_BLOCKS - 1)) begin
                                x0 <= x0_r_n ^ 64'h0000000000000001;
                            end
                        end

                        DATA_STORE: begin
                            logic [127:0] output_block;
                            output_block = {
                                data_out_block[71:64], data_out_block[79:72], data_out_block[87:80], data_out_block[95:88],
                                data_out_block[103:96],data_out_block[111:104],data_out_block[119:112],data_out_block[127:120],
                                data_out_block[7:0],   data_out_block[15:8],   data_out_block[23:16], data_out_block[31:24],
                                data_out_block[39:32], data_out_block[47:40], data_out_block[55:48], data_out_block[63:56]
                            };
                            if (block_cnt < (MESSAGE_SIZE / 128)) begin
                                shared_padded_data_out_reg[(NUM_BLOCKS * 128 - 1) - (block_cnt * 128) -: 128] <= output_block;
                            end else if (REM_BITS > 0) begin
                                shared_padded_data_out_reg[(NUM_BLOCKS * 128 - 1) - (block_cnt * 128) -: SAFE_REM_BITS] <= output_block[127 -: SAFE_REM_BITS];
                            end
                            block_cnt <= block_cnt + 1;
                        end

                        FINAL_XOR1: begin
                            if (active_encrypt) begin
                                x2 <= x2 ^ enc_key[127:64];
                                x3 <= x3 ^ enc_key[63:0];
                            end else begin
                                x2 <= x2 ^ dec_key[127:64];
                                x3 <= x3 ^ dec_key[63:0];
                            end
                            num_rounds <= 12;
                            round_cnt <= 0;
                        end

                        FINAL_PERM: begin
                            x0 <= x0_n; x1 <= x1_n; x2 <= x2_n;
                            x3 <= x3_n; x4 <= x4_n;
                        end

                        FINAL_ROUND: begin
                            x0 <= x0_r_n; x1 <= x1_r_n; x2 <= x2_r_n;
                            x3 <= x3_r_n; x4 <= x4_r_n;
                            round_cnt <= round_cnt + 1;
                        end

                        FINAL_XOR2: begin
                            if (active_encrypt) begin
                                tag <= {byteswap64(x3 ^ enc_key[127:64]), byteswap64(x4 ^ enc_key[63:0])};
                            end else begin
                                tag <= {byteswap64(x3 ^ dec_key[127:64]), byteswap64(x4 ^ dec_key[63:0])};
                            end
                        end

                        DONE_STATE: begin
                            done <= 1;
                        end
                        default: ;
                    endcase
                end
            end
        end
endgenerate

endmodule

// =============================================================================
// HELPER HARDWARE MODULES
// =============================================================================

// 2-Round Serial Chained Hardware Block
module ascon_round_2 (
    input  logic [63:0] x0_in, x1_in, x2_in, x3_in, x4_in,
    input  logic [7:0]  rc0, rc1,
    output logic [63:0] x0_out, x1_out, x2_out, x3_out, x4_out
);
    logic [63:0] x0_int, x1_int, x2_int, x3_int, x4_int;

    ascon_round r0 (
        .x0_in(x0_in), .x1_in(x1_in), .x2_in(x2_in), .x3_in(x3_in), .x4_in(x4_in),
        .rc(rc0),
        .x0_out(x0_int), .x1_out(x1_int), .x2_out(x2_int), .x3_out(x3_int), .x4_out(x4_int)
    );

    ascon_round r1 (
        .x0_in(x0_int), .x1_in(x1_int), .x2_in(x2_int), .x3_in(x3_int), .x4_in(x4_int),
        .rc(rc1),
        .x0_out(x0_out), .x1_out(x1_out), .x2_out(x2_out), .x3_out(x3_out), .x4_out(x4_out)
    );
endmodule

// 4-Round Serial Chained Hardware Block
module ascon_round_4 (
    input  logic [63:0] x0_in, x1_in, x2_in, x3_in, x4_in,
    input  logic [7:0]  rc0, rc1, rc2, rc3,
    output logic [63:0] x0_out, x1_out, x2_out, x3_out, x4_out
);
    logic [63:0] x0_0, x1_0, x2_0, x3_0, x4_0;
    logic [63:0] x0_1, x1_1, x2_1, x3_1, x4_1;
    logic [63:0] x0_2, x1_2, x2_2, x3_2, x4_2;

    ascon_round r0 (.x0_in(x0_in), .x1_in(x1_in), .x2_in(x2_in), .x3_in(x3_in), .x4_in(x4_in), .rc(rc0), .x0_out(x0_0), .x1_out(x1_0), .x2_out(x2_0), .x3_out(x3_0), .x4_out(x4_0));
    ascon_round r1 (.x0_in(x0_0),  .x1_in(x1_0),  .x2_in(x2_0),  .x3_in(x3_0),  .x4_in(x4_0),  .rc(rc1), .x0_out(x0_1), .x1_out(x1_1), .x2_out(x2_1), .x3_out(x3_1), .x4_out(x4_1));
    ascon_round r2 (.x0_in(x0_1),  .x1_in(x1_1),  .x2_in(x2_1),  .x3_in(x3_1),  .x4_in(x4_1),  .rc(rc2), .x0_out(x0_2), .x1_out(x1_2), .x2_out(x2_2), .x3_out(x3_2), .x4_out(x4_2));
    ascon_round r3 (.x0_in(x0_2),  .x1_in(x1_2),  .x2_in(x2_2),  .x3_in(x3_2),  .x4_in(x4_2),  .rc(rc3), .x0_out(x0_out), .x1_out(x1_out), .x2_out(x2_out), .x3_out(x3_out), .x4_out(x4_out));
endmodule