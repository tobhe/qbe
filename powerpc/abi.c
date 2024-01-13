#include "all.h"

/* the powerpc elf abi */

typedef struct Class Class;
typedef struct Insl Insl;
typedef struct Params Params;

enum {
	Cstk = 1, /* pass on the stack */
	Cptr = 2, /* replaced by a pointer */
};

struct Class {
	char class;
	char ishfa;
	struct {
		char base;
		uchar size;
	} hfa;
	uint size;
	uint align;
	Typ *t;
	uchar nreg;
	uchar ngp;
	uchar nfp;
	int reg[4];
	int cls[4];
};

struct Insl {
	Ins i;
	Insl *link;
};

struct Params {
	uint ngp;
	uint nfp;
	uint stk; /* stack offset for varargs */
};

static int gpreg[10] = {R3, R4, R5, R6, R7, R8, R9, R10};
static int fpreg[10] = {F1, F2, F3, F4, F5, F6, F7, F8};
static int store[] = {
	[Kw] = Ostorew, [Kl] = Ostorel,
	[Ks] = Ostores, [Kd] = Ostored
};

/* layout of call's second argument (RCall)
 *
 *  29   12    8    4  2  0
 *  |0.00|x|xxxx|xxxx|xx|xx|                  range
 *        |   |    |  |  ` gp regs returned (0..2)
 *        |   |    |  ` fp regs returned    (0..2)
 *        |   |    ` gp regs passed         (0..8)
 *        |    ` fp regs passed             (0..8)
 *        ` env pointer passed in R31       (0..1)
 */

static int
isfloatv(Typ *t, char *cls)
{
	Field *f;
	uint n;

	for (n=0; n<t->nunion; n++)
		for (f=t->fields[n]; f->type != FEnd; f++)
			switch (f->type) {
			case Fs:
				if (*cls == Kd)
					return 0;
				*cls = Ks;
				break;
			case Fd:
				if (*cls == Ks)
					return 0;
				*cls = Kd;
				break;
			case FTyp:
				if (isfloatv(&typ[f->len], cls))
					break;
				/* fall through */
			default:
				return 0;
			}
	return 1;
}

static void
typclass(Class *c, Typ *t, int *gp, int *fp)
{
	uint64_t sz;
	uint n;

	sz = ROUNDUP(t->size, 8);
	c->t = t;
	c->class = 0;
	c->ngp = 0;
	c->nfp = 0;
	c->align = 4;

	if (t->align > 3)
		err("alignments larger than 8 are not supported");

	if (t->isdark || sz > 16 || sz == 0) {
		/* large structs are replaced by a
		 * pointer to some caller-allocated
		 * memory */
		c->class |= Cptr;
		c->size = 8;
		c->ngp = 1;
		*c->reg = *gp;
		*c->cls = Kl;
		return;
	}

	c->size = sz;
	c->hfa.base = Kx;
	c->ishfa = isfloatv(t, &c->hfa.base);
	c->hfa.size = t->size/(KWIDE(c->hfa.base) ? 8 : 4);

	if (c->ishfa)
		for (n=0; n<c->hfa.size; n++, c->nfp++) {
			c->reg[n] = *fp++;
			c->cls[n] = c->hfa.base;
		}
	else
		for (n=0; n<sz/8; n++, c->ngp++) {
			c->reg[n] = *gp++;
			c->cls[n] = Kl;
		}

	c->nreg = n;
}

static void
sttmps(Ref tmp[], int cls[], uint nreg, Ref mem, Fn *fn)
{
	uint n;
	uint64_t off;
	Ref r;

	assert(nreg <= 4);
	off = 0;
	for (n=0; n<nreg; n++) {
		tmp[n] = newtmp("abi", cls[n], fn);
		r = newtmp("abi", Kl, fn);
		emit(store[cls[n]], 0, R, tmp[n], r);
		emit(Oadd, Kl, r, mem, getcon(off, fn));
		off += KWIDE(cls[n]) ? 8 : 4;
	}
}

/* todo, may read out of bounds */
static void
ldregs(int reg[], int cls[], int n, Ref mem, Fn *fn)
{
	int i;
	uint64_t off;
	Ref r;

	off = 0;
	for (i=0; i<n; i++) {
		r = newtmp("abi", Kl, fn);
		emit(Oload, cls[i], TMP(reg[i]), r, R);
		emit(Oadd, Kl, r, mem, getcon(off, fn));
		off += KWIDE(cls[i]) ? 8 : 4;
	}
}

