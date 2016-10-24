Prerequisites:

Linux
g++ compiler version 4.8.2 or newer
Pin version 2.14


To build the simulator:

make


To trace a program:

pin.sh -t obj-intel64/TracerPin.so -- /program/to/trace


To run the simulator:

obj-intel64/sim configuration trace
