/*
 * This program writes out a big logic puzzle for z3 to solve.
 *
 * Here's the idea. We want to find 121 bits (one per clock cycle) that
 * make the chip's `success` wire turn on. Instead of guessing, we
 * describe the whole chip to z3 as a math problem and let it search.
 *
 * We give every wire in the chip its own true/false variable, but not
 * just once,  once PER CLOCK CYCLE, because the same wire has a
 * different value each cycle as data moves through it. So wire "X" at
 * cycle 5 and wire "X" at cycle 6 are two completely different
 * variables to z3, tied together by whatever rule describes how that
 * wire's value gets computed.
 *
 * For a plain logic gate we just say "this wire equals this true/false
 * formula of these other wires," copied straight out of the real gate's
 * behavior in the liberty file. For a flip-flop we say "this wire NEXT
 * cycle equals D this cycle, unless reset or set is active." We do this
 * for every gate, every cycle, 122 cycles in a row (121 real data
 * cycles, plus one extra -- `success` reads a counter as a CURRENT
 * value, so it only turns on one cycle after that counter does).
 *
 * Then at the very end we say: I want cycle 122's `success` wire to be
 * true. Go find me values for the 121 input-bit variables that make
 * that possible.
 *
 * That's the whole file: read the chip, read the gate library, walk
 * through 122 cycles writing down "this wire equals this" for
 * everything, then ask the question at the end and let z3 answer it.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define NM   128
#define NCON 24
#define NINS 1024
#define NL   16384
#define NPIN 16
#define NF   512
#define NCT  128
#define NNV  4096
#define CYC  122

typedef struct { char pin[NM]; char net[NM]; } Cn;
typedef struct { char ty[NM]; char nm[NM]; Cn c[NCON]; int nc; } In;
static In ins[NINS]; static int ni = 0;
static char po[64][NM]; static char pd[64][8]; static int np = 0;
static char *lns[NL]; static int nl = 0;

static char *rd(const char *p) {
    FILE *f = fopen(p, "r");
    if (!f) { perror("fopen"); exit(1); }
    fseek(f, 0, SEEK_END); long z = ftell(f); fseek(f, 0, SEEK_SET);
    char *b = malloc((size_t)z + 1);
    fread(b, 1, (size_t)z, f); b[z] = 0; fclose(f);
    return b;
}
static void sl(char *t) {
    char *p = t;
    while (*p && nl < NL) { lns[nl++] = p; while (*p && *p != '\n') p++; if (*p == '\n') *p++ = 0; }
}
static char *ws(char *s) { while (*s == ' ' || *s == '\t') s++; return s; }
static char *rn(char *s, char *o) {
    s = ws(s); char *q = o;
    if (*s == '\\') { while (*s && *s != ')') { *q++ = *s++; if (q[-1] == ' ') break; } }
    else { while (*s && *s != ',' && *s != ')' && *s != ';') *q++ = *s++; }
    *q = 0; return s;
}
static int kw(const char *l) {
    static const char *w[] = {"module", "input", "output", "wire", "endmodule", "assign", 0};
    for (int i = 0; w[i]; i++) {
        size_t n = strlen(w[i]);
        if (!strncmp(l, w[i], n) && (l[n] == ' ' || l[n] == '\t' || l[n] == '(')) return 1;
    }
    return 0;
}
static void pp(void) {
    for (int i = 0; i < nl; i++) {
        char *l = ws(lns[i]); char k[8];
        if (sscanf(l, "%7s", k) == 1 && (!strcmp(k, "input") || !strcmp(k, "output"))) {
            char net[NM]; rn(l + strlen(k), net);
            snprintf(po[np], NM, "%s", net);
            snprintf(pd[np], 8, "%s", k);
            np++;
        }
        if (!strncmp(l, "wire", 4) || !strncmp(l, "endmodule", 9)) break;
    }
}
static int pi(char *l, In *x) {
    l = ws(l);
    if ((!isalpha((unsigned char)*l) && *l != '_') || kw(l)) return 0;
    char *p = l; while (*p && *p != ' ') p++;
    if (!*p) return 0;
    snprintf(x->ty, NM, "%.*s", (int)(p - l), l);
    l = ws(p); p = l; while (*p && *p != ' ' && *p != '(') p++;
    snprintf(x->nm, NM, "%.*s", (int)(p - l), l);
    l = ws(p);
    if (*l != '(') return 0;
    l++;
    x->nc = 0;
    while (1) {
        l = ws(l);
        if (*l != '.') break;
        l++;
        char *q = l; while (*l && *l != '(') l++;
        snprintf(x->c[x->nc].pin, NM, "%.*s", (int)(l - q), q);
        l++;
        l = rn(l, x->c[x->nc].net);
        while (*l && *l != ')') l++;
        if (*l == ')') l++;
        x->nc++;
        l = ws(l);
        if (*l == ',') { l++; continue; }
        break;
    }
    return 1;
}
static int op(const char *p) { return !strcmp(p, "X") || !strcmp(p, "Y") || !strcmp(p, "Q") || !strcmp(p, "HI") || !strcmp(p, "LO"); }
static int sq(const char *t) { return strstr(t, "__df") != NULL; }
static int cq(const char *t) { return strstr(t, "conb_1") != NULL; }

typedef struct { char nm[NM]; char pin[NPIN][NM]; char fn[NPIN][NF]; int np; int o; } LC;
static LC lc[NCT]; static int nlc = 0;
static char wt[NCT][NM]; static int nw = 0;
static int iw(const char *n) { for (int i = 0; i < nw; i++) if (!strcmp(wt[i], n)) return 1; return 0; }
static char *rq(char *s, char *o, int m) {
    s++; int i = 0;
    while (*s && *s != '"' && i < m - 1) o[i++] = *s++;
    o[i] = 0; if (*s == '"') s++;
    return s;
}
static char *sb(char *s) {
    s = strchr(s, '{'); if (!s) return s;
    int d = 0;
    do { if (*s == '{') d++; else if (*s == '}') d--; s++; } while (*s && d > 0);
    return s;
}
static void pl(char *t) {
    char *s = t;
    while ((s = strstr(s, "cell (\"")) != NULL) {
        char nm[NM]; char *a = rq(s + 6, nm, NM);
        char *e = sb(a);
        if (iw(nm) && nlc < NCT) {
            LC *c = &lc[nlc];
            snprintf(c->nm, NM, "%s", nm);
            c->np = 0; c->o = -1;
            char *p = a;
            while ((p = strstr(p, "pin (\"")) != NULL && p < e && c->np < NPIN) {
                char pn[NM]; rq(p + 5, pn, NM);
                char *b = strchr(p, '{'); char *pe = sb(p);
                snprintf(c->pin[c->np], NM, "%s", pn);
                c->fn[c->np][0] = 0;
                char *f = strstr(b, "function");
                if (f && f < pe) { char *q = strchr(f, '"'); if (q) { rq(q, c->fn[c->np], NF); c->o = c->np; } }
                c->np++;
                p = pe;
            }
            nlc++;
        }
        s = e;
    }
}
static LC *fl(const char *nm) { for (int i = 0; i < nlc; i++) if (!strcmp(lc[i].nm, nm)) return &lc[i]; return NULL; }

static char nn[NNV][NM]; static int rk[NNV]; static int nv = 0;
static int fd(const char *n) { for (int i = 0; i < nv; i++) if (!strcmp(nn[i], n)) return i; return -1; }
static int gn(const char *n) {
    int i = fd(n);
    if (i >= 0) return i;
    snprintf(nn[nv], NM, "%s", n); rk[nv] = 0;
    return nv++;
}
static int lo[NINS]; static int nz = 0;
static void lz(void) {
    for (int i = 0; i < np; i++) if (!strcmp(pd[i], "input")) rk[gn(po[i])] = 1;
    for (int i = 0; i < ni; i++)
        if (sq(ins[i].ty) || cq(ins[i].ty))
            for (int k = 0; k < ins[i].nc; k++) if (op(ins[i].c[k].pin)) rk[gn(ins[i].c[k].net)] = 1;
    int pc[NINS]; for (int i = 0; i < ni; i++) pc[i] = 0;
    int ch = 1;
    while (ch) {
        ch = 0;
        for (int i = 0; i < ni; i++) {
            if (pc[i] || sq(ins[i].ty) || cq(ins[i].ty)) continue;
            int ok = 1;
            for (int k = 0; k < ins[i].nc; k++) {
                if (op(ins[i].c[k].pin)) continue;
                if (!rk[gn(ins[i].c[k].net)]) { ok = 0; break; }
            }
            if (!ok) continue;
            for (int k = 0; k < ins[i].nc; k++) if (op(ins[i].c[k].pin)) rk[gn(ins[i].c[k].net)] = 1;
            lo[nz++] = i; pc[i] = 1; ch = 1;
        }
    }
}

static char *sn(const char *net, char *o) {
    char *q = o;
    for (const char *s = net; *s; s++) *q++ = (isalnum((unsigned char)*s) || *s == '_') ? *s : '_';
    *q = 0;
    return o;
}
static void ns(const char *net, int cy, char *o) {
    char s[NM]; sn(net, s);
    snprintf(o, NM, "n%d_%s", cy, s);
}

static const char *tp;
static char (*ts)[NM];
static char *tm;
static int tn;

static void to(char *o, size_t cap, size_t *ps);
static void tu(char *o, size_t cap, size_t *ps) {
    while (*tp == ' ') tp++;
    if (*tp == '!') {
        tp++;
        *ps += (size_t)snprintf(o + *ps, cap - *ps, "(not ");
        tu(o, cap, ps);
        *ps += (size_t)snprintf(o + *ps, cap - *ps, ")");
        return;
    }
    if (*tp == '(') {
        tp++;
        to(o, cap, ps);
        while (*tp == ' ') tp++;
        if (*tp == ')') tp++;
        return;
    }
    char nm[NM]; int i = 0;
    while (isalnum((unsigned char)*tp) || *tp == '_') nm[i++] = *tp++;
    nm[i] = 0;
    for (int k = 0; k < tn; k++)
        if (!strcmp(tm + (size_t)k * NM, nm)) { *ps += (size_t)snprintf(o + *ps, cap - *ps, "%s", ts[k]); return; }
    fprintf(stderr, "unresolved pin '%s'\n", nm); exit(1);
}
static void ta(char *o, size_t cap, size_t *ps) {
    size_t st = *ps;
    char a[4096]; size_t pa = 0;
    tu(a, sizeof a, &pa);
    while (1) {
        while (*tp == ' ') tp++;
        if (*tp != '&') break;
        tp++;
        char b[4096]; size_t pb = 0;
        tu(b, sizeof b, &pb);
        char m[8192];
        snprintf(m, sizeof m, "(and %s %s)", a, b);
        snprintf(a, sizeof a, "%s", m);
    }
    *ps = st + (size_t)snprintf(o + st, cap - st, "%s", a);
}
static void to(char *o, size_t cap, size_t *ps) {
    size_t st = *ps;
    char a[4096]; size_t pa = 0;
    ta(a, sizeof a, &pa);
    while (1) {
        while (*tp == ' ') tp++;
        if (*tp != '|') break;
        tp++;
        char b[4096]; size_t pb = 0;
        ta(b, sizeof b, &pb);
        char m[8192];
        snprintf(m, sizeof m, "(or %s %s)", a, b);
        snprintf(a, sizeof a, "%s", m);
    }
    *ps = st + (size_t)snprintf(o + st, cap - st, "%s", a);
}
static void tr(const char *ex, char sb2[][NM], char *nm2, int n, char *o, size_t cap) {
    tp = ex; ts = sb2; tm = nm2; tn = n;
    size_t ps = 0;
    to(o, cap, &ps);
}

static void rv(const char *net, int cy, char *o) {
    if (!strcmp(net, "clk") || !strcmp(net, "VGND") || !strcmp(net, "VPWR")) { snprintf(o, NM, "false"); return; }
    ns(net, cy, o);
}

static FILE *fp;

int main(int argc, char **argv) {
    if (argc < 4) { fprintf(stderr, "usage: %s puzzle.v lib.lib celltypes.txt\n", argv[0]); return 1; }

    sl(rd(argv[1]));
    pp();
    for (int i = 0; i < nl; i++) { In x; if (pi(lns[i], &x)) ins[ni++] = x; }

    FILE *wf = fopen(argv[3], "r");
    char ln[NM];
    while (fgets(ln, sizeof ln, wf) && nw < NCT) { ln[strcspn(ln, "\n")] = 0; snprintf(wt[nw++], NM, "%s", ln); }
    fclose(wf);
    pl(rd(argv[2]));
    lz();

    fp = stdout;
    fprintf(fp, "(set-option :produce-models true)\n");

    for (int i = 0; i < ni; i++) {
        if (!sq(ins[i].ty)) continue;
        char *q = NULL; int sf = 0;
        for (int k = 0; k < ins[i].nc; k++) {
            if (!strcmp(ins[i].c[k].pin, "Q")) q = ins[i].c[k].net;
            if (!strcmp(ins[i].c[k].pin, "SET_B")) sf = 1;
        }
        char y[NM]; ns(q, 0, y);
        fprintf(fp, "(declare-const %s Bool)\n(assert (= %s %s))\n", y, y, sf ? "true" : "false");
    }
    char iy[CYC][NM];
    for (int c = 0; c < CYC; c++) {
        for (int i = 0; i < ni; i++) {
            if (!cq(ins[i].ty)) continue;
            for (int k = 0; k < ins[i].nc; k++) {
                char y[NM]; ns(ins[i].c[k].net, c, y);
                fprintf(fp, "(declare-const %s Bool)\n(assert (= %s %s))\n",
                        y, y, !strcmp(ins[i].c[k].pin, "HI") ? "true" : "false");
            }
        }

        char is[NM]; snprintf(is, NM, "I_%d", c); snprintf(iy[c], NM, "%s", is);
        fprintf(fp, "(declare-const %s Bool)\n", is);
        char ey[NM], ry[NM];
        ns("enable", c, ey); ns("rst_n", c, ry);
        fprintf(fp, "(declare-const %s Bool)\n(assert (= %s true))\n", ey, ey);
        fprintf(fp, "(declare-const %s Bool)\n(assert (= %s true))\n", ry, ry);
        char iN[NM]; ns("I", c, iN);
        fprintf(fp, "(declare-const %s Bool)\n(assert (= %s %s))\n", iN, iN, is);

        for (int j = 0; j < nz; j++) {
            In *x = &ins[lo[j]];
            LC *L = fl(x->ty);
            char sb2[NPIN][NM]; char nm2[NPIN][NM]; int n = 0; char oy[NM]; int has = 0;
            for (int k = 0; k < x->nc; k++) {
                int lp = -1;
                for (int p = 0; p < L->np; p++) if (!strcmp(L->pin[p], x->c[k].pin)) { lp = p; break; }
                if (lp == L->o) { ns(x->c[k].net, c, oy); has = 1; continue; }
                if (lp < 0) continue;
                snprintf(nm2[n], NM, "%s", x->c[k].pin);
                rv(x->c[k].net, c, sb2[n]);
                n++;
            }
            if (!has) continue;
            fprintf(fp, "(declare-const %s Bool)\n", oy);
            char ex[8192];
            tr(L->fn[L->o], sb2, (char *)nm2, n, ex, sizeof ex);
            fprintf(fp, "(assert (= %s %s))\n", oy, ex);
        }

        for (int i = 0; i < ni; i++) {
            if (!sq(ins[i].ty)) continue;
            char *d = NULL, *r = NULL, *s = NULL, *q = NULL;
            for (int k = 0; k < ins[i].nc; k++) {
                if (!strcmp(ins[i].c[k].pin, "D")) d = ins[i].c[k].net;
                else if (!strcmp(ins[i].c[k].pin, "RESET_B")) r = ins[i].c[k].net;
                else if (!strcmp(ins[i].c[k].pin, "SET_B")) s = ins[i].c[k].net;
                else if (!strcmp(ins[i].c[k].pin, "Q")) q = ins[i].c[k].net;
            }
            char dy[NM]; ns(d, c, dy);
            char qn[NM]; ns(q, c + 1, qn);
            fprintf(fp, "(declare-const %s Bool)\n", qn);
            if (r) {
                char ry2[NM]; ns(r, c, ry2);
                fprintf(fp, "(assert (= %s (and %s %s)))\n", qn, dy, ry2);
            } else if (s) {
                char sy[NM]; ns(s, c, sy);
                fprintf(fp, "(assert (= %s (or %s (not %s))))\n", qn, dy, sy);
            } else {
                fprintf(fp, "(assert (= %s %s))\n", qn, dy);
            }
        }
    }

    char gy[NM]; ns("success", CYC, gy);
    fprintf(fp, "(assert (= %s true))\n", gy);
    fprintf(fp, "(check-sat)\n(get-value (");
    for (int c = 0; c < CYC; c++) fprintf(fp, "%s ", iy[c]);
    fprintf(fp, "))\n");

    fprintf(stderr, "emitted %d cycles\n", CYC);
    return 0;
}
