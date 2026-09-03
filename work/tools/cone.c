#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define NM   128
#define NCON 24
#define NINS 1024
#define NL   16384

typedef struct { char pin[NM]; char net[NM]; } Conn;
typedef struct { char type[NM]; char name[NM]; Conn c[NCON]; int nc; } Inst;
static Inst ins[NINS]; static int nin = 0;
static char ports[64][NM]; static char dir[64][8]; static int nports = 0;
static char *lns[NL]; static int nl = 0;

/* --- same parsing as tools/netlist.c --- */

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

/* driver[net] = instance index whose output pin drives it; -1 = primary
 * input; -2 (via NOT finding it) = nothing knows this net yet */
static char drvnet[4096][NM]; static int drvidx[4096]; static int ndrv = 0;
static int finddrv(const char *net) {
    for (int i = 0; i < ndrv; i++) if (!strcmp(drvnet[i], net)) return drvidx[i];
    return -2;
}
static void setdrv(const char *net, int idx) {
    if (finddrv(net) != -2) return;
    snprintf(drvnet[ndrv], NM, "%s", net); drvidx[ndrv] = idx; ndrv++;
}

/* --- fan-in cone --- */

static int visited[NINS];
static int cone[NINS]; static int ncone = 0;
static int coneflops[NINS]; static int nconeflops = 0;

/* walks backward from `net`, through every instance that drives something
 * on the path, all the way to primary inputs. Does NOT stop at flops --
 * this matches "the full transitive cone including clock/reset trees",
 * which is what the original solve used for the headline 484-cell,
 * 79-flop numbers for `success`. */
static void walk(const char *net) {
    int d = finddrv(net);
    if (d < 0) return; /* primary input or unknown */
    if (visited[d]) return;
    visited[d] = 1;
    cone[ncone++] = d;
    if (isseq(ins[d].type)) coneflops[nconeflops++] = d;
    for (int k = 0; k < ins[d].nc; k++)
        if (!isoutpin(ins[d].c[k].pin)) walk(ins[d].c[k].net);
}

int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: %s puzzle.v net_name\n", argv[0]); return 1; }

    sl(rd(argv[1]));
    parseports();
    for (int i = 0; i < nl; i++) { Inst x; if (parseinst(lns[i], &x)) ins[nin++] = x; }

    for (int i = 0; i < nin; i++)
        for (int k = 0; k < ins[i].nc; k++)
            if (isoutpin(ins[i].c[k].pin)) setdrv(ins[i].c[k].net, i);
    for (int i = 0; i < nports; i++) if (!strcmp(dir[i], "input")) setdrv(ports[i], -1);

    for (int a = 2; a < argc; a++) walk(argv[a]);

    printf("fan-in cone of");
    for (int a = 2; a < argc; a++) printf(" %s", argv[a]);
    printf(": %d cells, %d flops\n", ncone, nconeflops);
    return 0;
}
