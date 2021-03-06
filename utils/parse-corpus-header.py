#!/usr/bin/env python

"""
	Script to add syscall benchmarks to the existing libsyzcorpus
"""

header ="""

#define MAX_SYSCALLS 4207

#define TO_NSECS(sec,nsec)\\
		((sec) * 1000000000 + (nsec))

#include <time.h> 
#include <stdint.h>

typedef struct {
	int16_t syscall_number;
	intptr_t ret_val;
	unsigned long long time_in;
	unsigned long long time_out;
} vb_syscall_info_t;
"""

#Do this later
def usage():
	return

def parse_file(program_src):
	with open(program_src, "r") as f:
		lines = f.readlines()
		s=[]
		for line_number, line in enumerate(lines):
			if 	("int _" in line) and ("(void);" in line): 
				line = line.replace("void", "vb_syscall_info_t * scall_info, int * num_calls")
			if "#define __LIBSYZCORPUS_H__" in line:
				line += header 

			s.append(line)
	
	return ''.join(s) + '\n' 

if __name__ == "__main__":
	s= parse_file("../src/kernels/corpuses/sample-corpus/libsyzcorpus.h");
	print s
