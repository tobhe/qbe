#include "all.h"

enum {
	Ki = -1, /* matches Kw and Kl */
	Ka = -2, /* matches all classes */
};

#define FRAME_ALIGN	16u
#define ROUNDUP(x) (((x) + (FRAME_ALIGN - 1u)) & ~(FRAME_ALIGN - 1u))

#define CMP(X) \
	X(Cieq,       "eq") \
	X(Cine,       "ne") \
	X(Cisge,      "ge") \
	X(Cisgt,      "gt") \
	X(Cisle,      "le") \
	X(Cislt,      "lt") \
	X(Ciuge,      "cs") \
	X(Ciugt,      "hi") \
	X(Ciule,      "ls") \
	X(Ciult,      "cc") \
	X(NCmpI+Cfeq, "eq") \
	X(NCmpI+Cfge, "ge") \
	X(NCmpI+Cfgt, "gt") \
	X(NCmpI+Cfle, "ls") \
	X(NCmpI+Cflt, "mi") \
	X(NCmpI+Cfne, "ne") \
	X(NCmpI+Cfo,  "vc") \
	X(NCmpI+Cfuo, "vs")

/* Instruction format strings:
 * %k  is used to set the class of the instruction,
 *     it'll expand to
 * 			"w" word
 *          "d" double word
 *     on the instruction class
 * %0  designates the first argument
 * %1  designates the second argument
 * %=  designates the result
 */

