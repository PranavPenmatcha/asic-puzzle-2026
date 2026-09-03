#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define NM   128
#define NF   512
#define NPIN 16
#define NCT  128

static int ispower(const char *p) {
    return !strcmp(p, "VGND") || !strcmp(p, "VNB") || !strcmp(p, "VPB") || !strcmp(p, "VPWR");
}

typedef struct { char name[NM]; char pin[NPIN][NM]; char func[NPIN][NF]; int np; int out; } Cell;
static Cell c[NCT]; static int nc = 0;

/* --- liberty parsing (same technique as tools/cells.c) --- */

static char *rd(const char *p) {
    FILE *f = fopen(p, "r");
    if (!f) { perror("fopen"); exit(1); }
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    char *b = malloc((size_t)sz + 1);
    fread(b, 1, (size_t)sz, f); b[sz] = 0; fclose(f);
    return b;
}

static char *readq(char *s, char *out, int max) {
    s++; int i = 0;
    while (*s && *s != '"' && i < max - 1) out[i++] = *s++;
    out[i] = 0;
    if (*s == '"') s++;
    return s;
}

static char *skipblock(char *s) {
    s = strchr(s, '{');
    if (!s) return s;
    int depth = 0;
    do { if (*s == '{') depth++; else if (*s == '}') depth--; s++; } while (*s && depth > 0);
    return s;
}

static char wanted[NCT][NM]; static int nwanted = 0;
static int iswanted(const char *n) { for (int i = 0; i < nwanted; i++) if (!strcmp(wanted[i], n)) return 1; return 0; }

static void parseliberty(char *text) {
    char *s = text;
    while ((s = strstr(s, "cell (\"")) != NULL) {
        char name[NM]; char *after = readq(s + 6, name, NM);
        char *blockend = skipblock(after);
        if (iswanted(name) && nc < NCT) {
            Cell *cc = &c[nc];
            snprintf(cc->name, NM, "%s", name);
            cc->np = 0; cc->out = -1;
            char *p = after;
            while ((p = strstr(p, "pin (\"")) != NULL && p < blockend && cc->np < NPIN) {
                char pname[NM]; readq(p + 5, pname, NM);
                char *pblock = strchr(p, '{');
                char *pend = skipblock(p);
                snprintf(cc->pin[cc->np], NM, "%s", pname);
                cc->func[cc->np][0] = 0;
                char *fn = strstr(pblock, "function");
                if (fn && fn < pend) {
                    char *fq = strchr(fn, '"');
                    if (fq) { readq(fq, cc->func[cc->np], NF); cc->out = cc->np; }
                }
                cc->np++;
                p = pend;
            }
            nc++;
        }
        s = blockend;
    }
}

/* --- boolean expression evaluator: '!','&','|','(',')', identifiers --- */

static const char *ep;
static char evnames[NPIN][NM]; static int evvals[NPIN]; static int nev;

static int lookup(const char *name) {
    for (int i = 0; i < nev; i++) if (!strcmp(evnames[i], name)) return evvals[i];
    fprintf(stderr, "unknown var '%s'\n", name); exit(1);
}

static int eval_or(void);

static int eval_unary(void) {
    while (*ep == ' ') ep++;
    if (*ep == '!') { ep++; return !eval_unary(); }
    if (*ep == '(') { ep++; int v = eval_or(); while (*ep == ' ') ep++; if (*ep == ')') ep++; return v; }
    char name[NM]; int i = 0;
    while (isalnum((unsigned char)*ep) || *ep == '_') name[i++] = *ep++;
    name[i] = 0;
    return lookup(name);
}

static int eval_and(void) {
    int v = eval_unary();
    while (1) { while (*ep == ' ') ep++; if (*ep != '&') break; ep++; int r = eval_unary(); v &= r; }
    return v;
}

static int eval_or(void) {
    int v = eval_and();
    while (1) { while (*ep == ' ') ep++; if (*ep != '|') break; ep++; int r = eval_and(); v |= r; }
    return v;
}

static int evalfunc(const char *expr, char names[][NM], int vals[], int n) {
    ep = expr;
    memcpy(evnames, names, (size_t)n * NM);
    memcpy(evvals, vals, (size_t)n * sizeof(int));
    nev = n;
    return eval_or();
}

/* --- generate a testbench instantiating every cell, driving all 2^k input combos --- */

