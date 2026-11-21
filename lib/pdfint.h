/* SPDX-License-Identifier: MIT
 * Internal declarations of pdfcore. Not part of the application interface. */
#ifndef PDFINT_H
#define PDFINT_H
#include "pdfcore.h"

typedef signed char pi8;
typedef pi32 fx;                 /* 16.16 fixed point */
#define FX(i)     ((fx)(i) << 16)
#define FXONE     65536L
#define NIL       0xFFFFU
#define XT_UNSET  0xFFFFFFFFUL

/* ---------------- sizes (all inside the work area) ---------------- */
#define WIN_SIZE     32768U      /* inflate window, must be 32 KB          */
#define ARENA_SIZE    8192U      /* parsed objects (mark/release)          */
#define LKBUF_SIZE     512U      /* buffered reads for object lookups      */
#define RAWBUF_SIZE    512U      /* compressed content stream input        */
#define CBUF_SIZE      256U      /* decoded content bytes for the lexer    */
#define GBUF_SIZE      256U      /* Type3 glyph procedures                 */
#define FPOOL_SIZE   10240U      /* per-page font tables                   */
#define MAXPTS         600       /* path points                            */
#define MAXSUB          64       /* subpaths per path                      */
#define MAXSEC          32       /* xref sections                          */
#define MAXFONTS        24       /* fonts per page                         */
#define MAXOPS          24       /* operand stack                          */
#define MAXGS           12       /* q/Q nesting                            */
#define MAXCONT         16       /* content streams per page               */
#define TOKMAX         260       /* token / string buffer                  */
#define RUNMAX         120       /* characters per text run                */
#define MAXXO           16       /* XObjects remembered per page           */
#define MAXJOB          12       /* XObjects drawn after the content      */
#define MAXEXT           4       /* external image blocks                 */

/* ---------------- byte sources ---------------- */
#define SRC_FILE    0            /* unbounded file reads                  */
#define SRC_RAW     1            /* file reads up to limit               */
#define SRC_INFLATE 2            /* Flate decoded from an inner raw src   */
#define SRC_A85     3            /* ASCII85 decoded from inner            */
#define SRC_AHX     4            /* ASCIIHex decoded from inner           */
#define SF_FLATE 1
#define SF_A85   2
#define SF_AHX   4
#define SF_DCT   8              /* DCT data follows (images only)        */

typedef struct Huff { pi16 count[16]; pi16 sym[288]; } Huff;

typedef struct Src Src;
typedef struct PdfDoc PdfDoc_;
typedef struct Inflate Inflate;

struct Src {
    pu8 PFAR *buf;
    pu16 size, pos, len;
    pi32 off;                    /* file offset of buf[len] (file/raw)   */
    pi32 limit;                  /* end of raw data                      */
    pu8 kind, eof;
    Inflate PFAR *inf;
    Src PFAR *inner;
    PdfDoc PFAR *doc;
    pu32 tpos;                   /* bytes consumed so far                */
};

struct Inflate {
    Src raw;
    pu32 bitbuf; pu8 bitcnt;
    pu8 state;                   /* 0 hdr, 1 stored, 2 huffman, 3 done, 4 err */
    pu8 last;
    pu16 stored_left;
    pu16 copy_len, copy_dist;
    pu16 wpos;
    pu8 PFAR *win;
    Huff lit, dist;
    pu8 lens[320];
};

/* ---------------- tokens ---------------- */
#define T_EOF   0
#define T_NUM   1
#define T_NAME  2
#define T_STR   3
#define T_KW    4
#define T_ABEG  5
#define T_AEND  6
#define T_DBEG  7
#define T_DEND  8
#define T_PBEG  9
#define T_PEND 10
#define T_ERR  11

typedef struct Tok {
    pu8 type, isint;
    pu16 len;
    fx num;
    pi32 ival;
    pu8 s[TOKMAX];
} Tok;

/* ---------------- parsed objects in the arena ---------------- */
#define N_NULL 0
#define N_BOOL 1
#define N_NUM  2
#define N_NAME 3
#define N_STR  4
#define N_ARR  5
#define N_DICT 6
#define N_REF  7

typedef struct Node {
    pu8 type, isint;
    pu16 next;                   /* sibling (arena offset) or NIL       */
    pu16 child;                  /* first element / string offset       */
    pu16 len;                    /* string length / element count        */
    pi32 val;                    /* int, fixed, ref object number        */
} Node;

/* ---------------- fonts ---------------- */
typedef struct CidEnt { pu16 code; pu16 w; pu8 ch; pu8 pad; } CidEnt;

typedef struct Font {
    pu8 res[24];                 /* resource name                        */
    pu16 gfont; pu8 style;
    pu8 twobyte;
    pu16 dw;                     /* default width (1/1000 em)            */
    pu8 PFAR *map;               /* simple: 256 GEOS chars               */
    pu8 PFAR *w4;                /* simple: 256 widths / 4 (1/1000 em)   */
    CidEnt PFAR *ent;            /* Type0: sorted by code                */
    pu16 nent;
    pu16 PFAR *t3;               /* Type3: glyph procedure object per code */
    pu16 t3upm;                  /* Type3: glyph units per em (1/FontMatrix) */
} Font;

/* ---------------- graphics ---------------- */
typedef struct GState {
    fx ctm[6];
    pu8 fillc, strokec;
    fx lw;
    pi16 font;                   /* index into fonts or -1               */
    fx fs, tc, tw, th, tl, ts;
    pu8 tr;
} GState;

typedef struct Sec { pu16 start, count; pi32 off; } Sec;

typedef struct Op { pu8 type; pu8 isint; fx num; pu16 soff, slen; } Op;