static struct {
	short op;
	short cls;
	char *asm;
} omap[] = {
	{ Oadd,    Ki, "add%k %=, %0, %1" },
	/* Removed the dot here, but we need an s-class for single
	e.g. fadds for float and fadd for double */
	{ Oadd,    Ka, "fadd%k %=, %0, %1" },
	{ Osub,    Ki, "sub%k %=, %0, %1" },
	{ Osub,    Ka, "fsub%k %=, %0, %1" },
	{ Oneg,    Ki, "neg%k %=, %0" },
	{ Oneg,    Ka, "fneg%k %=, %0" },
	
	{ Odiv,    Ki, "divw %=, %0, %1" },
	{ Oudiv,   Ki, "divwu %=, %0, %1" },
	{ Odiv,    Ka, "fdiv%k %=, %0, %1" },
	
	/* PowerPC Microprocessor Family: The Programming Environments 8-58 */
	{ Orem,    Ki, "divw %?, %0, %1\n\tmullw %?, %?, %1\n\tsubf %=, %?, %0" },
	{ Ourem,   Ki, "divwu %?, %0, %1\n\tmullw %?, %?, %1\n\tsubf %=, %?, %0" },

	/* Here %k could be w for word and d for double word */
	{ Omul,    Ki, "mul %=, %0, %1" },
	{ Omul,    Ka, "fmul %=, %0, %1" },
	
	/* logical operators */
	{ Oand,    Ki, "and %=, %0, %1" },
	{ Oor,     Ki, "or %=, %0, %1" },
	{ Oxor,    Ki, "xor %=, %0, %1" },
	
	{ Osar,    Ki, "sraw%k %=, %0, %1" },
	/* TODO is the general shift right the logical shift right? */
	{ Oshr,    Ki, "srw%k %=, %0, %1" },
	{ Oshl,    Ki, "slw%k %=, %0, %1" },

	{ Ocsltl,  Ki, "slt %=, %0, %1" },
	{ Ocultl,  Ki, "sltu %=, %0, %1" },
	
	/*
	Comparisons:

	1. fcmpu: Use ordered or unordered?
	ordered NaN == NaN --> False
	Used unordered for now to allow NaN comparison
	There seems to be now difference between single and double

	2. There seems to be no greater, equal etc. instructions.
	--> Check ARM64 which also seems to only have compare instructions
	```
	*/
	{ Oacmp,   Ki, "cmpw 7, %0, %1" },
	{ Oafcmp,  Ka, "fcmpu 7, %0, %1" },

	/*
	Store
	added a "t", so sb became stb
	*/
	{ Ostoreb, Kw, "stb %0, %M1" },
	{ Ostoreh, Kw, "sth %0, %M1" },
	{ Ostorew, Kw, "stw %0, %M1" },
	/* double word */
	{ Ostorel, Ki, "std %0, %M1" },
	{ Ostores, Kw, "stfs %0, %M1" },
	{ Ostored, Kw, "stfd %0, %M1" },
	
	/*
	Load
	*/
	{ Oloadsb, Ki, "lbz %=, %M0" },
	{ Oloadub, Ki, "lbz %=, %M0" },
	{ Oloadsh, Ki, "lhz %=, %M0" },
	{ Oloaduh, Ki, "lhz %=, %M0" },
	{ Oloadsw, Ki, "lwz %=, %M0" },
	{ Oloaduw, Kw, "lwz %=, %M0" },

	{ Oload,   Kw, "lwz %=, %M0" },
	/* TODO loading a long long needs two lwz */
	{ Oload,   Kl, "lwz %=, %M0" },

	{ Oload,   Ks, "lfs %=, %M0" },
	{ Oload,   Kd, "lfd %=, %M0" },


	{ Oextsb,  Ki, "extsb %=, %0" },
	{ Oextub,  Ki, "rlwinm %=, %0, 0, 0xff" },
	{ Oextsh,  Ki, "extsh %=, %0" },
	{ Oextuh,  Ki, "rlwinm %=, %0, 0, 0xffff" },
	{ Oextsw,  Kl, "extsw %=, %0" },
	{ Oextuw,  Kl, "mr %=, %0" },
	
	/* fcvt.s.d means from double to single */
	{ Otruncd, Ks, "frsp %=, %0" },
	
	/*
	 * Conversion does not seem necessary for other types
	 * should just work with load
	{ Oexts,   Kd, "fcvt.d.s %=, %0" },
	{ Ostosi,  Kw, "fcvt.w.s %=, %0, rtz" },
	{ Ostosi,  Kl, "fcvt.l.s %=, %0, rtz" },
	{ Ostoui,  Kw, "fcvt.wu.s %=, %0, rtz" },
	{ Ostoui,  Kl, "fcvt.lu.s %=, %0, rtz" },
	{ Odtosi,  Kw, "fcvt.w.d %=, %0, rtz" },
	{ Odtosi,  Kl, "fcvt.l.d %=, %0, rtz" },
	{ Odtoui,  Kw, "fcvt.wu.d %=, %0, rtz" },
	{ Odtoui,  Kl, "fcvt.lu.d %=, %0, rtz" },
	{ Oswtof,  Ka, "fcvt%k.w %=, %0" },
	{ Ouwtof,  Ka, "fcvt%k.wu %=, %0" },
	{ Osltof,  Ka, "fcvt%k.l %=, %0" },
	{ Oultof,  Ka, "fcvt%k.lu %=, %0" },
	
	{ Ocast,   Kw, "fmv.x.w %=, %0" },
	{ Ocast,   Kl, "fmv.x.d %=, %0" },
	{ Ocast,   Ks, "fmv.w.x %=, %0" },
	{ Ocast,   Kd, "fmv.d.x %=, %0" },
	*/

	{ Ocopy,   Ki, "mr %=, %0" },
	{ Ocopy,   Ka, "fmr %=, %0" },
	
	{ Oswap,   Ki, "mr %?, %0\n\tmr %0, %1\n\tmr %1, %?" },
	{ Oswap,   Ka, "fmr%k %?, %0\n\tfmr%k %0, %1\n\tfmr%k %1, %?" },
	/*
	 * These are not architecture specific and come from isel() 
	{ Oreqz,   Ki, "seqz %=, %0" },
	{ Ornez,   Ki, "snez %=, %0" },
	*/
	{ Ocall,   Kw, "jalr %0" },

#define X(c, str) \
	{ Oflag+c, Ki, "#lol: " str },
	CMP(X)
#undef X

	{ NOp, 0, 0 }
};

static char *
rname(int r)
{
	static char buf[5];

	if (R0 <= r && r <= R12) {
		sprintf(buf, "%%r%d", r - R0);
	}
	else if (R14 <= r && r <= R31) {
		sprintf(buf, "%%r%d", r - R0 + 1);
	}
	else if (F0 <= r && r <= F31) {
		sprintf(buf, "%%f%d", r - F0);
	}
	return buf;
}

static int64_t
slot(Ref r, Fn *fn)
{
	int s;

	s = rsval(r);
	assert(s <= fn->slot);
	if (s < 0)
		return 8 * -s;
	else
		return -4 * (fn->slot - s);
}

