#include "all.h"

PowerpcOp powerpc_op[NOp] = {
#define O(op, t, x) [O##op] =
#define P(imm) { imm },
#include "../ops.h"
};

int powerpc_rsave[] = {
	R3, R4, R5, R6, R7,
	R8, R9, R10, R11, R12,
	F0, F1, F2, F3, F4, F5, F6, F7,
	F8, F9, F10, F11, F12, F13,
	-1
};
int powerpc_rclob[] = {
	R14, R15, R16, R17, R18, R19, R20, R21,
	R22, R23, R24, R25, R26, R27, R28, R29,
	R30, R31,
	F14, F15, F16, F17, F18, F19, F20, F21,
	F22, F23, F24, F25, F26, F27, F28, F29,
	F30, F31,
	-1
};

#define RGLOB (BIT(R0) | BIT(R1) | BIT(R2))

static int
powerpc_memargs(int op)
{
	(void)op;
	return 0;
}

Target T_powerpc = {
	.name = "powerpc",
	.gpr0 = R0,
	.ngpr = NGPR,
	.fpr0 = F0,
	.nfpr = NFPR,
	.rglob = RGLOB,
	.nrglob = 3,
	.rsave = powerpc_rsave,
	.nrsave = {NGPS, NFPS},
	.retregs = powerpc_retregs,
	.argregs = powerpc_argregs,
	.memargs = powerpc_memargs,
	.abi0 = elimsb,
	.abi1 = powerpc_abi,
	.isel = powerpc_isel,
	.emitfn = powerpc_emitfn,
	.emitfin = elf_emitfin,
	.asloc = ".L",
};

MAKESURE(rsave_size_ok, sizeof powerpc_rsave == (NGPS+NFPS+1) * sizeof(int));
MAKESURE(rclob_size_ok, sizeof powerpc_rclob == (NCLR+1) * sizeof(int));
