#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define NM 128
#define NP 24
#define NCT 128
#define NIN 2048
#define NL 8192
#define NSN 8192

typedef struct { char name[NM]; char pins[NP][NM]; int np; } CT;
typedef struct { char name[NM]; char ct[NM]; char nets[NP][NM]; int nn; } IN;
typedef struct { char name[NM]; int s, e; } SR;

static CT cts[NCT]; static int nct = 0;
static IN ins[NIN]; static int nin = 0;
static SR srs[NCT]; static int nsr = 0;
static char *lns[NL]; static int nl = 0;
static char seen[NSN][NM]; static int nsn = 0;

/* reads a whole file into a malloc'd buffer */
static char *rd(const char *p) {
    FILE *f = fopen(p, "r");
    if (!f) { perror("fopen"); exit(1); }
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    char *b = malloc((size_t)sz + 1);
    fread(b, 1, (size_t)sz, f); b[sz] = 0; fclose(f);
    return b;
}

/* merges SPICE '+' continuation lines into the line before them */
static char *jc(const char *t) {
    size_t n = strlen(t), oi = 0; int bol = 1;
    char *o = malloc(n + 1);
    for (size_t i = 0; i < n; i++) {
        if (bol && t[i] == '+') {
            if (oi && o[oi - 1] == '\n') o[oi - 1] = ' ';
            i++; while (t[i] == ' ' || t[i] == '\t') i++; i--;
            bol = 0; continue;
        }
        o[oi++] = t[i]; bol = (t[i] == '\n');
    }
    o[oi] = 0;
    return o;
}

/* splits joined text into an array of null-terminated lines, in place */
static void sl(char *t) {
    char *p = t;
    while (*p && nl < NL) {
        lns[nl++] = p;
        while (*p && *p != '\n') p++;
        if (*p == '\n') *p++ = 0;
    }
}

static char *ws(char *s) { while (*s == ' ' || *s == '\t') s++; return s; }

/* whitespace-tokenizes a line in place, strtok-style */
static int tok(char *l, char t[][NM], int max) {
    int n = 0;
    for (char *w = strtok(l, " \t"); w && n < max; w = strtok(NULL, " \t"), n++)
        snprintf(t[n], NM, "%s", w);
    return n;
}

/* parses every .subckt block into a pin-name table, tracking each one's line range */
static void p1(void) {
    for (int i = 0; i < nl; ) {
        char *l = ws(lns[i]);
        if (strncmp(l, ".subckt", 7)) { i++; continue; }

        char b[2048]; snprintf(b, sizeof b, "%s", l);
        char t[NP + 2][NM]; int nt = tok(b, t, NP + 2);
        CT *c = &cts[nct++];
        snprintf(c->name, NM, "%s", t[1]);
        for (c->np = 0; c->np + 2 < nt; c->np++) snprintf(c->pins[c->np], NM, "%s", t[c->np + 2]);

        int j = i + 1;
        while (j < nl && strncmp(ws(lns[j]), ".ends", 5)) j++;
        SR *s = &srs[nsr++];
        snprintf(s->name, NM, "%s", t[1]); s->s = i; s->e = j;
        i = j + 1;
    }
}

/* pulls X-line instances out of the last (top-level) subckt */
static void p2(void) {
    if (!nsr) { fprintf(stderr, "no .subckt blocks found\n"); exit(1); }
    SR *top = &srs[nsr - 1];
    for (int i = top->s + 1; i < top->e; i++) {
        char *l = ws(lns[i]);
        if (l[0] != 'X') continue;
        char t[NP + 4][NM]; int nt = tok(l, t, NP + 4);
        if (nt < 2) continue;
        IN *x = &ins[nin++];
        snprintf(x->name, NM, "%s", t[0] + 1);
        snprintf(x->ct, NM, "%s", t[nt - 1]);
        x->nn = 0;
        for (int k = 1; k < nt - 1; k++) snprintf(x->nets[x->nn++], NM, "%s", t[k]);
    }
}

/* true if a cell type is a real logic gate, not a via/decap/diode/tap */
static int islogic(const char *ct) {
    static const char *skip[] = {"VIA_", "sky130_fd_sc_hd__decap", "sky130_fd_sc_hd__diode", "sky130_fd_sc_hd__tapvpwrvgnd", 0};
    for (int i = 0; skip[i]; i++) if (!strncmp(ct, skip[i], strlen(skip[i]))) return 0;
    return 1;
}

