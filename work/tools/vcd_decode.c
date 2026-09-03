#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NM 128
#define NL 65536
#define NSAMP 8192

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

static char idclk[NM] = "", idrst[NM] = "", idena[NM] = "", idi[NM] = "";

/* scans $var lines, remembering the VCD ids for clk/rst_n/enable/I */
static void findids(void) {
    for (int i = 0; i < nl; i++) {
        char name[NM], id[NM];
        if (sscanf(lns[i], " $var reg %*d %s %s $end", id, name) != 2 &&
            sscanf(lns[i], " $var wire %*d %s %s $end", id, name) != 2) continue;
        if (!strcmp(name, "clk")) snprintf(idclk, NM, "%s", id);
        else if (!strcmp(name, "rst_n")) snprintf(idrst, NM, "%s", id);
        else if (!strcmp(name, "enable")) snprintf(idena, NM, "%s", id);
        else if (!strcmp(name, "I")) snprintf(idi, NM, "%s", id);
    }
}

typedef struct { char rst, ena, I; } Samp;
static Samp samp[NSAMP]; static int nsamp = 0;

/* replays value-change lines, sampling rst_n/enable/I on every clk posedge */
static void sample(void) {
    char clk = '0', rst = '0', ena = '0', I = '0';
    for (int i = 0; i < nl; i++) {
        char *l = lns[i];
        if (l[0] == '#' || l[0] == '$') continue;
        char v = l[0], *id = l + 1;
        if (!strcmp(id, idclk)) {
            if (clk != '1' && v == '1' && nsamp < NSAMP) samp[nsamp++] = (Samp){rst, ena, I};
            clk = v;
        } else if (!strcmp(id, idrst)) rst = v;
        else if (!strcmp(id, idena)) ena = v;
        else if (!strcmp(id, idi))   I = v;
    }
}

/* decodes one 11-bit-per-character run of I bits: 8 bits LSB-first per char */
static void decode(const char *bits, int n) {
    for (int c = 0; c + 8 <= n; c += 11) {
        int v = 0;
        for (int b = 0; b < 8; b++) if (bits[c + b] == '1') v |= 1 << b;
        putchar(v >= 32 && v < 127 ? v : '.');
    }
    putchar('\n');
}

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: %s example_inputs.vcd\n", argv[0]); return 1; }
    sl(rd(argv[1]));
    findids();
    if (!*idclk || !*idrst || !*idena || !*idi) { fprintf(stderr, "missing a signal id\n"); return 1; }
    sample();
    fprintf(stderr, "%d posedge samples\n", nsamp);

    /* find each contiguous run where enable==1, print its I-bit string decoded */
    for (int i = 0; i < nsamp; ) {
        if (samp[i].ena != '1') { i++; continue; }
        int j = i;
        char bits[4096]; int n = 0;
        while (j < nsamp && samp[j].ena == '1' && n < (int)sizeof(bits) - 1) bits[n++] = samp[j++].I;
        bits[n] = 0;
        printf("run of %d bits: ", n);
        decode(bits, n);
        i = j;
    }
    return 0;
}