static void
selret(Blk *b, Fn *fn)
{
	int j, k, cty;
	Ref r;
	Class cr;

	j = b->jmp.type;

	if (!isret(j) || j == Jret0)
		return;

	r = b->jmp.arg;
	b->jmp.type = Jret0;

	if (j == Jretc) {
		typclass(&cr, &typ[fn->retty], gpreg, fpreg);
		if (cr.class & Cptr) {
			assert(rtype(fn->retr) == RTmp);
			emit(Oblit1, 0, R, INT(cr.t->size), R);
			emit(Oblit0, 0, R, r, fn->retr);
			cty = 0;
		} else {
			ldregs(cr.reg, cr.cls, cr.nreg, r, fn);
			cty = (cr.nfp << 2) | cr.ngp;
		}
	} else {
		k = j - Jretw;
		if (KBASE(k) == 0) {
			emit(Ocopy, k, TMP(R3), r, R);
			cty = 1;
		} else {
			emit(Ocopy, k, TMP(F1), r, R);
			cty = 1 << 2;
		}
	}

	b->jmp.arg = CALL(cty);
}

static int
argsclass(Ins *i0, Ins *i1, Class *carg)
{
	int va, envc, ngp, nfp, *gp, *fp;
	Class *c;
	Ins *i;

	va = 0;
	envc = 0;
	gp = gpreg;
	fp = fpreg;
	ngp = 8;
	nfp = 8;
	for (i=i0, c=carg; i<i1; i++, c++)
		switch (i->op) {
		case Oargsb:
		case Oargub:
		case Oparsb:
		case Oparub:
			c->size = 1;
			goto Scalar;
		case Oargsh:
		case Oarguh:
		case Oparsh:
		case Oparuh:
			c->size = 2;
			goto Scalar;
		case Opar:
		case Oarg:
			c->size = 8;
			if (T.apple && !KWIDE(i->cls))
				c->size = 4;
		Scalar:
			c->align = c->size;
			*c->cls = i->cls;
			if (va) {
				c->class |= Cstk;
				c->size = 8;
				c->align = 8;
				break;
			}
			if (KBASE(i->cls) == 0 && ngp > 0) {
				ngp--;
				*c->reg = *gp++;
				break;
			}
			if (KBASE(i->cls) == 1 && nfp > 0) {
				nfp--;
				*c->reg = *fp++;
				break;
			}
			c->class |= Cstk;
			break;
		case Oparc:
		case Oargc:
			typclass(c, &typ[i->arg[0].val], gp, fp);
			if (c->ngp <= ngp) {
				if (c->nfp <= nfp) {
					ngp -= c->ngp;
					nfp -= c->nfp;
					gp += c->ngp;
					fp += c->nfp;
					break;
				} else
					nfp = 0;
			} else
				ngp = 0;
			c->class |= Cstk;
			break;
		case Opare:
		case Oarge:
			*c->reg = R9;
			*c->cls = Kl;
			envc = 1;
			break;
		case Oargv:
			va = 0;
			break;
		default:
			die("unreachable");
		}

	return envc << 14 | (gp-gpreg) << 5 | (fp-fpreg) << 9;
}

bits
powerpc_retregs(Ref r, int p[2])
{
	bits b;
	int ngp, nfp;

	assert(rtype(r) == RCall);
	ngp = r.val & 3;
	nfp = (r.val >> 2) & 7;
	if (p) {
		p[0] = ngp;
		p[1] = nfp;
	}
	b = 0;
	while (ngp--)
		b |= BIT(R3+ngp);
	while (nfp--)
		b |= BIT(F1+nfp);
	return b;
}

bits
powerpc_argregs(Ref r, int p[2])
{
	bits b;
	int ngp, nfp, x8, x9;

	assert(rtype(r) == RCall);
	ngp = (r.val >> 5) & 15;
	nfp = (r.val >> 9) & 15;
	x8 = (r.val >> 13) & 1;
	x9 = (r.val >> 14) & 1;
	if (p) {
		p[0] = ngp + x8 + x9;
		p[1] = nfp;
	}
	b = 0;
	while (ngp--)
		b |= BIT(R3+ngp);
	while (nfp--)
		b |= BIT(F1+nfp);
	return b | ((bits)x8 << R8) | ((bits)x9 << R9);
}

