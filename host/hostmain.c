/* Host test driver: renders pages to display-list files (.dl). */
#include <stdio.h>
#include <stdlib.h>
#include "../core/pdfcore.h"
static FILE *fp;
static pi16 rd(pu16 file, pi32 off, pu8 *buf, pu16 len)
{ (void)file; if (fseek(fp, off, SEEK_SET)) return -1; return (pi16)fread(buf, 1, len, fp); }
int main(int argc, char **argv)
{
    static pu8 work[65520]; static pu8 dl[200000]; static pu32 xr[10000];
    static pu8 ex[3][65536]; pu8 *exp[3]; int nex = getenv("NEXT") ? atoi(getenv("NEXT")) : 0, k;
    pi16 e, n, i, w, h, first = 0, last = -1; pu8 tr; char msg[64];
    unsigned scale = argc > 3 ? atoi(argv[3]) : 256;
    if (argc < 3) { fprintf(stderr, "usage: pdfhost file.pdf outprefix [scale256] [page]\n"); return 2; }
    fp = fopen(argv[1], "rb"); if (!fp) return 2;
    fseek(fp, 0, SEEK_END);
    e = pdf_open(0, (pi32)ftell(fp), rd, work, sizeof(work), (pu8 *)xr, sizeof(xr));
    if (e) { pdf_errtext(e, msg, sizeof msg); printf("open: %d %s\n", e, msg); return 1; }
    for (k = 0; k < 3; k++) exp[k] = ex[k];
    pdf_set_ext(work, exp, (pu16)nex, (pu16)(getenv("DLSZ") ? atoi(getenv("DLSZ")) : 65535));
    n = pdf_page_count(work);
    printf("pages %d\n", n);
    if (argc > 4) first = last = atoi(argv[4]); else last = n > 3 ? 2 : n - 1;
    for (i = first; i <= last; i++) {
        char nm[512]; FILE *o;
        e = pdf_page_size(work, i, &w, &h);
        e = pdf_render(work, i, (pu16)(scale | (getenv("TC") ? 0x8000U : 0)), dl, (pu16)(getenv("DLSZ") ? atoi(getenv("DLSZ")) : 65535), &tr);
        printf("page %d size %dx%d -> %u bytes%s\n", i, w, h, (unsigned)(pu16)e, tr ? " TRUNC" : "");
        if (e < 0 && e > -20) e = e; else e = (pi16)e;
        if (e < 0 && e > -20) { pdf_errtext(e, msg, sizeof msg); printf("  %s\n", msg); continue; }
        sprintf(nm, "%s-%d.dl", argv[2], i);
        o = fopen(nm, "wb"); fwrite(&w, 2, 1, o); fwrite(&h, 2, 1, o); fwrite(dl, 1, (pu16)e, o); fclose(o);
        for (k = 0; k < nex; k++) {
            sprintf(nm, "%s-%d.e%d", argv[2], i, k);
            o = fopen(nm, "wb"); fwrite(ex[k], 1, 65536, o); fclose(o);
        }
        if (getenv("TC")) {
            unsigned dsz = getenv("DLSZ") ? atoi(getenv("DLSZ")) : 65535;
            sprintf(nm, "%s-%d.ct", argv[2], i);
            o = fopen(nm, "wb"); fwrite(dl + dsz, 1, 960, o); fclose(o);
        }
    }
    return 0;
}
