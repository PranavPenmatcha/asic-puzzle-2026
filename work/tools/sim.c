#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

typedef unsigned long long u64;

#define NM   128
#define NCON 24
#define NINS 1024
#define NL   16384
#define NPIN 16
#define NF   512
#define NCT  128
#define NNETV 4096


typedef struct { char pin[NM]; char net[NM]; } Conn;
typedef struct { char type[NM]; char name[NM]; Conn c[NCON]; int nc; } Inst;
static Inst ins[NINS]; static int nin = 0;
static char ports[64][NM]; static char dir[64][8]; static int nports = 0;
static char *lns[NL]; static int nl = 0;

static char *rd(const char *p) {
    FILE *f = fopen(p, "r");
    if (!f) { perror("fopen"); exit(1); }
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    char *b = malloc((size_t)sz + 1);
    fread(b, 1, (size_t)sz, f); b[sz] = 0; fclose(f);
    return b;
}
static void sl(char *t) {
    char *p = t;
    while (*p && nl < NL) { lns[nl++] = p; while (*p && *p != '\n') p++; if (*p == '\n') *p++ = 0; }
}
static char *ws(char *s) { while (*s == ' ' || *s == '\t') s++; return s; }

static char *readnet(char *s, char *out) {
    s = ws(s); char *o = out;
    if (*s == '\\') { while (*s && *s != ')') { *o++ = *s++; if (s[-1] == ' ') break; } }
    else { while (*s && *s != ',' && *s != ')' && *s != ';') *o++ = *s++; }
    *o = 0; return s;
}

static int iskeyword(const char *l) {
    static const char *kw[] = {"module", "input", "output", "wire", "endmodule", "assign", 0};
    for (int i = 0; kw[i]; i++) {
        size_t n = strlen(kw[i]);
        if (!strncmp(l, kw[i], n) && (l[n] == ' ' || l[n] == '\t' || l[n] == '(')) return 1;
    }
    return 0;
}

static void parseports(void) {
    for (int i = 0; i < nl; i++) {
        char *l = ws(lns[i]); char kind[8];
        if (sscanf(l, "%7s", kind) == 1 && (!strcmp(kind, "input") || !strcmp(kind, "output"))) {
            char net[NM]; readnet(l + strlen(kind), net);
            snprintf(ports[nports], NM, "%s", net);
            snprintf(dir[nports], 8, "%s", kind);
            nports++;
        }
        if (!strncmp(l, "wire", 4) || !strncmp(l, "endmodule", 9)) break;
    }
}

static int parseinst(char *l, Inst *x) {
    l = ws(l);
    if ((!isalpha((unsigned char)*l) && *l != '_') || iskeyword(l)) return 0;
    char *p = l; while (*p && *p != ' ') p++;
    if (!*p) return 0;
    snprintf(x->type, NM, "%.*s", (int)(p - l), l);
    l = ws(p); p = l; while (*p && *p != ' ' && *p != '(') p++;
    snprintf(x->name, NM, "%.*s", (int)(p - l), l);
    l = ws(p);
    if (*l != '(') return 0;
    l++;
    x->nc = 0;
    while (1) {
        l = ws(l);
        if (*l != '.') break;
        l++;
        char *pn = l; while (*l && *l != '(') l++;
        snprintf(x->c[x->nc].pin, NM, "%.*s", (int)(l - pn), pn);
        l++;
        l = readnet(l, x->c[x->nc].net);
        while (*l && *l != ')') l++;
        if (*l == ')') l++;
        x->nc++;
        l = ws(l);
        if (*l == ',') { l++; continue; }
        break;
    }
    return 1;
}

static int isoutpin(const char *p) {
    return !strcmp(p, "X") || !strcmp(p, "Y") || !strcmp(p, "Q") || !strcmp(p, "HI") || !strcmp(p, "LO");
}
static int isseq(const char *type) { return strstr(type, "__df") != NULL; }
static int isconst(const char *type) { return strstr(type, "conb_1") != NULL; }


typedef struct { char name[NM]; char pin[NPIN][NM]; char func[NPIN][NF]; int np; int out; } LCell;
static LCell lc[NCT]; static int nlc = 0;
static char wanted[NCT][NM]; static int nwanted = 0;
static int iswanted(const char *n) { for (int i = 0; i < nwanted; i++) if (!strcmp(wanted[i], n)) return 1; return 0; }

