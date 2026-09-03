import re
t = open('/tmp/result.txt').read()
p = dict(re.findall(r'\(I_(\d+)\s+(true|false)\)', t))
bits = ''.join('1' if p[str(i)] == 'true' else '0' for i in range(121))
print(bits)
