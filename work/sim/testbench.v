`timescale 1ps/1ps
module testbench;
  reg clk, rst_n, enable, I;
  wire [7:0] O;
  wire success;

  puzzle dut (.clk(clk), .rst_n(rst_n), .enable(enable), .I(I),
    .\O[0] (O[0]), .\O[1] (O[1]), .\O[2] (O[2]), .\O[3] (O[3]),
    .\O[4] (O[4]), .\O[5] (O[5]), .\O[6] (O[6]), .\O[7] (O[7]),
    .success(success));

  always #5000 clk = ~clk;

  initial begin
    clk = 0; rst_n = 0; enable = 0; I = 0;
    #20000 rst_n = 1;
    #20000 enable = 1;
    repeat (40) begin
      #10000 I = ~I;
    end
    #10000 $display("success=%b O=%b", success, O);
    $finish;
  end
endmodule