bits = open('/tmp/answer.txt').read().strip()

L = []
L.append("`timescale 1ps/1ps")
L.append("module tb;")
L.append("reg clk,rst_n,enable,I;")
L.append("wire [7:0] O; wire success;")
L.append("puzzle dut (.clk(clk),.rst_n(rst_n),.enable(enable),.I(I),")
for i in range(8):
    L.append(f"  .\\O[{i}] (O[{i}]),")
L.append("  .success(success));")
L.append("reg [7:0] pO; reg f;")
L.append("initial begin")
L.append("I=0;enable=0;rst_n=0;clk=0;f=1;")

def pulse():
    L.append("#5000;clk=1;#5000;clk=0;")

for _ in range(3):
    pulse()
L.append("rst_n=1;")
L.append("enable=1;")
for b in bits:
    L.append(f"I=1'b{b};")
    pulse()
L.append("enable=0;")
for _ in range(60):
    pulse()
    L.append('if(f||O!==pO)begin $display("success=%b O=%b (0x%02h) %c",success,O,O,(O>=32&&O<127)?O:46); pO=O; f=0; end')
L.append("$finish;")
L.append("end")
L.append("endmodule")

open('/tmp/tb.v', 'w').write("\n".join(L) + "\n")
print("wrote /tmp/tb.v")
