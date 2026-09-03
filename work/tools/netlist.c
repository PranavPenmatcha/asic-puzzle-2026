#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define NM   128
#define NCON 24
#define NINS 1024
#define NNET 4096
#define NL   16384

typedef struct { char pin[NM]; char net[NM]; } Conn;
typedef struct { char type[NM]; char name[NM]; Conn c[NCON]; int nc; } Inst;

static Inst ins[NINS]; static int nin = 0;
static char ports[64][NM]; static char dir[64][8]; static int nports = 0;

static char *lns[NL]; static int nl = 0;

/* reads a whole file into a malloc'd buffer */
static char *rd(const char *p) {
    FILE *f = fopen(p, "r");
    if (!f) { perror("fopen"); exit(1); }
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    char *b = malloc((size_t)sz + 1);
    fread(b, 1, (size_t)sz, f); b[sz] = 0; fclose(f);
    return b;
}

/* splits text into an array of null-terminated lines, in place */
static void sl(char *t) {
    char *p = t;
    while (*p && nl < NL) {
        lns[nl++] = p;
        while (*p && *p != '\n') p++;
        if (*p == '\n') *p++ = 0;
    }
}

static char *ws(char *s) { while (*s == ' ' || *s == '\t') s++; return s; }

/* copies a raw net token, which may be a plain word OR a Verilog escaped
 * identifier ("\name ", including its trailing space) -- returns pointer
 * just past what was consumed */
static char *readnet(char *s, char *out) {
    s = ws(s);
    char *o = out;
    if (*s == '\\') {
        while (*s && *s != ')' ) { *o++ = *s++; if (s[-1] == ' ') break; }
    } else {
        while (*s && *s != ',' && *s != ')' && *s != ';') *o++ = *s++;
    }
    *o = 0;
    return s;
}

/* parses the module header's port list into `ports`, and each following
 * "input NAME;" / "output NAME;" line into `dir` */
static void parseports(void) {
    for (int i = 0; i < nl; i++) {
        char *l = ws(lns[i]);
        char kind[8];
        if (sscanf(l, "%7s", kind) == 1 && (!strcmp(kind, "input") || !strcmp(kind, "output"))) {
            char net[NM];
            char *p = l + strlen(kind);
            readnet(p, net);
            snprintf(ports[nports], NM, "%s", net);
            snprintf(dir[nports], 8, "%s", kind);
            nports++;
        }
        if (!strncmp(l, "wire", 4) || !strncmp(l, "endmodule", 9)) break;
    }
}

/* parses one "CELLTYPE instname (.PIN(net), .PIN(net), ...);" line */
static int iskeyword(const char *l) {
    static const char *kw[] = {"module", "input", "output", "wire", "endmodule", "assign", 0};
    for (int i = 0; kw[i]; i++) {
        size_t n = strlen(kw[i]);
        if (!strncmp(l, kw[i], n) && (l[n] == ' ' || l[n] == '\t' || l[n] == '(')) return 1;
    }
    return 0;
}

static int parseinst(char *l, Inst *x) {
    l = ws(l);
    if (!isalpha((unsigned char)*l) && *l != '_') return 0;
    if (iskeyword(l)) return 0;
    char *p = l;
    while (*p && *p != ' ') p++;
    if (!*p) return 0;
    snprintf(x->type, NM, "%.*s", (int)(p - l), l);
    l = ws(p);
    p = l;
    while (*p && *p != ' ' && *p != '(') p++;
    snprintf(x->name, NM, "%.*s", (int)(p - l), l);
    l = ws(p);
    if (*l != '(') return 0;
    l++;

    x->nc = 0;
    while (1) {
        l = ws(l);
        if (*l != '.') break;
        l++;
        char *pn = l;
        while (*l && *l != '(') l++;
        snprintf(x->c[x->nc].pin, NM, "%.*s", (int)(l - pn), pn);
        l++; /* skip '(' */
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

static int isseq(const char *type) {
    return strstr(type, "__df") != NULL;
}

/* driver[net] = index into ins[] of the instance whose output pin drives it,
 * or -1 if it's a primary input / never driven by a cell. Simple linear
 * table since ~700 nets fits easily. */
static char drvnet[NNET][NM]; static int drvidx[NNET]; static int ndrv = 0;

/* -2 = no driver recorded at all; -1 = recorded as a primary input;
 * >=0 = index into ins[] of the driving cell. -1 and -2 must stay distinct:
 * "primary input" and "nothing knows about this net" are different facts. */
static int finddrv(const char *net) {
    for (int i = 0; i < ndrv; i++) if (!strcmp(drvnet[i], net)) return drvidx[i];
    return -2;
}
static void setdrv(const char *net, int idx) {
    if (finddrv(net) != -2) return;
    snprintf(drvnet[ndrv], NM, "%s", net);
    drvidx[ndrv] = idx;
    ndrv++;
}

static void buildnetlist(void) {
    for (int i = 0; i < nl; i++) {
        Inst x;
        if (!parseinst(lns[i], &x)) continue;
        ins[nin] = x;
        for (int k = 0; k < x.nc; k++)
            if (isoutpin(x.c[k].pin)) setdrv(x.c[k].net, nin);
        nin++;
    }
    for (int i = 0; i < nports; i++)
        if (!strcmp(dir[i], "input")) setdrv(ports[i], -1);
}

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: %s puzzle.v\n", argv[0]); return 1; }
    sl(rd(argv[1]));
    parseports();
    buildnetlist();

    int nflop = 0, nconst = 0;
    for (int i = 0; i < nin; i++) {
        if (isseq(ins[i].type)) nflop++;
        if (strstr(ins[i].type, "conb_1")) nconst++;
    }

    /* every non-output pin's net must have a driver: either a cell output
     * (setdrv'd above) or a declared top-level input */
    int undriven = 0;
    for (int i = 0; i < nin; i++)
        for (int k = 0; k < ins[i].nc; k++)
            if (!isoutpin(ins[i].c[k].pin) && finddrv(ins[i].c[k].net) == -2) {
                fprintf(stderr, "UNDRIVEN: %s.%s -> %s\n", ins[i].name, ins[i].c[k].pin, ins[i].c[k].net);
                undriven++;
            }
    fprintf(stderr, "%d undriven nets\n", undriven);

    printf("%d instances, %d flops, %d const cells, %d ports (%d nets driven)\n",
           nin, nflop, nconst, nports, ndrv);
    printf("inputs: ");
    for (int i = 0; i < nports; i++) if (!strcmp(dir[i], "input")) printf("%s ", ports[i]);
    printf("\noutputs: ");
    for (int i = 0; i < nports; i++) if (!strcmp(dir[i], "output")) printf("%s ", ports[i]);
    printf("\n");
    return 0;
}
