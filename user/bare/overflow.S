# This user-space program makes MIPS overflow exception.
#
# To test this manually:
# - Add the following one line in init/init.c:
#	ENV_CREATE(user_bare_overflow);
# - Then build the kernel with `make`

.text
.globl _start
_start:
	li      x2, 0x80000000
	addi    x3, x2, 0  # overflow