static char *readq(char *s, char *out, int max) {
    s++; int i = 0;
    while (*s && *s != '"' && i < max - 1) out[i++] = *s++;
    out[i] = 0; if (*s == '"') s++;
    return s;
}
static char *skipblock(char *s) {
    s = strchr(s, '{'); if (!s) return s;
    int depth = 0;
    do { if (*s == '{') depth++; else if (*s == '}') depth--; s++; } while (*s && depth > 0);
    return s;
}
static void parseliberty(char *text) {
    char *s = text;
    while ((s = strstr(s, "cell (\"")) != NULL) {
        char name[NM]; char *after = readq(s + 6, name, NM);
        char *blockend = skipblock(after);
        if (iswanted(name) && nlc < NCT) {
            LCell *cc = &lc[nlc];
            snprintf(cc->name, NM, "%s", name);
            cc->np = 0; cc->out = -1;
            char *p = after;
            while ((p = strstr(p, "pin (\"")) != NULL && p < blockend && cc->np < NPIN) {
                char pname[NM]; readq(p + 5, pname, NM);
                char *pblock = strchr(p, '{'); char *pend = skipblock(p);
                snprintf(cc->pin[cc->np], NM, "%s", pname);
                cc->func[cc->np][0] = 0;
                char *fn = strstr(pblock, "function");
                if (fn && fn < pend) { char *fq = strchr(fn, '"'); if (fq) { readq(fq, cc->func[cc->np], NF); cc->out = cc->np; } }
                cc->np++;
                p = pend;
            }
            nlc++;
        }
        s = blockend;
    }
}
static LCell *findlc(const char *name) { for (int i = 0; i < nlc; i++) if (!strcmp(lc[i].name, name)) return &lc[i]; return NULL; }

static const char *ep;
static char evnames[NPIN][NM]; static u64 evvals[NPIN]; static int nev;
static u64 lookup(const char *name) {
    for (int i = 0; i < nev; i++) if (!strcmp(evnames[i], name)) return evvals[i];
    fprintf(stderr, "unknown var '%s'\n", name); exit(1);
}
static u64 eval_or(void);
static u64 eval_unary(void) {
    while (*ep == ' ') ep++;
    if (*ep == '!') { ep++; return ~eval_unary(); }
    if (*ep == '(') { ep++; u64 v = eval_or(); while (*ep == ' ') ep++; if (*ep == ')') ep++; return v; }
    char name[NM]; int i = 0;
    while (isalnum((unsigned char)*ep) || *ep == '_') name[i++] = *ep++;
    name[i] = 0;
    return lookup(name);
}
static u64 eval_and(void) {
    u64 v = eval_unary();
    while (1) { while (*ep == ' ') ep++; if (*ep != '&') break; ep++; u64 r = eval_unary(); v &= r; }
    return v;
}
static u64 eval_or(void) {
    u64 v = eval_and();
    while (1) { while (*ep == ' ') ep++; if (*ep != '|') break; ep++; u64 r = eval_and(); v |= r; }
    return v;
}
static u64 evalfunc(const char *expr, char names[][NM], u64 vals[], int n) {
    ep = expr; memcpy(evnames, names, (size_t)n * NM); memcpy(evvals, vals, (size_t)n * sizeof(u64));
    nev = n;
    return eval_or();
}
static char netname[NNETV][NM]; static u64 netval[NNETV]; static int ready[NNETV]; static int nnetv = 0;
static int findnet(const char *n) { for (int i = 0; i < nnetv; i++) if (!strcmp(netname[i], n)) return i; return -1; }
static int getnet(const char *n) {
    int i = findnet(n);
    if (i >= 0) return i;
    snprintf(netname[nnetv], NM, "%s", n); netval[nnetv] = 0; ready[nnetv] = 0;
    return nnetv++;
}

static int levelorder[NINS]; static int nlevel = 0;

static void levelize(void) {
    for (int i = 0; i < nports; i++) if (!strcmp(dir[i], "input")) ready[getnet(ports[i])] = 1;
    for (int i = 0; i < nin; i++)
        if (isseq(ins[i].type) || isconst(ins[i].type))
            for (int k = 0; k < ins[i].nc; k++)
                if (isoutpin(ins[i].c[k].pin)) ready[getnet(ins[i].c[k].net)] = 1;

    int placed[NINS]; for (int i = 0; i < nin; i++) placed[i] = 0;
    int changed = 1;
    while (changed) {
        changed = 0;
        for (int i = 0; i < nin; i++) {
            if (placed[i] || isseq(ins[i].type) || isconst(ins[i].type)) continue;
            int allready = 1;
            for (int k = 0; k < ins[i].nc; k++) {
                if (isoutpin(ins[i].c[k].pin)) continue;
                if (!ready[getnet(ins[i].c[k].net)]) { allready = 0; break; }
            }
            if (!allready) continue;
            for (int k = 0; k < ins[i].nc; k++) if (isoutpin(ins[i].c[k].pin)) ready[getnet(ins[i].c[k].net)] = 1;
            levelorder[nlevel++] = i; placed[i] = 1; changed = 1;
        }
    }
    int ncomb = 0;
    for (int i = 0; i < nin; i++) if (!isseq(ins[i].type) && !isconst(ins[i].type)) ncomb++;
    fprintf(stderr, "levelized %d of %d combinational cells\n", nlevel, ncomb);
    if (nlevel != ncomb) { fprintf(stderr, "levelize incomplete -- cycle in combinational logic?\n"); exit(1); }
}
static void evalcomb(void) {
    for (int idx = 0; idx < nlevel; idx++) {
        Inst *x = &ins[levelorder[idx]];
        LCell *L = findlc(x->type);
        char names[NPIN][NM]; u64 vals[NPIN]; int n = 0; int outnet = -1;
        for (int k = 0; k < x->nc; k++) {
            int lp = -1;
            for (int p = 0; p < L->np; p++) if (!strcmp(L->pin[p], x->c[k].pin)) { lp = p; break; }
            if (lp == L->out) { outnet = getnet(x->c[k].net); continue; }
            if (lp < 0) continue; /* power pins etc, not in liberty pin table for this func */
            snprintf(names[n], NM, "%s", x->c[k].pin);
            vals[n] = netval[getnet(x->c[k].net)];
            n++;
        }
        netval[outnet] = evalfunc(L->func[L->out], names, vals, n);
    }
}

