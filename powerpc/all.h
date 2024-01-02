#include "../all.h"

typedef struct Rv64Op Rv64Op;

enum PowerpcReg {
	R0 = RXX + 1,

	/* SP, TOC */
	R1, R2,

	/* caller-saved */
	R3, R4, R5, R6, R7, R8, R9, R10,

	R11, R12,

	/* reserved */
	R13,

	/* callee-saved */
	R14, R15, R16, R17, R18, R19, R20, R21, R22,
	R23, R24, R25, R26, R27, R28, R29, R30, R31

	/* scratch */
	F0,

	/* caller-saved */
	F1, F2, F3, F4, F5, F6, F7, F8,
	F9, F10, F11, F12, F13,

	/* callee-saved */
	F14, F15, F16, F17, F18, F19, F20, F21,
	F22, F23, F24, F25, F26, F27, F28, F29,
	F30, F31, F31

	NFPR = F32 - F0 + 1,
	NGPR = R31 - R0 + 1,
	NGPS = R10 - R0 + 1,
	NFPS = F13 - F0 + 1,
	NCLR = (R31 - R14 + 1) + (F31 - F14 + 1),
};
MAKESURE(reg_not_tmp, F31 < (int)Tmp0);

struct Rv64Op {
	char imm;
};

/* targ.c */
extern int rv64_rsave[];
extern int rv64_rclob[];
extern Rv64Op rv64_op[];

/* abi.c */
bits rv64_retregs(Ref, int[2]);
bits rv64_argregs(Ref, int[2]);
void rv64_abi(Fn *);

/* isel.c */
void rv64_isel(Fn *);

/* emit.c */
void rv64_emitfn(Fn *, FILE *);
