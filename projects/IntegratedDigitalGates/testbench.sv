
module fulladder_testbench;
	reg [3:0]a,b;
  reg c;
  wire [3:0] sum;
  wire carry;

  bit4_adder uut (.sum(sum),.cout(carry),.a(a),.b(b),.cin(c));

	initial 
		begin
          $monitor("time=%d:%b + %b + %b = %b, carry = %b\n",$time,a,b,c,sum,carry);
		#10 a=4'b0110;b=4'b1010;c=0;
		#10 a=4'b0010;b=4'b0001;c=1;
		#10 a=4'b1100;b=4'b1011;c=0;
		#10 a=4'b0001;b=4'b1100;c=1;
		#10 a=4'b1001;b=4'b0100;c=0;

    	$stop;
	end
endmodule