static void
emitf(char *s, Ins *i, Fn *fn, FILE *f)
{
	Ref r;
	int k, c;
	Con *pc;
	int64_t offset;

	fputc('\t', f);
	for (;;) {
		k = i->cls;
		while ((c = *s++) != '%')
			if (!c) {
				fputc('\n', f);
				return;
			} else
				fputc(c, f);
		switch ((c = *s++)) {
		default:
			die("invalid escape");
		case '?':
			if (KBASE(k) == 0)
				fputs("%r6", f);
			else
				fputs("%f11", f);
			break;
		case 'k':
			break;
		case '=':
		case '0':
			r = c == '=' ? i->to : i->arg[0];
			assert(isreg(r));
			fputs(rname(r.val), f);
			break;
		case '1':
			r = i->arg[1];
			switch (rtype(r)) {
			default:
				die("invalid second argument");
			case RTmp:
				assert(isreg(r));
				fputs(rname(r.val), f);
				break;
			case RCon:
				pc = &fn->con[r.val];
				assert(pc->type == CBits);
				assert(pc->bits.i >= -2048 && pc->bits.i < 2048);
				fprintf(f, "%d", (int)pc->bits.i);
				break;
			}
			break;
		case 'M':
			c = *s++;
			assert(c == '0' || c == '1' || c == '=');
			r = c == '=' ? i->to : i->arg[c - '0'];
			switch (rtype(r)) {
			case RSlot:
			default:
				die("todo (powerpc emit): unhandled ref");
			case RTmp:
				assert(isreg(r));
				fprintf(f, "0(%s)", rname(r.val));
				break;
			}
			break;
		}
	}
}

static void
loadaddr(Con *c, char *rn, FILE *f)
{
	char off[32], *s;

	switch (c->sym.type) {
	case SThr: /* Thread local */
	default:
		die("unreachable");
	case SGlo: /* Global */
		fprintf(f, "\tlis %s, %s@ha\n", rn, str(c->sym.id));
		break;
	}
}

static void
loadcon(Con *c, int r, int k, FILE *f)
{
	char *rn;
	int64_t n;

	rn = rname(r);
	switch (c->type) {
	case CAddr:
		loadaddr(c, rn, f);
		break;
	case CBits:
		n = c->bits.i;
		if (!KWIDE(k))
			n = (int32_t)n;
		fprintf(f, "\tli %s, %"PRIi64"\n", rn, n);
		break;
	default:
		die("invalid constant");
	}
}

static void
emitins(Ins *i, Fn *fn, FILE *f)
{
	int o;
	char *rn;
	int64_t s;
	Con *con;

	switch (i->op) {
	default:
	Table:
		/* most instructions are just pulled out of
		 * the table omap[], some special cases are
		 * detailed below */
		for (o=0;; o++) {
			/* this linear search should really be a binary
			 * search */
			if (omap[o].op == NOp)
				die("no match for %s(%c)",
					optab[i->op].name, "wlsd"[i->cls]);
			if (omap[o].op == i->op)
			if (omap[o].cls == i->cls || omap[o].cls == Ka
			|| (omap[o].cls == Ki && KBASE(i->cls) == 0))
				break;
		}
		emitf(omap[o].asm, i, fn, f);
		break;
	case Ocopy:
		if (req(i->to, i->arg[0]))
			break;
		if (rtype(i->to) == RSlot) {
			switch (rtype(i->arg[0])) {
			case RSlot:
			case RCon:
				die("unimplemented");
				break;
			default:
				assert(isreg(i->arg[0]));
				i->arg[1] = i->to;
				i->to = R;
				switch (i->cls) {
				case Kw: i->op = Ostorew; break;
				case Kl: i->op = Ostorel; break;
				case Ks: i->op = Ostores; break;
				case Kd: i->op = Ostored; break;
				}
				goto Table;
			}
			break;
		}
		assert(isreg(i->to));
		switch (rtype(i->arg[0])) {
		case RCon:
			loadcon(&fn->con[i->arg[0].val], i->to.val, i->cls, f);
			break;
		case RSlot:
			i->op = Oload;
			goto Table;
		default:
			assert(isreg(i->arg[0]));
			goto Table;
		}
		break;
	case Onop:
		break;
	/* Immediates */
	case Oadd:
		if (isreg(i->arg[1]))
			goto Table;

		emitf("addi %=, %0, %1", i, fn, f);
		break;
	case Osub:
		if (isreg(i->arg[1]))
			goto Table;

		emitf("si %=, %0, %1", i, fn, f);
		break;
	case Oand:
		if (isreg(i->arg[1]))
			goto Table;

		emitf("andi %=, %0, %1", i, fn, f);
		break;
	case Oor:
		if (isreg(i->arg[1]))
			goto Table;

		emitf("ori %=, %0, %1", i, fn, f);
		break;
	case Omul:
		if (isreg(i->arg[1]))
			goto Table;

		emitf("mulli %=, %0, %1", i, fn, f);
		break;
	case Oxor:
		if (isreg(i->arg[1]))
			goto Table;

		emitf("xori %=, %0, %1", i, fn, f);
		break;
	/* End Immediates */
	case Oaddr:
		assert(rtype(i->arg[0]) == RSlot);
		rn = rname(i->to.val);
		s = slot(i->arg[0], fn);
		if (-s < 2048) {
			fprintf(f, "\tadd %s, fp, %"PRId64"\n", rn, s);
		} else {
			fprintf(f,
				"\tli %s, %"PRId64"\n"
				"\tadd %s, fp, %s\n",
				rn, s, rn, rn
			);
		}
		break;
	case Ocall:
		switch (rtype(i->arg[0])) {
		case RCon:
			con = &fn->con[i->arg[0].val];
			if (con->type != CAddr
			|| con->sym.type != SGlo
			|| con->bits.i)
				goto Invalid;
			fprintf(f, "\tbl %s\n", str(con->sym.id));
			break;
		case RTmp:
			emitf("jalr %0", i, fn, f);
			break;
		default:
		Invalid:
			die("invalid call argument");
		}
		break;
	case Osalloc:
		emitf("sub sp, sp, %0", i, fn, f);
		if (!req(i->to, R))
			emitf("mr %=, sp", i, fn, f);
		break;
	case Odbgloc:
		emitdbgloc(i->arg[0].val, i->arg[1].val, f);
		break;
	}
}