static const u64 ALL1 = ~(u64)0;

static void step(void) {
    evalcomb();
    static int first = 1;
    static u64 newq[NINS];
    for (int i = 0; i < nin; i++) {
        if (!isseq(ins[i].type)) continue;
        char *dnet = NULL, *rnet = NULL, *snet = NULL, *qnet = NULL;
        for (int k = 0; k < ins[i].nc; k++) {
            if (!strcmp(ins[i].c[k].pin, "D")) dnet = ins[i].c[k].net;
            else if (!strcmp(ins[i].c[k].pin, "RESET_B")) rnet = ins[i].c[k].net;
            else if (!strcmp(ins[i].c[k].pin, "SET_B")) snet = ins[i].c[k].net;
            else if (!strcmp(ins[i].c[k].pin, "Q")) qnet = ins[i].c[k].net;
        }
        u64 d = netval[getnet(dnet)];
        if (rnet) { u64 r = netval[getnet(rnet)]; newq[i] = d & r; }
        else if (snet) { u64 s = netval[getnet(snet)]; newq[i] = (d & s) | ~s; }
        else newq[i] = d;
        (void)qnet;
    }
    for (int i = 0; i < nin; i++) if (isseq(ins[i].type)) {
        char *qnet = NULL;
        for (int k = 0; k < ins[i].nc; k++) if (!strcmp(ins[i].c[k].pin, "Q")) qnet = ins[i].c[k].net;
        netval[getnet(qnet)] = newq[i];
    }
    (void)first;
}

static void setinput(const char *name, u64 v) { netval[getnet(name)] = v; }
static u64 getval(const char *name) { int i = findnet(name); return i < 0 ? 0 : netval[i]; }

static void resetpulse(int cycles) {
    setinput("rst_n", 0);
    for (int i = 0; i < cycles; i++) step();
    setinput("rst_n", ALL1);
}

int main(int argc, char **argv) {
    if (argc < 4) { fprintf(stderr, "usage: %s puzzle.v lib.lib celltypes.txt [121bits.txt]\n", argv[0]); return 1; }

    sl(rd(argv[1]));
    parseports();
    for (int i = 0; i < nl; i++) { Inst x; if (parseinst(lns[i], &x)) ins[nin++] = x; }

    FILE *wf = fopen(argv[3], "r");
    char line[NM];
    while (fgets(line, sizeof line, wf) && nwanted < NCT) { line[strcspn(line, "\n")] = 0; snprintf(wanted[nwanted++], NM, "%s", line); }
    fclose(wf);
    parseliberty(rd(argv[2]));

    /* conb_1 outputs are compile-time constants, not liberty-evaluated */
    for (int i = 0; i < nin; i++) {
        if (!isconst(ins[i].type)) continue;
        for (int k = 0; k < ins[i].nc; k++) {
            if (!strcmp(ins[i].c[k].pin, "HI")) netval[getnet(ins[i].c[k].net)] = ALL1;
            else if (!strcmp(ins[i].c[k].pin, "LO")) netval[getnet(ins[i].c[k].net)] = 0;
        }
    }

    levelize();

    resetpulse(3);
    setinput("enable", 0); setinput("I", 0);
    setinput("enable", ALL1);

    if (argc >= 5) {
        /* replay a real 121-bit input (one char '0'/'1' per cycle) */
        char *bits = rd(argv[4]);
        for (int i = 0; bits[i] == '0' || bits[i] == '1'; i++) {
            setinput("I", bits[i] == '1' ? ALL1 : 0);
            step();
        }
        step(); 
    } else {
       u64 iv = 0;
        for (int i = 0; i < 40; i++) { iv = ~iv; setinput("I", iv); step(); }
    }

    printf("success=%llu O[7:0]=", (unsigned long long)(getval("success") & 1));
    for (int b = 7; b >= 0; b--) {
        char nm[NM]; snprintf(nm, NM, "\\O[%d] ", b);
        printf("%llu", (unsigned long long)(getval(nm) & 1));
    }
    printf("\n");
    return 0;
}
