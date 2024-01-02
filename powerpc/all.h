#include "../all.h"

typedef struct PowerpcOp PowerpcOp;

enum PowerpcReg {
	R0 = RXX + 1,

	/* SP, TOC */
	R1, R2,

	/* caller-saved */
	R3, R4, R5, R6, R7, R8, R9, R10,

	/* ENV, EXC */
	R11, R12,

	/* reserved */
	/* R13, */

	/* callee-saved */
	R14, R15, R16, R17, R18, R19, R20, R21, R22,
	R23, R24, R25, R26, R27, R28, R29, R30, R31,

	/* scratch */
	F0,

	/* caller-saved */
	F1, F2, F3, F4, F5, F6, F7, F8,
	F9, F10, F11, F12, F13,

	/* callee-saved */
	F14, F15, F16, F17, F18, F19, F20, F21,
	F22, F23, F24, F25, F26, F27, F28, F29,
	F30, F31,

	NFPR = F31 - F0 + 1,
	NGPR = R31 - R0 + 1,
	NGPS = R12 - R0 + 1,
	NFPS = F13 - F0 + 1,
	NCLR = (R31 - R14 + 1) + (F31 - F14 + 1),
};
MAKESURE(reg_not_tmp, F31 < (int)Tmp0);

struct PowerpcOp {
	char imm;
};

/* targ.c */
extern int powerpc_rsave[];
extern int powerpc_rclob[];
extern PowerpcOp powerpc_op[];

/* abi.c */
bits powerpc_retregs(Ref, int[2]);
bits powerpc_argregs(Ref, int[2]);
void powerpc_abi(Fn *);

/* isel.c */
void powerpc_isel(Fn *);

/* emit.c */
void powerpc_emitfn(Fn *, FILE *);