static int ispwr(const char *p) { return !strcmp(p, "VPWR") || !strcmp(p, "VGND") || !strcmp(p, "VPB") || !strcmp(p, "VNB"); }
static int idc(char c) { return isalnum((unsigned char)c) || c == '_'; }

/* turns a raw SPICE net name into a legal (or escaped) Verilog identifier */
static void san(const char *n, char *o, size_t sz) {
    size_t len = strlen(n);
    int plain = isalpha((unsigned char)n[0]) || n[0] == '_';
    for (size_t i = 1; plain && i < len; i++) plain = idc(n[i]);
    if (plain) { snprintf(o, sz, "%s", n); return; }
    if (strchr(n, '/') && !strchr(n, '[')) {
        char b[NM]; size_t i;
        for (i = 0; i < len && i < sizeof b - 1; i++) b[i] = idc(n[i]) ? n[i] : '_';
        b[i] = 0; snprintf(o, sz, "%s", b); return;
    }
    snprintf(o, sz, "\\%s ", n);
}

static CT *findct(const char *n) { for (int i = 0; i < nct; i++) if (!strcmp(cts[i].name, n)) return &cts[i]; return NULL; }
static int istop(const char *n) { CT *t = &cts[nct - 1]; for (int i = 0; i < t->np; i++) if (!strcmp(t->pins[i], n)) return 1; return 0; }
static int haveseen(const char *s) { for (int i = 0; i < nsn; i++) if (!strcmp(seen[i], s)) return 1; return 0; }
static void markseen(const char *s) { if (nsn < NSN) snprintf(seen[nsn++], NM, "%s", s); }

/* writes the Verilog module: port list, wire decls, then one instance per gate */
static void emit(FILE *out) {
    CT *top = &cts[nct - 1];

    fprintf(out, "module %s (", top->name);
    for (int i = 0; i < top->np; i++) { char s[NM]; san(top->pins[i], s, sizeof s); fprintf(out, "%s%s", s, i + 1 < top->np ? ", " : ""); }
    fprintf(out, ");\n");

    for (int i = 0; i < top->np; i++) {
        char s[NM]; san(top->pins[i], s, sizeof s);
        int isout = !strncmp(top->pins[i], "O[", 2) || !strcmp(top->pins[i], "success");
        fprintf(out, "  %s %s;\n", isout ? "output" : "input", s);
    }
    fprintf(out, "\n");

    static char buf[1024 * 1024]; size_t bi = 0; int skip = 0;
    for (int i = 0; i < nin; i++) {
        IN *x = &ins[i];
        if (!islogic(x->ct)) { skip++; continue; }
        CT *c = findct(x->ct);
        if (!c) { fprintf(stderr, "WARNING: unknown cell type '%s' (%s)\n", x->ct, x->name); continue; }
        if (c->np != x->nn) { fprintf(stderr, "WARNING: pin count mismatch for %s\n", x->name); continue; }

        char in_[NM]; san(x->name, in_, sizeof in_);
        char conn[8192] = ""; int first = 1;
        for (int p = 0; p < c->np; p++) {
            if (ispwr(c->pins[p])) continue;
            char ns[NM]; san(x->nets[p], ns, sizeof ns);
            if (!istop(x->nets[p]) && !haveseen(ns)) markseen(ns);
            char piece[NM * 2]; snprintf(piece, sizeof piece, "%s.%s(%s)", first ? "" : ", ", c->pins[p], ns);
            strncat(conn, piece, sizeof conn - strlen(conn) - 1);
            first = 0;
        }
        bi += (size_t)snprintf(buf + bi, sizeof buf - bi, "  %s %s (%s);\n", x->ct, in_, conn);
    }

    for (int i = 0; i < nsn; i++) fprintf(out, "  wire %s;\n", seen[i]);
    fprintf(out, "\n%sendmodule\n", buf);
    fprintf(stderr, "wrote %d instances (%d skipped), %d wires\n", nin - skip, skip, nsn);
}

int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: %s in.spice out.v\n", argv[0]); return 1; }
    sl(jc(rd(argv[1])));
    p1(); p2();
    FILE *out = fopen(argv[2], "w");
    if (!out) { perror("fopen"); return 1; }
    emit(out);
    fclose(out);
    fprintf(stderr, "%d cell types, top = '%s'\n", nct, cts[nct - 1].name);
    return 0;
}