static void gentb(FILE *out, int inpins[NCT][NPIN], int ninp[NCT]) {
    fprintf(out, "`timescale 1ns/1ps\nmodule tb;\n");
    for (int i = 0; i < nc; i++) {
        for (int k = 0; k < ninp[i]; k++) fprintf(out, "  reg c%d_%s;\n", i, c[i].pin[inpins[i][k]]);
        fprintf(out, "  wire c%d_out;\n", i);
        fprintf(out, "  %s u%d (", c[i].name, i);
        for (int k = 0; k < ninp[i]; k++) fprintf(out, ".%s(c%d_%s), ", c[i].pin[inpins[i][k]], i, c[i].pin[inpins[i][k]]);
        fprintf(out, ".%s(c%d_out));\n", c[i].pin[c[i].out], i);
    }
    fprintf(out, "  integer i;\n  initial begin\n");
    for (int i = 0; i < nc; i++) {
        int rows = 1 << ninp[i];
        fprintf(out, "    for (i=0;i<%d;i=i+1) begin\n", rows);
        for (int k = 0; k < ninp[i]; k++) fprintf(out, "      c%d_%s=(i>>%d)&1;\n", i, c[i].pin[inpins[i][k]], k);
        fprintf(out, "      #1; $display(\"R %s %%0d %%0d\", i, c%d_out);\n", c[i].name, i);
        fprintf(out, "    end\n");
    }
    fprintf(out, "    $finish;\n  end\nendmodule\n");
}

int main(int argc, char **argv) {
    if (argc < 4) { fprintf(stderr, "usage: %s lib.lib celltypes.txt PDK_verilog_dir\n", argv[0]); return 1; }

    FILE *wf = fopen(argv[2], "r");
    char line[NM];
    while (fgets(line, sizeof line, wf) && nwanted < NCT) {
        line[strcspn(line, "\n")] = 0;
        if (strstr(line, "__conb_1") || strstr(line, "__df")) continue; /* not simple combinational funcs */
        snprintf(wanted[nwanted++], NM, "%s", line);
    }
    fclose(wf);

    parseliberty(rd(argv[1]));
    printf("parsed %d combinational cell types\n", nc);

    int inpins[NCT][NPIN], ninp[NCT], total = 0;
    for (int i = 0; i < nc; i++) {
        ninp[i] = 0;
        for (int p = 0; p < c[i].np; p++) {
            if (p == c[i].out || ispower(c[i].pin[p])) continue;
            inpins[i][ninp[i]++] = p;
        }
        total += 1 << ninp[i];
    }

    char tbpath[] = "/tmp/vc_tb.v";
    FILE *tb = fopen(tbpath, "w");
    gentb(tb, inpins, ninp);
    fclose(tb);

    char cmd[2048];
    snprintf(cmd, sizeof cmd, "iverilog -g2012 -o /tmp/vc_tb.out %s/primitives.v %s/sky130_fd_sc_hd.v %s 2>&1",
             argv[3], argv[3], tbpath);
    FILE *p1 = popen(cmd, "r");
    char buf[512]; int compileerr = 0;
    while (fgets(buf, sizeof buf, p1)) { fputs(buf, stderr); compileerr = 1; }
    pclose(p1);
    if (compileerr) fprintf(stderr, "(compile warnings above; checking whether it still ran)\n");

    FILE *p2 = popen("vvp /tmp/vc_tb.out", "r");
    int mismatches = 0, checked = 0;
    char cellname[NM]; int row, got;
    while (fgets(buf, sizeof buf, p2)) {
        if (sscanf(buf, "R %s %d %d", cellname, &row, &got) != 3) continue;
        int ci = -1;
        for (int i = 0; i < nc; i++) if (!strcmp(c[i].name, cellname)) { ci = i; break; }
        if (ci < 0) continue;
        char names[NPIN][NM]; int vals[NPIN];
        for (int k = 0; k < ninp[ci]; k++) {
            snprintf(names[k], NM, "%s", c[ci].pin[inpins[ci][k]]);
            vals[k] = (row >> k) & 1;
        }
        int want = evalfunc(c[ci].func[c[ci].out], names, vals, ninp[ci]);
        checked++;
        if (want != got) {
            mismatches++;
            fprintf(stderr, "MISMATCH %s row %d: c-eval=%d iverilog=%d\n", cellname, row, want, got);
        }
    }
    pclose(p2);

    printf("checked %d vectors (expected %d), %d mismatches\n", checked, total, mismatches);
    return mismatches ? 1 : 0;
}
