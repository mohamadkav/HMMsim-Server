#include <iostream>
#include <zlib.h>
#include <string>

using namespace std;


int main()
{
	int length = 1024;
	unsigned long int  *a = new  unsigned long int[length];
	string filename = "/home/coston/Simulator/obj-intel64/kossssssss-instr-addr.gz";
	gzFile file = gzopen(filename.c_str(), "r");
	gzread(file, a, length-1);
	for(int i = 0; i < 10; i++)
		cout <<a[i]<<"\n";
}