static void
stkblob(Ref r, Class *c, Fn *fn, Insl **ilp)
{
	Insl *il;
	int al;
	uint64_t sz;

	il = alloc(sizeof *il);
	al = c->t->align - 2; /* NAlign == 3 */
	if (al < 0)
		al = 0;
	sz = c->class & Cptr ? c->t->size : c->size;
	il->i = (Ins){Oalloc+al, Kl, r, {getcon(sz, fn)}};
	il->link = *ilp;
	*ilp = il;
}

static uint
align(uint x, uint al)
{
	return (x + al-1) & -al;
}

static void
selcall(Fn *fn, Ins *i0, Ins *i1, Insl **ilp)
{
	Ins *i;
	Class *ca, *c, cr;
	int op, cty;
	uint n, stk, off;;
	Ref r, rstk, tmp[4];

	ca = alloc((i1-i0) * sizeof ca[0]);
	cty = argsclass(i0, i1, ca);

	stk = 0;
	for (i=i0, c=ca; i<i1; i++, c++) {
		if (c->class & Cptr) {
			i->arg[0] = newtmp("abi", Kl, fn);
			stkblob(i->arg[0], c, fn, ilp);
			i->op = Oarg;
		}
		if (c->class & Cstk) {
			stk = align(stk, c->align);
			stk += c->size;
		}
	}
	stk = align(stk, 16);
	rstk = getcon(stk, fn);
	if (stk)
		emit(Oadd, Kl, TMP(R1), TMP(R1), rstk);

	if (!req(i1->arg[1], R)) {
		typclass(&cr, &typ[i1->arg[1].val], gpreg, fpreg);
		stkblob(i1->to, &cr, fn, ilp);
		cty |= (cr.nfp << 2) | cr.ngp;
		if (cr.class & Cptr) {
			/* spill & rega expect calls to be
			 * followed by copies from regs,
			 * so we emit a dummy
			 */
			cty |= 1 << 13 | 1;
			emit(Ocopy, Kw, R, TMP(R0), R);
		} else {
			sttmps(tmp, cr.cls, cr.nreg, i1->to, fn);
			for (n=0; n<cr.nreg; n++) {
				r = TMP(cr.reg[n]);
				emit(Ocopy, cr.cls[n], tmp[n], r, R);
			}
		}
	} else {
		if (KBASE(i1->cls) == 0) {
			emit(Ocopy, i1->cls, i1->to, TMP(R3), R);
			cty |= 1;
		} else {
			emit(Ocopy, i1->cls, i1->to, TMP(F1), R);
			cty |= 1 << 2;
		}
	}

	emit(Ocall, 0, R, i1->arg[0], CALL(cty));

	if (cty & (1 << 13))
		/* struct return argument */
		emit(Ocopy, Kl, TMP(R3), i1->to, R);

	/* move arguments into registers */
	for (i=i0, c=ca; i<i1; i++, c++) {
		if ((c->class & Cstk) != 0)
			continue;
		if (i->op == Oarg || i->op == Oarge)
			emit(Ocopy, *c->cls, TMP(*c->reg), i->arg[0], R);
		if (i->op == Oargc)
			ldregs(c->reg, c->cls, c->nreg, i->arg[1], fn);
	}

	/* populate the stack */
	off = 0;
	for (i=i0, c=ca; i<i1; i++, c++) {
		if ((c->class & Cstk) == 0)
			continue;
		off = align(off, c->align);
		r = newtmp("abi", Kl, fn);
		if (i->op == Oarg || isargbh(i->op)) {
			switch (c->size) {
			case 1: op = Ostoreb; break;
			case 2: op = Ostoreh; break;
			case 4:
			case 8: op = store[*c->cls]; break;
			default: die("unreachable");
			}
			emit(op, 0, R, i->arg[0], r);
		} else {
			assert(i->op == Oargc);
			emit(Oblit1, 0, R, INT(c->size), R);
			emit(Oblit0, 0, R, i->arg[1], r);
		}
		emit(Oadd, Kl, r, TMP(R1), getcon(off, fn));
		off += c->size;
	}
	if (stk)
		emit(Osub, Kl, TMP(R1), TMP(R1), rstk);

	for (i=i0, c=ca; i<i1; i++, c++)
		if (c->class & Cptr) {
			emit(Oblit1, 0, R, INT(c->t->size), R);
			emit(Oblit0, 0, R, i->arg[1], i->arg[0]);
		}
}

