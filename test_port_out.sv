module sub #(
  parameter int N = 8
) (
  output logic out_o
);
  if (N > 0) begin : gen_pmp
    always_comb begin
      out_o = 1'b1;
    end
  end
endmodule

module mid_mod;
  logic wire_w;
  logic result_o;

  sub #(.N(8)) i_sub (.out_o(wire_w));

  always_comb begin
    if (wire_w) begin
      result_o = 1'b1;
    end else begin
      result_o = 1'b0;
    end
  end
endmodule

module top_mod;
  mid_mod i_mid ();

  initial begin
    #10;
    $display("[TEST] sub.out_o=%b wire_w=%b result_o=%b",
             i_mid.i_sub.out_o, i_mid.wire_w, i_mid.result_o);
    $finish;
  end
endmodule
