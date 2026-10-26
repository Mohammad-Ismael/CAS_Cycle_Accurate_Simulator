// Code your design here
module full_adder(sum,cout,in1, in2, cin);
	input in1,in2,cin;
	output sum,cout;
    
  	wire in1,in2,cin,sum,cout;
  
  	wire w1,w2,w3;

  xor(sum,in1,in2,cin);
  and(w1,in1,in2);
  and(w2,in2,cin);
  and(w3,in1,cin);
  or(cout,w1,w2,w3);

endmodule

//4bit adder
module bit4_adder(sum,cout,a,b,cin);
      input [3:0]a;
  input [3:0]b;
  input cin;
  
  output [3:0]sum;
  output cout;
  
  wire [3:0]a;
  wire [3:0]b;
  wire [2:0]c;
  wire [3:0]sum;
  wire cout;
  
  full_adder fa0(sum[0], c[0], a[0], b[0], cin);
  full_adder fa1(sum[1], c[1], a[1], b[1], c[0]);
  full_adder fa2(sum[2], c[2], a[2], b[2], c[1]);
  full_adder fa3(sum[3], cout, a[3], b[3], c[2]);
  
endmodule

//16bit adder
module bit16_adder(sum,cout,a,b,cin);
  input [15:0]a,b;
  input cin;
  output [15:0]sum;
  output cout;
  
  wire [15:0]a,b,sum;
  wire cin,cout;
  
  wire [2:0]c;
  
  bit4_adder fa0(sum[3:0],c[0],a[3:0],b[3:0],cin);
  bit4_adder fa1(sum[7:4],c[1],a[7:4],b[7:4],c[0]);
  bit4_adder fa2(sum[11:8],c[2],a[11:8],b[11:8],c[1]);
  bit4_adder fa3(sum[15:12],cout,a[15:12],b[15:12],c[2]);
  
endmodule