/*

  Stack-frame layout:

  +=============+  r1 <- sp
  | back chain  |
  +-------------+  r1 + 4
  |  link area  |
  +-------------+  r1 + 8
  |   spill     |
  +-------------+  .
  |   locals    |
  +-------------+  .
  |   padding   |
  +-------------+ 
  |   GP save   |
  |    area     |
  +-------------+
  |   FP save   |
  |    area     |
  +=============+  r1 + fs <- old sp
  | back chain  |
  +-------------+  r1 + fs + 4
  |  link area  |
  +-------------+ 
  :     ...     :

*/

void
powerpc_emitfn(Fn *fn, FILE *f)
{
	static char *ctoa[] = {
	#define X(c, s) [c] = s,
		CMP(X)
	#undef X
	};
	static int id0;
	int lbl, c, off, *r;
	Blk *b, *t;
	Ins *i;
	size_t fs;

	emitfnlnk(fn->name, &fn->lnk, f);

	for (r=powerpc_rclob, off=0; *r>=0; r++) {
		if (fn->reg & BIT(*r))
			fprintf(stderr, "found 1\n");
	}

	fs = ROUNDUP(12);

	/* Adjust SP + Back chain */
	fprintf(f, "\tstwu %%r1, -%zu(%%r1)\n", fs);

	/* Save link register */
	fprintf(f, "\tmflr %%r0\n");
	fprintf(f, "\tstw %%r0, %zu(%%r1)\n", fs+4);

	/* Save env */
	fprintf(f, "\tstw %%r31, %zu(%%r1)\n", fs-4);
	fprintf(f, "\tmr %%r31, %%r1\n");

	for (lbl=0, b=fn->start; b; b=b->link) {
		if (lbl || b->npred > 1)
			fprintf(f, ".L%d:\n", id0+b->id);
		for (i=b->ins; i!=&b->ins[b->nins]; i++)
			emitins(i, fn, f);
		lbl = 1;
		switch (b->jmp.type) {
		case Jhlt:
			fprintf(f, "\ttrap\n");
			break;
		case Jret0:
			/* Load return value in return register */
			/* TODO change 9 to actal register*/

			fprintf(f, "\tlwz %%r0, 20(%%r1)\n");

			/* Calculate environment pointer */
			fprintf(f, "\tlwz %%r31,%zu(%%r1)\n", fs-4);

			/* Reset stack pointer */
			fprintf(f, "\taddi %%r1,%%r1,%zu\n", fs);

			fprintf(f, "\tmtlr %%r0\n");

			/* return */
			fprintf(f, "\tblr\n");

			break;
		case Jjmp:
		Jmp:
			if (b->s1 != b->link)
				fprintf(f, "\tb\t%s%d\n", T.asloc,
				    id0+b->s1->id);
			else
				lbl=0;
			break;
		default:
			c = b->jmp.type - Jjf;
			if (c < 0 || c > NCmp)
				die("unhandled jump %d", b->jmp.type);
			if (b->link == b->s2) {
				t = b->s1;
				b->s1 = b->s2;
				b->s2 = t;
			} else
				c = cmpneg(c);
			fprintf(f,
				"\tb%s\t%s%d\n",
				ctoa[c], T.asloc, id0+b->s2->id
			);
			goto Jmp;
		}
	}
	id0 += fn->nblk;
	elf_emitfnfin(fn->name, f);
}