static Params
selpar(Fn *fn, Ins *i0, Ins *i1)
{
	Class *ca, *c, cr;
	Insl *il;
	Ins *i;
	int op, n, cty;
	uint off;
	Ref r, tmp[16], *t;

	ca = alloc((i1-i0) * sizeof ca[0]);
	curi = &insb[NIns];

	cty = argsclass(i0, i1, ca);
	fn->reg = powerpc_argregs(CALL(cty), 0);

	il = 0;
	t = tmp;
	for (i=i0, c=ca; i<i1; i++, c++) {
		if (i->op != Oparc || (c->class & (Cptr|Cstk)))
			continue;
		sttmps(t, c->cls, c->nreg, i->to, fn);
		stkblob(i->to, c, fn, &il);
		t += c->nreg;
	}
	for (; il; il=il->link)
		emiti(il->i);

	if (fn->retty >= 0) {
		typclass(&cr, &typ[fn->retty], gpreg, fpreg);
		if (cr.class & Cptr) {
			fn->retr = newtmp("abi", Kl, fn);
			emit(Ocopy, Kl, fn->retr, TMP(R8), R);
			fn->reg |= BIT(R8);
		}
	}

	t = tmp;
	off = 0;
	for (i=i0, c=ca; i<i1; i++, c++)
		if (i->op == Oparc && !(c->class & Cptr)) {
			if (c->class & Cstk) {
				off = align(off, c->align);
				fn->tmp[i->to.val].slot = -(off+2);
				off += c->size;
			} else
				for (n=0; n<c->nreg; n++) {
					r = TMP(c->reg[n]);
					emit(Ocopy, c->cls[n], *t++, r, R);
				}
		} else if (c->class & Cstk) {
			off = align(off, c->align);
			if (isparbh(i->op))
				op = Oloadsb + (i->op - Oparsb);
			else
				op = Oload;
			emit(op, *c->cls, i->to, SLOT(-(off+2)), R);
			off += c->size;
		} else {
			emit(Ocopy, *c->cls, i->to, TMP(*c->reg), R);
		}

	return (Params){
		.stk = align(off, 16),
		.ngp = (cty >> 5) & 15,
		.nfp = (cty >> 9) & 15
	};
}

void
powerpc_abi(Fn *fn)
{
	Blk *b;
	Ins *i, *i0, *ip;
	Insl *il;
	int n;
	Params p;

	for (b=fn->start; b; b=b->link)
		b->visit = 0;

	/* lower parameters */
	for (b=fn->start, i=b->ins; i<&b->ins[b->nins]; i++)
		if (!ispar(i->op))
			break;
	p = selpar(fn, b->ins, i);
	n = b->nins - (i - b->ins) + (&insb[NIns] - curi);
	i0 = alloc(n * sizeof(Ins));
	ip = icpy(ip = i0, curi, &insb[NIns] - curi);
	ip = icpy(ip, i, &b->ins[b->nins] - i);
	b->nins = n;
	b->ins = i0;

	/* lower calls, returns, and vararg instructions */
	il = 0;
	b = fn->start;
	do {
		if (!(b = b->link))
			b = fn->start; /* do it last */
		if (b->visit)
			continue;
		curi = &insb[NIns];
		selret(b, fn);
		for (i=&b->ins[b->nins]; i!=b->ins;)
			switch ((--i)->op) {
			default:
				emiti(*i);
				break;
			case Ocall:
				for (i0=i; i0>b->ins; i0--)
					if (!isarg((i0-1)->op))
						break;
				selcall(fn, i0, i, &il);
				i = i0;
				break;
			case Oarg:
			case Oargc:
				die("unreachable");
			}
		if (b == fn->start)
			for (; il; il=il->link)
				emiti(il->i);
		b->nins = &insb[NIns] - curi;
		idup(&b->ins, curi, b->nins);
	} while (b != fn->start);

	if (debug['A']) {
		fprintf(stderr, "\n> After ABI lowering:\n");
		printfn(fn, stderr);
	}
}
