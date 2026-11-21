/* SPDX-License-Identifier: MIT
 * pdfcore - small PDF reader/renderer core for PC/GEOS 1.x (8086, 16-bit).
 *
 * The core knows nothing about GEOS. It reads the file through
 * pdf_io_read() (supplied by the host test or the GEOS adapter) and turns
 * one page into a compact display list of filled rectangles and text runs
 * that the application replays with GrFillRect / GrDrawText.
 *
 * Supported: PDF 1.0-1.7 with classic xref tables (incl. /Prev updates),
 * cross-reference streams and object streams (PDF 1.5+), xref repair,
 * FlateDecode streams, page tree with inherited MediaBox/Resources,
 * paths (m l c v y h re, f f* F S s B B* b b* n), q/Q/cm/w, gray/RGB/CMYK
 * colours, text (BT/ET, Tf Td TD Tm T* Tc Tw Tz TL Ts, Tj TJ ' ")
 * with simple fonts (Standard/WinAnsi/MacRoman/Differences/ToUnicode)
 * and Type0 Identity-H fonts with ToUnicode.
 * Not (yet): encryption, images
 * (drawn as grey placeholders), clipping, form XObjects, rotation.
 */
#ifndef PDFCORE_H
#define PDFCORE_H

#ifdef __WATCOMC__
#define PFAR   __far
#define PCDECL __cdecl
#define PAPI   __far __cdecl
#else
#define PFAR
#define PCDECL
#define PAPI
#endif

typedef unsigned char  pu8;
typedef unsigned short pu16;
typedef short          pi16;
#if defined(__WATCOMC__) || !defined(__LP64__)
typedef unsigned long  pu32;
typedef long           pi32;
#else
typedef unsigned int   pu32;           /* host test builds on 64-bit */
typedef int            pi32;
#endif

/* ---- error codes (negative) ---- */
#define PDF_OK            0
#define PDF_E_IO         -1   /* read error */
#define PDF_E_NOTPDF     -2   /* no %PDF header / no startxref */
#define PDF_E_XREFSTM    -3   /* PDF 1.5 xref stream but no xref block given */
#define PDF_E_XREF       -4   /* damaged cross-reference table */
#define PDF_E_ENCRYPT    -5   /* encrypted document */
#define PDF_E_SYNTAX     -6   /* object syntax error */
#define PDF_E_NOMEM      -7   /* work area exhausted */
#define PDF_E_PAGE       -8   /* page not found */
#define PDF_E_FILTER     -9   /* unsupported stream filter */
#define PDF_E_INFLATE   -10   /* corrupt Flate data */

/* ---- display list ----
 * Byte stream, little endian. Every record starts with an opcode byte.
 *   DL_END   (0)
 *   DL_RECT  (1) color:u8  x0:i16 y0:i16 x1:i16 y1:i16     (inclusive)
 *   DL_TEXT  (2) color:u8  font:u16 size:u16 style:u8 x:i16 y:i16 len:u8 chars[len]
 *                size = point size * 4 (quarter points); x,y = top-left
 *                of the text box as GrDrawText expects; chars in GEOS
 *                character set.
 *   DL_IMAGE (3) color:u8  x0 y0 x1 y1  (placeholder frame for an image)
 * Colours 0..15 are the GEOS palette; with PDF_TRUECOLOR, colour 16+k is
 * r,g,b,nearest palette index at dl[dlsize + 4k] (the caller keeps DL_CTSZ bytes after the DL).
 *   DL_RGBIMG (5) scale:u8 x:i16 y:i16 len:u16 rows:u16, then per row
 *        runs:u16 and per run dx:u16 + a GEOS 24-bit Bitmap (w,1,0,3) and
 *        3*w bytes r,g,b. Every pixel is drawn scale x scale.
 *   DL_BITMAP (4) color:u8 x:i16 y:i16 len:u16, then len bytes of a GEOS
 *                Bitmap: w:u16 h:u16 compact:u8 (1 = PackBits per row)
 *                type:u8 (0 = mono), rows of (w+7)/8 bytes, bit 7 = leftmost
 *                pixel, 1 = paint in color
 * Coordinates are device pixels relative to the page's top-left corner.
 * Colours are indices into the standard 16 colour GEOS palette.
 */
#define DL_END   0
#define DL_RECT  1
#define DL_TEXT  2
#define DL_IMAGE 3
#define DL_BITMAP 4
#define DL_RGBIMG 5
#define DL_EXT    6           /* blk:u8 off:u16 len:u16 -> records in external block */
#define DL_CTSZ  960           /* colour table after the DL: 240 x r,g,b,index */
#define PDF_TRUECOLOR 0x8000U  /* in scale256: RGB colours and 24-bit images */

/* GEOS font ids used in DL_TEXT (FID_DTC_URW_*), text style bits */
#define PF_ROMAN 0x1000
#define PF_SANS  0x1200
#define PF_MONO  0x1a00
#define PS_BOLD   0x20
#define PS_ITALIC 0x10

/* The application owns all memory. The work area must be at least
 * PDF_WORK_MIN bytes and stays in use while the document is open. */
#define PDF_WORK_MIN 63200U

typedef struct PdfDoc PdfDoc;

/* Supplied by the caller: read len bytes at absolute file offset off into
 * buf. Returns bytes read (short read at EOF), or -1. Called back from
 * inside pdf_open/pdf_page_size/pdf_render only. */
typedef pi16 (PAPI *PdfReadFn)(pu16 file, pi32 off, pu8 PFAR *buf, pu16 len);

/* Open a document of fsize bytes. work = work area (>= PDF_WORK_MIN bytes)
 * that stays owned by the caller; the PdfDoc lives at its start.
 * xref/xrefsize: optional second block (4 bytes per object) for PDF 1.5+
 * cross-reference streams and for rebuilding a damaged xref table; pass
 * 0/0 to support classic xref tables only. */
pi16 PAPI pdf_open(pu16 file, pi32 fsize, PdfReadFn rd, pu8 PFAR *work, pu16 worksize,
                   pu8 PFAR *xref, pu16 xrefsize);

/* Number of pages (>=1) or error. */
pi16 PAPI pdf_page_count(pu8 PFAR *work);

/* Page size in points (rounded). */
pi16 PAPI pdf_page_size(pu8 PFAR *work, pi16 page, pi16 PFAR *w, pi16 PFAR *h);

/* Render page (0-based) into dl (dlsize bytes). scale256 = device pixels
 * per point * 256 (256 = 100%). Returns number of bytes written (>0) or an
 * error. If the list was too small, *truncated is set to 1 and the list
 * ends early but is still terminated with DL_END. */
pi16 PAPI pdf_render(pu8 PFAR *work, pi16 page, pu16 scale256,
                     pu8 PFAR *dl, pu16 dlsize, pu8 PFAR *truncated);

/* Short German message for an error code (static string). */
/* Copy a short German message for err into buf (NUL terminated). */
void PAPI pdf_errtext(pi16 err, char PFAR *buf, pu16 size);

/* External image blocks: n far pointers to size bytes each (laid out like
 * the dl buffer). Images then go there, with DL_EXT records in the dl. */
void PAPI pdf_set_ext(pu8 PFAR *work, pu8 PFAR * PFAR *ext, pu16 n, pu16 size);

#endif