struct PdfDoc {
    pu16 file;
    PdfReadFn rd;
    pu32 PFAR *xtab; pu16 xcap;   /* xref table from xref streams (app)  */
    pu8 incontent;               /* a content stream holds the inflater  */
    pu8 nxo;                     /* XObjects of the page                 */
    pu16 xohash[MAXXO]; pu8 xoimg[MAXXO]; pu16 xoobj[MAXXO];   /* xoimg: 1 image 2 form */
    struct { pu16 obj, ps, pe; fx ctm[6]; pu8 col, form; } job[MAXJOB]; pu8 njob;
    pu16 entcap;
    fx lastx, lasty;
    pi32 fsize;
    pu16 nsec;
    Sec sec[MAXSEC];
    pi32 root, pages;
    pi16 npages;
    /* memory */
    pu8 PFAR *arena; pu16 atop;
    pu8 PFAR *win;
    pu8 PFAR *fpool; pu16 ftop;
    pu8 PFAR *lkbuf;
    pu8 PFAR *rawbuf;
    pu8 PFAR *cbuf;
    Inflate inf;
    Src rawsrc;                  /* innermost raw source of a chain      */
    pu8 decbuf[128];
    pu8 ilut[768];               /* indexed image palette                */
    pu8 PFAR *gbuf;              /* GBUF_SIZE: Type3 glyph source        */
    pu8 PFAR *bm; pi16 bmx, bmy; pu16 bmrow;   /* fill into a DL bitmap */
    Src lk;                      /* lookup source                        */
    Tok tok;
    pi32 streamoff;              /* data offset of last loaded stream    */
    pu16 scratch[256];
    /* page */
    pi32 pageobj, resobj;        /* object whose dict holds /Resources  */
    fx mbox[4];
    pi16 rot;                    /* /Rotate 0,90,180,270                 */
    Font fonts[MAXFONTS]; pi16 nfonts;
    /* content */
    pi32 cont[MAXCONT]; pu8 ncont, curcont;
    Src cs;                      /* current content source               */
    Op ops[MAXOPS]; pi16 nops;
    pu8 opstr[512]; pu16 opstrtop;
    GState gs[MAXGS]; pi16 gsp;
    fx tm[6], tlm[6];
    /* path in device quarter pixels */
    pi16 px[MAXPTS], py[MAXPTS]; pi16 npts;
    pi16 subend[MAXSUB]; pu8 subclosed[MAXSUB]; pi16 nsub;
    fx curx, cury, startx, starty;      /* user space current point   */
    pu8 onlyrects;
    /* output */
    pu8 PFAR *dl; pu16 dlsize, dlpos; pu8 trunc;
    pu8 tc, nct, jleft;          /* true colour; colours 16.. in ctab;  */
    pu32 share;                  /* images still to draw, output share  */
    pu8 PFAR *ext[MAXEXT];       /* external image blocks (pdf_set_ext)  */
    pu8 next, extk;              /* number of blocks, block in use       */
    pu16 extsize, extpos;
    pu8 PFAR *ctab;              /* r g b of colour 16+k (after the DL)   */
    pi16 devw, devh;
    fx base[6];
    /* text run */
    pu8 run[RUNMAX]; pu8 nrun;
    pi16 runx, runy; pu16 runsize; pu16 runfont; pu8 runstyle, runcol, runrot;
    /* span merge */
    pi16 ox0[32], ox1[32], oy[32]; pi16 nopen;
    /* scratch for the rasteriser (kept off the small GEOS stack) */
    pi16 nx0[32], nx1[32], ny[32];
    pi16 xs[48]; pi8 dirs[48];
    pi16 cx0[32], cx1[32];
};


/* ---- pdfcore.c services used by pdfrend.c ---- */
void p_memset(void PFAR *d, pu8 v, pu16 n);
void p_memcpy(void PFAR *d, const void PFAR *s, pu16 n);
pi16 p_streq(const pu8 PFAR *a, pu16 alen, const char PFAR *b);
pi16 p_contains(const pu8 PFAR *a, pu16 alen, const char PFAR *b);
fx fx_mul(fx a, fx b);
fx fx_div(fx a, fx b);
pu32 p_isqrt(pu32 v);
pi16 src_get(Src PFAR *s);
pi16 src_peek(Src PFAR *s);
void src_file(PdfDoc PFAR *d, Src PFAR *s, pu8 PFAR *buf, pu16 size, pi32 off, pi32 limit);
pi16 src_stream(PdfDoc PFAR *d, Src PFAR *s, pu8 PFAR *rawbuf, pu8 PFAR *outbuf,
                pu16 outsize, pi32 off, pi32 len, pu8 flate);
pi16 lex(Src PFAR *s, Tok PFAR *t);
pu16 load_obj(PdfDoc PFAR *d, pi32 num);
pu16 resolve(PdfDoc PFAR *d, pu16 o);
pu16 dict_get(PdfDoc PFAR *d, pu16 dict, const char PFAR *key);
pu16 dict_getr(PdfDoc PFAR *d, pu16 dict, const char PFAR *key);
pu16 arr_at(PdfDoc PFAR *d, pu16 arr, pu16 idx);
pi32 node_int(PdfDoc PFAR *d, pu16 o, pi32 dflt);
fx node_fx(PdfDoc PFAR *d, pu16 o, fx dflt);
pu16 parse_inline_dict(PdfDoc PFAR *d, Src PFAR *s);
pi16 node_is(PdfDoc PFAR *d, pu16 o, pu8 type, const char PFAR *name);
pi16 stream_info(PdfDoc PFAR *d, pu16 sd, pi32 PFAR *len, pu8 PFAR *flate);
pi16 find_page(PdfDoc PFAR *d, pi16 idx);
#define NODE(d, o) ((Node PFAR *)((d)->arena + (o)))

#endif
