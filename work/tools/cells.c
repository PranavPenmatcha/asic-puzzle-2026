#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NM   128
#define NF   512
#define NPIN 16
#define NCT  128

typedef struct { char pin[NPIN][NM]; char func[NPIN][NF]; int np; } Cell;
static Cell cells[NCT]; static char names[NCT][NM]; static int ncell = 0;

static char wanted[NCT][NM]; static int nwanted = 0;

/* reads a whole file into a malloc'd buffer */
static char *rd(const char *p) {
    FILE *f = fopen(p, "r");
    if (!f) { perror("fopen"); exit(1); }
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    char *b = malloc((size_t)sz + 1);
    fread(b, 1, (size_t)sz, f); b[sz] = 0; fclose(f);
    return b;
}

/* copies a quoted string; s must point AT the opening quote */
static char *readq(char *s, char *out, int max) {
    s++;
    int i = 0;
    while (*s && *s != '"' && i < max - 1) out[i++] = *s++;
    out[i] = 0;
    if (*s == '"') s++;
    return s;
}

/* advances s past the block starting at the next '{', tracking nesting */
static char *skipblock(char *s) {
    s = strchr(s, '{');
    if (!s) return s;
    int depth = 0;
    do { if (*s == '{') depth++; else if (*s == '}') depth--; s++; } while (*s && depth > 0);
    return s;
}

static int iswanted(const char *n) {
    for (int i = 0; i < nwanted; i++) if (!strcmp(wanted[i], n)) return 1;
    return 0;
}

/* scans one cell's body for pin() blocks, recording each pin's name and,
 * if present, its boolean "function" string */
static void parsepins(char *body, char *bodyend, Cell *c) {
    c->np = 0;
    char *s = body;
    while ((s = strstr(s, "pin (\"")) != NULL && s < bodyend && c->np < NPIN) {
        char *q = s + 5;
        char pname[NM]; readq(q, pname, NM);
        char *blockstart = strchr(s, '{');
        char *blockend = skipblock(s);
        snprintf(c->pin[c->np], NM, "%s", pname);
        c->func[c->np][0] = 0;
        char *fn = strstr(blockstart, "function");
        if (fn && fn < blockend) {
            char *fq = strchr(fn, '"');
            if (fq) readq(fq, c->func[c->np], NF);
        }
        c->np++;
        s = blockend;
    }
}

/* single pass over the whole liberty file: for every "cell (\"NAME\")" whose
 * NAME is in `wanted`, extract its pin list + boolean functions */
static void parseliberty(char *text) {
    char *s = text;
    while ((s = strstr(s, "cell (\"")) != NULL) {
        char *q = s + 6;
        char name[NM]; char *after = readq(q, name, NM);
        char *blockend = skipblock(after);
        if (iswanted(name) && ncell < NCT) {
            snprintf(names[ncell], NM, "%s", name);
            parsepins(after, blockend, &cells[ncell]);
            ncell++;
        }
        s = blockend;
    }
}

int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: %s lib.lib celltypes.txt\n", argv[0]); return 1; }

    FILE *wf = fopen(argv[2], "r");
    if (!wf) { perror("fopen celltypes"); return 1; }
    char line[NM];
    while (fgets(line, sizeof line, wf) && nwanted < NCT) {
        line[strcspn(line, "\n")] = 0;
        snprintf(wanted[nwanted++], NM, "%s", line);
    }
    fclose(wf);

    char *lib = rd(argv[1]);
    parseliberty(lib);

    printf("parsed %d of %d wanted cell types\n", ncell, nwanted);
    for (int i = 0; i < ncell; i++) {
        printf("%-32s pins:", names[i]);
        for (int p = 0; p < cells[i].np; p++) printf(" %s", cells[i].pin[p]);
        printf("  func:");
        for (int p = 0; p < cells[i].np; p++)
            if (cells[i].func[p][0]) printf(" %s=%s", cells[i].pin[p], cells[i].func[p]);
        printf("\n");
    }
    return 0;
}
