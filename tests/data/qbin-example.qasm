OPENQASM 3.0;
qubit[2] q;
bit[2] c;

h q[0];
cx q[0], q[1];
c[1] = measure q[1];
if (c[1] == 1) { x q[0]; }

