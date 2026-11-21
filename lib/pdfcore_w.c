#define TABSEG __based(__segname("PDFTABS"))
#include "pdfint.h"
static const char TABSEG KS[] =
  "true\000false\000ID\000obj\000stream\000Length\000Filter\000FlateDecode\000Fl\000ASCII8"
  "5Decode\000A85\000ASCIIHexDecode\000AHx\000DCTDecode\000DCT\000N\000First\000Encrypt"
  "\000Root\000W\000Size\000Prev\000DecodeParms\000Predictor\000Columns\000Index\000Type\000C"
  "atalog\000xref\000trailer\000XRefStm\000startxref\000Pages\000Count\000Resources\000"
  "MediaBox\000Rotate\000Kids\000CropBox\000Lesefehler\000Keine PDF-Datei\000PDF "
  "1.5+ (XRef-Stream) noch nicht unterstuetzt\000Querverweistabell"
  "e beschaedigt\000Verschluesselte PDF\000Syntaxfehler\000Zu wenig Spei"
  "cher\000Seite nicht gefunden\000Unbekannter Stream-Filter\000Komprimi"
  "erte Daten defekt\000Fehler\000beginbfchar\000beginbfrange\000Flags\000bold"
  "\000black\000heavy\000semibold\000demi\000italic\000oblique\000courier\000mono\000conso"
  "l\000typewriter\000cmtt\000sans\000arial\000helvetica\000verdana\000calibri\000tahom"
  "a\000frutiger\000univers\000futura\000gothic\000segoe\000trebuchet\000times\000roman"
  "\000serif\000georgia\000garamond\000cambria\000book\000palatino\000minion\000century"
  "\000cmr\000baskerville\000caslon\000bodoni\000nimbusrom\000Differences\000CharPro"
  "cs\000FontMatrix\000\000Subtype\000BaseFont\000FontDescriptor\000Type0\000Descend"
  "antFonts\000DW\000ToUnicode\000Encoding\000FirstChar\000Widths\000symbol\000dingb"
  "at\000wingding\000TrueType\000BaseEncoding\000WinAnsiEncoding\000MacRomanEn"
  "coding\000StandardEncoding\000MissingWidth\000Type3\000XObject\000Image\000For"
  "m\000Font\000Indexed\000I\000ICCBased\000CalRGB\000Lab\000CalGray\000Separation\000Devi"
  "ceGray\000G\000DeviceRGB\000RGB\000DeviceCMYK\000CMYK\000Width\000H\000Height\000BPC\000Bi"
  "tsPerComponent\000IM\000ImageMask\000CS\000ColorSpace\000D\000Decode\000DP\000F\000Matr"
  "ix\000SMask\000Mask\000q\000Q\000cm\000c\000w\000m\000l\000v\000y\000h\000re\000rg\000RG\000g\000k\000K\000s\000sc\000scn\000S"
  "\000SC\000SCN\000B\000B*\000BT\000BI\000b\000b*\000n\000ET\000Do\000T*\000Contents\000";

/* ---- pdfcore.c ---- */
/* SPDX-License-Identifier: MIT
 * pdfcore: file access, lexer, object parser, xref, inflate, page tree. */


/* ====================================================================== */
/* small helpers (no C library)                                           */
/* ====================================================================== */
void p_memset(void PFAR *d, pu8 v, pu16 n)
{ pu8 PFAR *p = (pu8 PFAR *)d; while (n--) *p++ = v; }

void p_memcpy(void PFAR *d, const void PFAR *s, pu16 n)
{ pu8 PFAR *p = (pu8 PFAR *)d; const pu8 PFAR *q = (const pu8 PFAR *)s; while (n--) *p++ = *q++; }

pi16 p_streq(const pu8 PFAR *a, pu16 alen, const char PFAR *b)
{
    pu16 i = 0;
    while (i < alen) { if ((pu8)b[i] != a[i] || !b[i]) return 0; i++; }
    return b[i] == 0;
}

pi16 p_contains(const pu8 PFAR *a, pu16 alen, const char PFAR *b)
{
    pu16 i, j;
    for (i = 0; i < alen; i++) {
        for (j = 0; b[j] && i + j < alen; j++) {
            pu8 c = a[i + j], d = (pu8)b[j];
            if (c >= 'A' && c <= 'Z') c += 32;
            if (d >= 'A' && d <= 'Z') d += 32;
            if (c != d) break;
        }
        if (!b[j]) return 1;
    }
    return 0;
}

/* 16.16 multiply without 64-bit arithmetic */
fx fx_mul(fx a, fx b)
{
    pi16 neg = 0; pu32 ua, ub, ah, al, bh, bl, r;
    if (a < 0) { a = -a; neg ^= 1; }
    if (b < 0) { b = -b; neg ^= 1; }
    ua = (pu32)a; ub = (pu32)b;
    ah = ua >> 16; al = ua & 0xFFFFUL; bh = ub >> 16; bl = ub & 0xFFFFUL;
    r = ((ah * bh) << 16) + ah * bl + al * bh + ((al * bl) >> 16);
    return neg ? -(fx)r : (fx)r;
}

/* 16.16 divide a/b (b != 0) by long division */
fx fx_div(fx a, fx b)
{
    pi16 neg = 0, i; pu32 ua, ub, q = 0, r = 0;
    if (b == 0) return a < 0 ? -0x7FFFFFFFL : 0x7FFFFFFFL;
    if (a < 0) { a = -a; neg ^= 1; }
    if (b < 0) { b = -b; neg ^= 1; }
    ua = (pu32)a; ub = (pu32)b;
    /* integer part */
    q = ua / ub; r = ua - q * ub;
    if (q > 0x7FFFUL) q = 0x7FFFUL;
    q <<= 16;
    for (i = 15; i >= 0; i--) {
        r <<= 1;
        if (r >= ub) { r -= ub; q |= (1UL << i); }
    }
    return neg ? -(fx)q : (fx)q;
}

pu32 p_isqrt(pu32 v)
{
    pu32 r = 0, bit = 1UL << 30;
    while (bit > v) bit >>= 2;
    while (bit) {
        if (v >= r + bit) { v -= r + bit; r = (r >> 1) + bit; }
        else r >>= 1;
        bit >>= 2;
    }
    return r;
}

/* ====================================================================== */
/* byte sources                                                            */
/* ====================================================================== */
static pi16 inflate_read(Inflate PFAR *z, pu8 PFAR *out, pu16 max);
static pi16 hexval(pi16 c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}


void src_file(PdfDoc PFAR *d, Src PFAR *s, pu8 PFAR *buf, pu16 size, pi32 off, pi32 limit)
{
    s->buf = buf; s->size = size; s->pos = s->len = 0;
    s->off = off; s->limit = limit; s->eof = 0;
    s->kind = limit >= 0 ? SRC_RAW : SRC_FILE;
    s->inf = 0; s->inner = 0; s->doc = d; s->tpos = 0;
}

static pi16 src_fill(PdfDoc PFAR *d, Src PFAR *s)
{
    pi16 n; pu16 want;
    if (s->eof) return 0;
    if (s->kind == SRC_A85 || s->kind == SRC_AHX) {
        pu16 n2 = 0;
        Src PFAR *in = s->inner;
        while (n2 + 4 <= s->size) {
            pi16 c = src_get(in);
            if (c < 0) break;
            if (s->kind == SRC_AHX) {
                pi16 h1, h2;
                if (c == '>') { in->eof = 1; break; }
                h1 = hexval(c); if (h1 < 0) continue;
                do { c = src_get(in); } while (c >= 0 && c != '>' && hexval(c) < 0);
                h2 = c >= 0 ? hexval(c) : -1;
                s->buf[n2++] = (pu8)(h1 * 16 + (h2 < 0 ? 0 : h2));
                if (h2 < 0) { in->eof = 1; break; }
            } else {
                pu32 v = 0; pi16 k = 0, i2;
                if (c == 'z') { s->buf[n2++] = 0; s->buf[n2++] = 0; s->buf[n2++] = 0; s->buf[n2++] = 0; continue; }
                if (c == '~') { in->eof = 1; break; }
                if (c < '!' || c > 'u') continue;
                v = (pu32)(c - '!'); k = 1;
                while (k < 5) {
                    c = src_get(in);
                    if (c < 0 || c == '~') break;
                    if (c < '!' || c > 'u') continue;
                    v = v * 85 + (pu32)(c - '!'); k++;
                }
                for (i2 = k; i2 < 5; i2++) v = v * 85 + 84;
                for (i2 = 0; i2 < k - 1; i2++) s->buf[n2++] = (pu8)(v >> (24 - 8 * i2));
                if (k < 5) { in->eof = 1; break; }
            }
        }
        if (n2 == 0) { s->eof = 1; return 0; }
        s->pos = 0; s->len = n2;
        return (pi16)n2;
    }
    if (s->kind == SRC_INFLATE) {
        n = inflate_read(s->inf, s->buf, s->size);
        if (n <= 0) { s->eof = 1; return 0; }
        s->pos = 0; s->len = (pu16)n;
        return n;
    }
    want = s->size;
    if (s->kind == SRC_RAW) {
        if (s->off >= s->limit) { s->eof = 1; return 0; }
        if ((pi32)want > s->limit - s->off) want = (pu16)(s->limit - s->off);
    }
    n = d->rd(d->file, s->off, s->buf, want);
    if (n <= 0) { s->eof = 1; return 0; }
    s->off += n; s->pos = 0; s->len = (pu16)n;
    return n;
}

pi16 src_get(Src PFAR *s)
{
    if (s->pos >= s->len && src_fill(s->doc, s) <= 0) return -1;
    s->tpos++;
    return s->buf[s->pos++];
}

/* Make at least n bytes available in the buffer if the source has them
 * (file, raw and inflate sources), so the parser can look ahead. */
void src_topup(Src PFAR *s, pu16 n)
{
    pu16 rem, i; pi16 got = 0, want;
    if (s->len - s->pos >= n || s->eof) return;
    if (s->kind != SRC_FILE && s->kind != SRC_RAW && s->kind != SRC_INFLATE) return;
    rem = s->len - s->pos;
    for (i = 0; i < rem; i++) s->buf[i] = s->buf[s->pos + i];
    s->pos = 0; s->len = rem;
    want = (pi16)(s->size - rem);
    if (s->kind == SRC_INFLATE) got = inflate_read(s->inf, s->buf + rem, (pu16)want);
    else {
        if (s->kind == SRC_RAW && (pi32)want > s->limit - s->off) want = (pi16)(s->limit - s->off);
        if (want > 0) got = s->doc->rd(s->doc->file, s->off, s->buf + rem, (pu16)want);
        if (got > 0) s->off += got;
    }
    if (got > 0) s->len = rem + (pu16)got;
}

pi16 src_peek(Src PFAR *s)
{
    if (s->pos >= s->len && src_fill(s->doc, s) <= 0) return -1;
    return s->buf[s->pos];
}

/* absolute file position of the next byte (file/raw sources only) */
pi32 src_tell(Src PFAR *s) { return s->off - (pi32)(s->len - s->pos); }

void src_seek(Src PFAR *s, pi32 off)
{
    /* reuse buffer when possible */
    pi32 bstart = s->off - (pi32)s->len;
    if (s->kind != SRC_INFLATE && off >= bstart && off < s->off) {
        s->pos = (pu16)(off - bstart); s->eof = 0; return;
    }
    s->off = off; s->pos = s->len = 0; s->eof = 0;
}

/* ====================================================================== */
/* inflate (RFC 1951), pull style, 32 KB window                            */
/* ====================================================================== */
static const pu16 TABSEG LBASE[29] = {3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258};
static const pu8 TABSEG LEXT[29]  = {0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0};
static const pu16 TABSEG DBASE[30] = {1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577};
static const pu8 TABSEG DEXT[30]  = {0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13};
static const pu8 TABSEG CLORD[19] = {16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15};

static pi16 z_bits(Inflate PFAR *z, pu8 need)
{
    pu16 v;
    while (z->bitcnt < need) {
        pi16 c = src_get(&z->raw);
        if (c < 0) { z->state = 4; return -1; }
        z->bitbuf |= (pu32)(pu8)c << z->bitcnt;   /* need <= 13: up to 20 bits */
        z->bitcnt += 8;
    }
    v = (pu16)(z->bitbuf & ((1UL << need) - 1));
    z->bitbuf >>= need;
    z->bitcnt -= need;
    return (pi16)v;
}

static pi16 z_build(Huff PFAR *h, const pu8 PFAR *len, pi16 n)
{
    pi16 offs[16], i, left;
    for (i = 0; i < 16; i++) h->count[i] = 0;
    for (i = 0; i < n; i++) h->count[len[i]]++;
    if (h->count[0] == n) return 0;
    left = 1;
    for (i = 1; i < 16; i++) { left <<= 1; left -= h->count[i]; if (left < 0) return -1; }
    offs[1] = 0;
    for (i = 1; i < 15; i++) offs[i + 1] = offs[i] + h->count[i];
    for (i = 0; i < n; i++) if (len[i]) h->sym[offs[len[i]]++] = i;
    return left;
}

static pi16 z_decode(Inflate PFAR *z, Huff PFAR *h)
{
    pi16 code = 0, first = 0, index = 0, len, count, b;
    for (len = 1; len < 16; len++) {
        b = z_bits(z, 1); if (b < 0) return -1;
        code |= b;
        count = h->count[len];
        if (code - count < first) return h->sym[index + (code - first)];
        index += count; first += count; first <<= 1; code <<= 1;
    }
    z->state = 4;
    return -1;
}

static pi16 z_header(Inflate PFAR *z)
{
    pi16 type, i, nlen, ndist, ncode, sym, len;
    pu8 PFAR *lens = z->lens;
    if (z->last) { z->state = 3; return 0; }
    z->last = (pu8)z_bits(z, 1);
    type = z_bits(z, 2);
    if (type < 0) return -1;
    if (type == 0) {
        pi16 a, b, e;
        z->bitbuf = 0; z->bitcnt = 0;          /* byte align */
        a = src_get(&z->raw); b = src_get(&z->raw);
        src_get(&z->raw); e = src_get(&z->raw);
        if (e < 0) return -1;
        z->stored_left = (pu16)(a | (b << 8));
        z->state = 1;
        return 0;
    }
    if (type == 1) {
        for (i = 0; i < 144; i++) lens[i] = 8;
        for (; i < 256; i++) lens[i] = 9;
        for (; i < 280; i++) lens[i] = 7;
        for (; i < 288; i++) lens[i] = 8;
        z_build(&z->lit, lens, 288);
        for (i = 0; i < 30; i++) lens[i] = 5;
        z_build(&z->dist, lens, 30);
        z->state = 2;
        return 0;
    }
    if (type != 2) return -1;
    nlen = z_bits(z, 5) + 257; ndist = z_bits(z, 5) + 1; ncode = z_bits(z, 4) + 4;
    if (nlen > 286 || ndist > 30) return -1;
    for (i = 0; i < 19; i++) lens[i] = 0;
    for (i = 0; i < ncode; i++) lens[CLORD[i]] = (pu8)z_bits(z, 3);
    if (z_build(&z->lit, lens, 19) != 0) return -1;
    i = 0;
    while (i < nlen + ndist) {
        sym = z_decode(z, &z->lit);
        if (sym < 0) return -1;
        if (sym < 16) lens[i++] = (pu8)sym;
        else {
            len = 0;
            if (sym == 16) { if (i == 0) return -1; len = lens[i - 1]; sym = 3 + z_bits(z, 2); }
            else if (sym == 17) sym = 3 + z_bits(z, 3);
            else sym = 11 + z_bits(z, 7);
            if (i + sym > nlen + ndist) return -1;
            while (sym--) lens[i++] = (pu8)len;
        }
    }
    if (z_build(&z->lit, lens, nlen) < 0) return -1;
    if (z_build(&z->dist, lens + nlen, ndist) < 0) return -1;
    z->state = 2;
    return 0;
}

static pi16 inflate_read(Inflate PFAR *z, pu8 PFAR *out, pu16 max)
{
    pu16 n = 0; pi16 sym, c;
    while (n < max) {
        if (z->copy_len) {
            pu8 b = z->win[(pu16)(z->wpos - z->copy_dist) & (WIN_SIZE - 1)];
            z->win[z->wpos] = b; z->wpos = (z->wpos + 1) & (WIN_SIZE - 1);
            out[n++] = b; z->copy_len--;
            continue;
        }
        if (z->state == 0) { if (z_header(z) < 0) z->state = 4; continue; }
        if (z->state >= 3) break;
        if (z->state == 1) {
            if (!z->stored_left) { z->state = 0; continue; }
            c = src_get(&z->raw); if (c < 0) { z->state = 4; break; }
            z->win[z->wpos] = (pu8)c; z->wpos = (z->wpos + 1) & (WIN_SIZE - 1);
            out[n++] = (pu8)c; z->stored_left--;
            continue;
        }
        sym = z_decode(z, &z->lit);
        if (sym < 0) { z->state = 4; break; }
        if (sym < 256) {
            z->win[z->wpos] = (pu8)sym; z->wpos = (z->wpos + 1) & (WIN_SIZE - 1);
            out[n++] = (pu8)sym;
        } else if (sym == 256) {
            z->state = 0;
        } else {
            sym -= 257;
            if (sym >= 29) { z->state = 4; break; }
            z->copy_len = LBASE[sym] + z_bits(z, LEXT[sym]);
            sym = z_decode(z, &z->dist);
            if (sym < 0 || sym >= 30) { z->state = 4; break; }
            z->copy_dist = DBASE[sym] + z_bits(z, DEXT[sym]);
        }
    }
    return (pi16)n;
}

/* Prepare src as reader for a stream at offset/length; flags SF_*. */
pi16 src_stream(PdfDoc PFAR *d, Src PFAR *s, pu8 PFAR *rawbuf, pu8 PFAR *outbuf,
                pu16 outsize, pi32 off, pi32 len, pu8 flags)
{
    Src base;
    src_file(d, &base, rawbuf, RAWBUF_SIZE, off, off + len);
    if (flags & (SF_A85 | SF_AHX)) {
        d->rawsrc = base;
        p_memset(&base, 0, sizeof(base));
        base.buf = d->decbuf; base.size = sizeof(d->decbuf);
        base.kind = (flags & SF_A85) ? SRC_A85 : SRC_AHX;
        base.inner = &d->rawsrc; base.doc = d; base.tpos = 0;
    }
    if (!(flags & SF_FLATE)) { *s = base; return 0; }
    {
        Inflate PFAR *z = &d->inf;
        pi16 b0, b1;
        z->raw = base;
        z->bitbuf = 0; z->bitcnt = 0; z->state = 0; z->last = 0;
        z->stored_left = 0; z->copy_len = 0; z->wpos = 0; z->win = d->win;
        b0 = src_peek(&z->raw);
        if (b0 >= 0 && (b0 & 0x0F) == 8) {
            z->raw.pos++;
            b1 = src_peek(&z->raw);
            if (b1 >= 0 && ((pu16)((b0 << 8) | b1)) % 31U == 0) z->raw.pos++;
            else z->raw.pos--;                      /* raw deflate */
        }
        s->buf = outbuf; s->size = outsize; s->pos = s->len = 0;
        s->kind = SRC_INFLATE; s->eof = 0; s->inf = z; s->off = 0; s->limit = 0; s->inner = 0; s->doc = d;
        s->tpos = 0;
    }
    return 0;
}

/* ====================================================================== */
/* lexer                                                                   */
/* ====================================================================== */
#define IS_WS(c)  ((c) == ' ' || (c) == '\n' || (c) == '\r' || (c) == '\t' || (c) == '\f' || (c) == 0)
#define IS_DEL(c) ((c) == '(' || (c) == ')' || (c) == '<' || (c) == '>' || (c) == '[' || (c) == ']' || \
                   (c) == '{' || (c) == '}' || (c) == '/' || (c) == '%')

static void tok_put(Tok PFAR *t, pi16 c) { if (t->len < TOKMAX - 1) t->s[t->len++] = (pu8)c; }

pi16 lex(Src PFAR *s, Tok PFAR *t)
{
    pi16 c, h;
    t->len = 0; t->isint = 0; t->num = 0; t->ival = 0;
    for (;;) {
        c = src_get(s);
        if (c < 0) { t->type = T_EOF; return T_EOF; }
        if (IS_WS(c)) continue;
        if (c == '%') { while (c >= 0 && c != '\n' && c != '\r') c = src_get(s); continue; }
        break;
    }
    switch (c) {
    case '[': return t->type = T_ABEG;
    case ']': return t->type = T_AEND;
    case '{': return t->type = T_PBEG;
    case '}': return t->type = T_PEND;
    case '>': if (src_peek(s) == '>') src_get(s); return t->type = T_DEND;
    case '/':
        for (;;) {
            c = src_peek(s);
            if (c < 0 || IS_WS(c) || IS_DEL(c)) break;
            src_get(s);
            if (c == '#') {
                pi16 a = hexval(src_peek(s)); if (a >= 0) src_get(s);
                h = hexval(src_peek(s)); if (h >= 0) src_get(s);
                if (a >= 0 && h >= 0) c = a * 16 + h;
            }
            tok_put(t, c);
        }
        t->s[t->len] = 0;
        return t->type = T_NAME;
    case '<':
        if (src_peek(s) == '<') { src_get(s); return t->type = T_DBEG; }
        h = -1;
        for (;;) {
            c = src_get(s);
            if (c < 0 || c == '>') break;
            c = hexval(c);
            if (c < 0) continue;
            if (h < 0) h = c; else { tok_put(t, h * 16 + c); h = -1; }
        }
        if (h >= 0) tok_put(t, h * 16);
        return t->type = T_STR;
    case '(': {
        pi16 depth = 1;
        for (;;) {
            c = src_get(s);
            if (c < 0) break;
            if (c == '(') depth++;
            else if (c == ')') { if (--depth == 0) break; }
            else if (c == '\\') {
                c = src_get(s);
                switch (c) {
                case 'n': c = '\n'; break;
                case 'r': c = '\r'; break;
                case 't': c = '\t'; break;
                case 'b': c = 8; break;
                case 'f': c = 12; break;
                case '\r': if (src_peek(s) == '\n') src_get(s); continue;
                case '\n': continue;
                default:
                    if (c >= '0' && c <= '7') {
                        pi16 v = c - '0', k;
                        for (k = 0; k < 2; k++) {
                            h = src_peek(s);
                            if (h < '0' || h > '7') break;
                            src_get(s); v = v * 8 + (h - '0');
                        }
                        c = v & 0xFF;
                    }
                }
            }
            tok_put(t, c);
        }
        return t->type = T_STR;
    }
    }
    if ((c >= '0' && c <= '9') || c == '-' || c == '+' || c == '.') {
        pi16 neg = 0, frac = 0; pi32 ip = 0; pu32 fp = 0, div = 1; pi16 any = 0;
        if (c == '-') neg = 1;
        else if (c == '.') frac = 1;
        else if (c != '+') { ip = c - '0'; any = 1; }
        for (;;) {
            c = src_peek(s);
            if (c >= '0' && c <= '9') {
                src_get(s); any = 1;
                if (!frac) { if (ip < 100000000L) ip = ip * 10 + (c - '0'); }
                else if (div < 100000UL) { fp = fp * 10 + (pu32)(c - '0'); div *= 10; }
            } else if (c == '.' && !frac) { src_get(s); frac = 1; }
            else if (c == '-' ) { src_get(s); }        /* tolerate 0.0-1 junk */
            else break;
        }
        (void)any;
        t->isint = !frac;
        t->ival = neg ? -ip : ip;
        {
            fx v = (ip > 32767 ? FX(32767) : FX(ip));
            if (fp) v += (fx)((fp << 16) / div);
            t->num = neg ? -v : v;
        }
        return t->type = T_NUM;
    }
    /* keyword */
    tok_put(t, c);
    for (;;) {
        c = src_peek(s);
        if (c < 0 || IS_WS(c) || IS_DEL(c)) break;
        tok_put(t, src_get(s));
    }
    t->s[t->len] = 0;
    return t->type = T_KW;
}

#define KW(t, w) ((t)->type == T_KW && p_streq((t)->s, (t)->len, (w)))

/* ====================================================================== */
/* object parser (arena)                                                   */
/* ====================================================================== */

static pu16 a_alloc(PdfDoc PFAR *d, pu16 n)
{
    pu16 o = (d->atop + 3) & ~3U;
    if ((pu32)o + n > ARENA_SIZE) return NIL;
    d->atop = o + n;
    return o;
}

static pu16 new_node(PdfDoc PFAR *d, pu8 type)
{
    pu16 o = a_alloc(d, sizeof(Node));
    if (o != NIL) {
        Node PFAR *n = NODE(d, o);
        n->type = type; n->isint = 0; n->next = NIL; n->child = NIL; n->len = 0; n->val = 0;
    }
    return o;
}

static pu16 node_str(PdfDoc PFAR *d, pu8 type, Tok PFAR *t)
{
    pu16 o = new_node(d, type), so;
    if (o == NIL) return NIL;
    so = a_alloc(d, t->len + 1);
    if (so == NIL) return NIL;
    p_memcpy(d->arena + so, t->s, t->len);
    d->arena[so + t->len] = 0;
    NODE(d, o)->child = so; NODE(d, o)->len = t->len;
    return o;
}

/* After an integer: does "<ws> gen <ws> R" follow? Consumes it if so.
 * Works on the buffer only, so it is fine for compressed streams too. */
static pi16 is_ref_ahead(Src PFAR *s)
{
    pu16 p, st;
    pu8 c;
    src_topup(s, 24);
    p = s->pos;
    st = p;
    while (p < s->len && IS_WS(s->buf[p])) p++;
    if (p == st || p >= s->len || s->buf[p] < '0' || s->buf[p] > '9') return 0;
    while (p < s->len && s->buf[p] >= '0' && s->buf[p] <= '9') p++;
    st = p;
    while (p < s->len && IS_WS(s->buf[p])) p++;
    if (p == st || p >= s->len || s->buf[p] != 'R') return 0;
    p++;
    if (p < s->len) { c = s->buf[p]; if (!IS_WS(c) && !IS_DEL(c)) return 0; }
    s->tpos += p - s->pos;
    s->pos = p;
    return 1;
}

/* Parse one object; first token already in d->tok. Handles "n g R". */
static pu16 parse_obj(PdfDoc PFAR *d, Src PFAR *s);

static pu16 parse_tokobj(PdfDoc PFAR *d, Src PFAR *s)
{
    Tok PFAR *t = &d->tok;
    pu16 o, last, c;
    switch (t->type) {
    case T_NUM:
        o = new_node(d, N_NUM);
        if (o == NIL) return NIL;
        NODE(d, o)->isint = t->isint;
        NODE(d, o)->val = t->isint ? t->ival : t->num;
        if (t->isint && t->ival >= 0 && is_ref_ahead(s)) {
            NODE(d, o)->type = N_REF;
        }
        return o;
    case T_NAME: return node_str(d, N_NAME, t);
    case T_STR:  return node_str(d, N_STR, t);
    case T_ABEG:
        o = new_node(d, N_ARR); if (o == NIL) return NIL;
        last = NIL;
        for (;;) {
            lex(s, t);
            if (t->type == T_AEND || t->type == T_EOF || t->type == T_ERR) break;
            c = parse_tokobj(d, s);
            if (c == NIL) return NIL;
            if (last == NIL) NODE(d, o)->child = c; else NODE(d, last)->next = c;
            last = c; NODE(d, o)->len++;
        }
        return o;
    case T_DBEG:
        o = new_node(d, N_DICT); if (o == NIL) return NIL;
        last = NIL;
        for (;;) {
            lex(s, t);
            if (t->type == T_DEND || t->type == T_EOF || t->type == T_ERR) break;
            if (t->type != T_NAME) continue;          /* tolerate junk */
            c = node_str(d, N_NAME, t);
            if (c == NIL) return NIL;
            if (last == NIL) NODE(d, o)->child = c; else NODE(d, last)->next = c;
            last = c;
            lex(s, t);
            if (t->type == T_DEND) { c = new_node(d, N_NULL); NODE(d, last)->next = c; break; }
            c = parse_tokobj(d, s);
            if (c == NIL) return NIL;
            NODE(d, last)->next = c; last = c; NODE(d, o)->len++;
        }
        return o;
    case T_KW:
        if (KW(t, (KS + 0)) || KW(t, (KS + 5))) {
            o = new_node(d, N_BOOL); if (o != NIL) NODE(d, o)->val = (t->s[0] == 't');
            return o;
        }
        return new_node(d, N_NULL);
    default:
        return new_node(d, N_NULL);
    }
}

static pu16 parse_obj(PdfDoc PFAR *d, Src PFAR *s)
{
    lex(s, &d->tok);
    return parse_tokobj(d, s);
}

/* inline image dictionary after BI, up to and including the ID keyword */
pu16 parse_inline_dict(PdfDoc PFAR *d, Src PFAR *s)
{
    Tok PFAR *t = &d->tok;
    pu16 o = new_node(d, N_DICT), last = NIL, c;
    if (o == NIL) return NIL;
    for (;;) {
        lex(s, t);
        if (t->type == T_EOF || t->type == T_ERR) return NIL;
        if (t->type == T_KW && KW(t, (KS + 11))) return o;
        if (t->type != T_NAME) continue;
        c = node_str(d, N_NAME, t);
        if (c == NIL) return NIL;
        if (last == NIL) NODE(d, o)->child = c; else NODE(d, last)->next = c;
        last = c;
        lex(s, t);
        if (t->type == T_KW && KW(t, (KS + 11))) { c = new_node(d, N_NULL); NODE(d, last)->next = c; return o; }
        c = parse_tokobj(d, s);
        if (c == NIL) return NIL;
        NODE(d, last)->next = c; last = c; NODE(d, o)->len++;
    }
}

/* ---- xref ---- */
static pi32 xref_lookup(PdfDoc PFAR *d, pi32 num)
{
    pu16 i; pu8 e[20]; pi16 n, k; pi32 off;
    for (i = 0; i < d->nsec; i++) {
        Sec PFAR *x = &d->sec[i];
        if (num >= (pi32)x->start && num < (pi32)x->start + x->count) {
            n = d->rd(d->file, x->off + (num - x->start) * 20L, e, 20);
            if (n < 18) return -1;
            if (e[17] != 'n') { if (e[17] == 'f') return 0; continue; }
            off = 0;
            for (k = 0; k < 10; k++) off = off * 10 + (e[k] - '0');
            return off;
        }
    }
    return 0;
}

/* Load indirect object num; returns node (NIL on failure).
 * Sets d->streamoff to stream data offset or -1. */
static pu16 load_from_objstm(PdfDoc PFAR *d, pi32 stm, pu16 idx, pi32 num);

pu16 load_obj(PdfDoc PFAR *d, pi32 num)
{
    pi32 off;
    pu16 o; Tok PFAR *t = &d->tok;
    d->streamoff = -1;
    if (d->xtab && num >= 0 && num < (pi32)d->xcap && d->xtab[num] != XT_UNSET) {
        pu32 e = d->xtab[num];
        if ((e >> 30) == 2) return load_from_objstm(d, (pi32)((e >> 12) & 0x3FFFFUL), (pu16)(e & 0xFFF), num);
        if ((e >> 30) != 1) return NIL;
        off = (pi32)(e & 0x3FFFFFFFUL);
    } else off = xref_lookup(d, num);
    if (off <= 0) return NIL;
    src_seek(&d->lk, off);
    lex(&d->lk, t); if (t->type != T_NUM) return NIL;
    lex(&d->lk, t); if (t->type != T_NUM) return NIL;
    lex(&d->lk, t); if (!KW(t, (KS + 14))) return NIL;
    o = parse_obj(d, &d->lk);
    if (o == NIL) return NIL;
    if (NODE(d, o)->type == N_DICT) {
        lex(&d->lk, t);
        if (KW(t, (KS + 18))) {
            pi16 c = src_get(&d->lk);
            if (c == '\r') { if (src_peek(&d->lk) == '\n') src_get(&d->lk); }
            d->streamoff = src_tell(&d->lk);
        }
    }
    return o;
}

pu16 resolve(PdfDoc PFAR *d, pu16 o)
{
    pi16 guard = 0;
    while (o != NIL && NODE(d, o)->type == N_REF && guard++ < 8)
        o = load_obj(d, NODE(d, o)->val);
    return o;
}

pu16 dict_get(PdfDoc PFAR *d, pu16 dict, const char PFAR *key)
{
    pu16 k;
    if (dict == NIL || NODE(d, dict)->type != N_DICT) return NIL;
    for (k = NODE(d, dict)->child; k != NIL; ) {
        Node PFAR *kn = NODE(d, k);
        pu16 v = kn->next;
        if (v == NIL) break;
        if (kn->type == N_NAME && p_streq(d->arena + kn->child, kn->len, key)) return v;
        k = NODE(d, v)->next;
    }
    return NIL;
}

pu16 dict_getr(PdfDoc PFAR *d, pu16 dict, const char PFAR *key)
{ return resolve(d, dict_get(d, dict, key)); }

pu16 arr_at(PdfDoc PFAR *d, pu16 arr, pu16 idx)
{
    pu16 c;
    if (arr == NIL || NODE(d, arr)->type != N_ARR) return NIL;
    for (c = NODE(d, arr)->child; c != NIL && idx; idx--) c = NODE(d, c)->next;
    return c;
}

pi32 node_int(PdfDoc PFAR *d, pu16 o, pi32 dflt)
{
    Node PFAR *n;
    if (o == NIL) return dflt;
    n = NODE(d, o);
    if (n->type != N_NUM) return dflt;
    return n->isint ? n->val : (n->val >> 16);
}

fx node_fx(PdfDoc PFAR *d, pu16 o, fx dflt)
{
    Node PFAR *n;
    if (o == NIL) return dflt;
    n = NODE(d, o);
    if (n->type != N_NUM) return dflt;
    if (!n->isint) return n->val;
    if (n->val > 32767) return FX(32767);
    if (n->val < -32767) return -FX(32767);
    return FX(n->val);
}

pi16 node_is(PdfDoc PFAR *d, pu16 o, pu8 type, const char PFAR *name)
{
    Node PFAR *n;
    if (o == NIL) return 0;
    n = NODE(d, o);
    if (n->type != type) return 0;
    if (!name) return 1;
    return p_streq(d->arena + n->child, n->len, name);
}

/* Stream length + Flate flag for the stream dict s (data at d->streamoff).
 * Returns 0 ok, 1 unsupported filter, -1 error. */
pi16 stream_info(PdfDoc PFAR *d, pu16 sd, pi32 PFAR *len, pu8 PFAR *flags)
{
    pi32 off = d->streamoff;
    pu16 mark = d->atop, f, i, n = 1;
    pi32 l = node_int(d, dict_getr(d, sd, (KS + 25)), -1);
    pi16 bad = 0;
    *flags = 0;
    f = dict_getr(d, sd, (KS + 32));
    if (f != NIL && NODE(d, f)->type == N_ARR) n = NODE(d, f)->len;
    for (i = 0; f != NIL && i < n; i++) {
        pu16 x = NODE(d, f)->type == N_ARR ? resolve(d, arr_at(d, f, i)) : f;
        if (node_is(d, x, N_NAME, (KS + 39)) || node_is(d, x, N_NAME, (KS + 51))) {
            if (*flags & SF_FLATE) bad = 1;
            *flags |= SF_FLATE;
        } else if ((node_is(d, x, N_NAME, (KS + 54)) || node_is(d, x, N_NAME, (KS + 68))) && i == 0 && !*flags)
            *flags |= SF_A85;
        else if ((node_is(d, x, N_NAME, (KS + 72)) || node_is(d, x, N_NAME, (KS + 87))) && i == 0 && !*flags)
            *flags |= SF_AHX;
        else if ((node_is(d, x, N_NAME, (KS + 91)) || node_is(d, x, N_NAME, (KS + 101))) && i == n - 1)
            *flags |= SF_DCT;
        else bad = 1;
    }
    d->streamoff = off;
    d->atop = mark;
    if (bad) return 1;
    if (l < 0) return -1;
    *len = l;
    return 0;
}

/* Object number num, index idx in object stream stm. The stream is decoded
 * with the shared inflater, which is impossible while a content stream is
 * being read (the caller then gets NIL). */
static pu16 load_from_objstm(PdfDoc PFAR *d, pi32 stm, pu16 idx, pi32 num)
{
    pu16 sd, i, n, o;
    pi32 first, len, target = -1, soff;
    pu8 flags;
    Src s; Tok PFAR *t = &d->tok;
    (void)idx;
    if (d->incontent) return NIL;
    if (d->xtab && stm < (pi32)d->xcap && (d->xtab[stm] >> 30) != 1) return NIL;
    sd = load_obj(d, stm);
    soff = d->streamoff;
    if (sd == NIL || soff < 0) return NIL;
    n = (pu16)node_int(d, dict_getr(d, sd, (KS + 105)), 0);
    first = node_int(d, dict_getr(d, sd, (KS + 107)), -1);
    d->streamoff = soff;
    if (first < 0 || stream_info(d, sd, &len, &flags) != 0) return NIL;
    src_stream(d, &s, d->rawbuf, d->cbuf, CBUF_SIZE, d->streamoff, len, flags);
    for (i = 0; i < n; i++) {
        pi32 on, of;
        lex(&s, t); if (t->type != T_NUM) return NIL;
        on = t->ival;
        lex(&s, t); if (t->type != T_NUM) return NIL;
        of = t->ival;
        if (on == num) { target = first + of; break; }
    }
    if (target < 0) return NIL;
    while ((pi32)s.tpos < target) if (src_get(&s) < 0) return NIL;
    o = parse_obj(d, &s);
    d->streamoff = -1;
    return o;
}

/* Cross-reference stream at file offset off (PDF 1.5+). */
static pi16 read_xref(PdfDoc PFAR *d, pi32 off, pi16 depth);

static pi16 read_xref_stream(PdfDoc PFAR *d, pi32 off, pi16 depth)
{
    Tok PFAR *t = &d->tok;
    pu16 mark = d->atop, sd, wa, ix, dp, c;
    pi16 w[3], i, rowlen, pred = 0, cols = 1;
    pi32 len, prev, size;
    pu8 flags, prow[32], row[33];
    Src s;
    if (!d->xtab) return PDF_E_XREFSTM;
    src_seek(&d->lk, off);
    lex(&d->lk, t); if (t->type != T_NUM) return PDF_E_XREF;
    lex(&d->lk, t); if (t->type != T_NUM) return PDF_E_XREF;
    lex(&d->lk, t); if (!KW(t, (KS + 14))) return PDF_E_XREF;
    sd = parse_obj(d, &d->lk);
    if (sd == NIL || NODE(d, sd)->type != N_DICT) return PDF_E_XREF;
    lex(&d->lk, t);
    if (!KW(t, (KS + 18))) return PDF_E_XREF;
    c = (pu16)src_get(&d->lk);
    if (c == '\r' && src_peek(&d->lk) == '\n') src_get(&d->lk);
    d->streamoff = src_tell(&d->lk);
    if (dict_get(d, sd, (KS + 113)) != NIL) return PDF_E_ENCRYPT;
    if (d->root < 0) {
        pu16 r = dict_get(d, sd, (KS + 121));
        if (r != NIL && NODE(d, r)->type == N_REF) d->root = NODE(d, r)->val;
    }
    wa = dict_getr(d, sd, (KS + 126));
    for (i = 0; i < 3; i++) w[i] = (pi16)node_int(d, resolve(d, arr_at(d, wa, (pu16)i)), 0);
    rowlen = w[0] + w[1] + w[2];
    if (rowlen <= 0 || rowlen > 32 || w[0] > 4 || w[1] > 4 || w[2] > 4) return PDF_E_XREF;
    size = node_int(d, dict_getr(d, sd, (KS + 128)), 0);
    prev = node_int(d, dict_getr(d, sd, (KS + 133)), -1);
    dp = dict_getr(d, sd, (KS + 138));
    if (dp != NIL && NODE(d, dp)->type == N_ARR) dp = resolve(d, arr_at(d, dp, 0));
    if (dp != NIL) {
        pred = (pi16)node_int(d, dict_getr(d, dp, (KS + 150)), 0);
        cols = (pi16)node_int(d, dict_getr(d, dp, (KS + 160)), 1);
        if (pred >= 10 && cols != rowlen) return PDF_E_XREF;
    }
    if (stream_info(d, sd, &len, &flags) != 0) return PDF_E_FILTER;
    src_stream(d, &s, d->rawbuf, d->cbuf, CBUF_SIZE, d->streamoff, len, flags);
    for (i = 0; i < 32; i++) prow[i] = 0;
    ix = dict_getr(d, sd, (KS + 168));
    {
        pu16 k = (ix != NIL && NODE(d, ix)->type == N_ARR) ? NODE(d, ix)->child : NIL;
        pi32 start = 0, count = size;
        for (;;) {
            pi32 j;
            if (k != NIL) {
                pu16 k2 = NODE(d, k)->next;
                if (k2 == NIL) break;
                start = node_int(d, k, 0); count = node_int(d, k2, 0);
                k = NODE(d, k2)->next;
            }
            for (j = 0; j < count; j++) {
                pi32 num = start + j;
                pu32 f[3];
                pi16 p = 0, q;
                if (pred >= 10) {
                    pi16 ft = src_get(&s);
                    for (q = 0; q < rowlen; q++) {
                        pi16 b = src_get(&s);
                        if (b < 0) goto done;
                        if (ft == 2) b = (pu8)(b + prow[q]);
                        else if (ft == 1 && q > 0) b = (pu8)(b + row[q - 1]);
                        row[q] = (pu8)b;
                    }
                    for (q = 0; q < rowlen; q++) prow[q] = row[q];
                } else {
                    for (q = 0; q < rowlen; q++) {
                        pi16 b = src_get(&s);
                        if (b < 0) goto done;
                        row[q] = (pu8)b;
                    }
                }
                for (i = 0; i < 3; i++) {
                    pi16 m;
                    f[i] = 0;
                    for (m = 0; m < w[i]; m++) f[i] = (f[i] << 8) | row[p++];
                }
                if (w[0] == 0) f[0] = 1;
                if (num >= 0 && num < (pi32)d->xcap && d->xtab[num] == XT_UNSET) {
                    if (f[0] == 0) d->xtab[num] = 0;
                    else if (f[0] == 1) d->xtab[num] = 0x40000000UL | (f[1] & 0x3FFFFFFFUL);
                    else if (f[0] == 2) d->xtab[num] = 0x80000000UL | ((f[1] & 0x3FFFFUL) << 12) | (f[2] & 0xFFF);
                }
            }
            if (k == NIL) break;
        }
    }
done:
    d->atop = mark;
    if (prev > 0 && depth < 16) return read_xref(d, prev, depth + 1);
    return PDF_OK;
}

/* Damaged file: find "n g obj" everywhere and the catalog among them. */
static pi16 rebuild_xref(PdfDoc PFAR *d)
{
    pi32 pos = 0, num;
    pu16 i;
    pu8 PFAR *b = d->win;                  /* free while opening */
    if (!d->xtab) return PDF_E_XREF;
    for (i = 0; i < d->xcap; i++) d->xtab[i] = XT_UNSET;
    d->nsec = 0;
    while (pos < d->fsize) {
        pi16 n = d->rd(d->file, pos, b, 16384);
        pi16 k;
        if (n <= 0) break;
        for (k = 0; k + 8 < n; k++) {
            pi16 p = k, q;
            pi32 v = 0;
            if (k > 0 && !IS_WS(b[k - 1])) continue;
            if (b[p] < '0' || b[p] > '9') continue;
            while (p < n && b[p] >= '0' && b[p] <= '9') { v = v * 10 + (b[p] - '0'); p++; }
            q = p;
            while (p < n && IS_WS(b[p])) p++;
            if (p == q || p >= n || b[p] < '0' || b[p] > '9') continue;
            while (p < n && b[p] >= '0' && b[p] <= '9') p++;
            q = p;
            while (p < n && IS_WS(b[p])) p++;
            if (p == q || p + 3 > n || b[p] != 'o' || b[p + 1] != 'b' || b[p + 2] != 'j') continue;
            if (v < (pi32)d->xcap) d->xtab[v] = 0x40000000UL | (pu32)(pos + k);   /* later copies win */
            k = p + 2;
        }
        if (n < 16384) break;
        pos += n - 32;                     /* overlap for patterns on the edge */
    }
    d->root = -1;
    for (num = 1; num < (pi32)d->xcap && d->root < 0; num++) {
        pu16 o;
        if ((d->xtab[num] >> 30) != 1) continue;
        o = load_obj(d, num);
        if (node_is(d, dict_get(d, o, (KS + 174)), N_NAME, (KS + 179))) d->root = num;
        d->atop = 0;
    }
    return d->root < 0 ? PDF_E_XREF : PDF_OK;
}

/* ====================================================================== */
/* open / xref                                                             */
/* ====================================================================== */
static pi16 read_xref(PdfDoc PFAR *d, pi32 off, pi16 depth)
{
    Tok PFAR *t = &d->tok;
    pu16 mark = d->atop, tr;
    pi32 prev;
    if (depth > 8) return PDF_E_XREF;
    src_seek(&d->lk, off);
    lex(&d->lk, t);
    if (t->type == T_NUM) return read_xref_stream(d, off, depth);
    if (!KW(t, (KS + 187))) return PDF_E_XREF;
    for (;;) {
        pi32 start, count;
        lex(&d->lk, t);
        if (KW(t, (KS + 192))) break;
        if (t->type != T_NUM) return PDF_E_XREF;
        start = t->ival;
        lex(&d->lk, t);
        if (t->type != T_NUM) return PDF_E_XREF;
        count = t->ival;
        /* skip whitespace to first entry */
        while (IS_WS(src_peek(&d->lk))) src_get(&d->lk);
        if (d->nsec < MAXSEC && count > 0) {
            Sec PFAR *x = &d->sec[d->nsec++];
            x->start = (pu16)start; x->count = (pu16)count; x->off = src_tell(&d->lk);
        }
        src_seek(&d->lk, src_tell(&d->lk) + count * 20L);
    }
    tr = parse_obj(d, &d->lk);
    if (tr == NIL || NODE(d, tr)->type != N_DICT) return PDF_E_XREF;
    if (dict_get(d, tr, (KS + 113)) != NIL) return PDF_E_ENCRYPT;
    if (d->root < 0) {
        pu16 r = dict_get(d, tr, (KS + 121));
        if (r != NIL && NODE(d, r)->type == N_REF) d->root = NODE(d, r)->val;
    }
    prev = node_int(d, dict_get(d, tr, (KS + 133)), -1);
    {
        pi32 xs = node_int(d, dict_get(d, tr, (KS + 200)), -1);     /* hybrid file */
        d->atop = mark;
        if (xs > 0 && d->xtab) read_xref_stream(d, xs, 16);
    }
    d->atop = mark;
    if (prev > 0) {
        pi16 e = read_xref(d, prev, depth + 1);
        if (e == PDF_E_XREFSTM) return PDF_OK;   /* hybrid file: table is enough */
        return e;
    }
    return PDF_OK;
}

pi16 PAPI pdf_open(pu16 file, pi32 sz, PdfReadFn rd, pu8 PFAR *work, pu16 worksize,
                   pu8 PFAR *xref, pu16 xrefsize)
{
    PdfDoc PFAR *d;
    pu8 PFAR *p = work;
    pu8 PFAR *tail;
    pi32 pos, sx;
    pi16 n, i, e;
    pu32 need = (pu32)sizeof(PdfDoc) + WIN_SIZE + ARENA_SIZE + FPOOL_SIZE +
                LKBUF_SIZE + RAWBUF_SIZE + CBUF_SIZE + GBUF_SIZE + 16;
    if ((pu32)worksize < need) return PDF_E_NOMEM;
    d = (PdfDoc PFAR *)p; p += (sizeof(PdfDoc) + 3) & ~3U;
    p_memset(d, 0, sizeof(PdfDoc));
    d->win = p; p += WIN_SIZE;
    d->arena = p; p += ARENA_SIZE;
    d->fpool = p; p += FPOOL_SIZE;
    d->lkbuf = p; p += LKBUF_SIZE;
    d->rawbuf = p; p += RAWBUF_SIZE;
    d->cbuf = p; p += CBUF_SIZE;
    d->gbuf = p;
    d->file = file; d->rd = rd;
    if (xref && xrefsize >= 64) {
        pu16 i;
        d->xtab = (pu32 PFAR *)xref; d->xcap = xrefsize / 4;
        for (i = 0; i < d->xcap; i++) d->xtab[i] = XT_UNSET;
    }
    tail = d->win;                     /* window is free while opening */
    d->root = -1; d->pages = -1;
    src_file(d, &d->lk, d->lkbuf, LKBUF_SIZE, 0, -1);

    n = rd(file, 0, tail, 8);
    if (n < 5 || tail[0] != '%' || tail[1] != 'P' || tail[2] != 'D' || tail[3] != 'F') return PDF_E_NOTPDF;
    if (sz <= 0) return PDF_E_IO;
    d->fsize = sz;
    pos = sz > 1024 ? sz - 1024 : 0;
    n = rd(file, pos, tail, (pu16)(sz - pos));
    if (n <= 9) return PDF_E_IO;
    sx = -1;
    for (i = n - 9; i >= 0; i--) {
        if (tail[i] == 's' && p_streq(tail + i, 9, (KS + 208))) {
            pi16 k = i + 9; sx = 0;
            while (k < n && IS_WS(tail[k])) k++;
            while (k < n && tail[k] >= '0' && tail[k] <= '9') sx = sx * 10 + (tail[k++] - '0');
            break;
        }
    }
    e = sx > 0 ? read_xref(d, sx, 0) : PDF_E_XREF;
    if (e == PDF_E_ENCRYPT || e == PDF_E_XREFSTM) return e;
    if (e || d->root < 0 || load_obj(d, d->root) == NIL) {
        d->atop = 0;
        e = rebuild_xref(d);               /* damaged or missing xref */
        if (e) return e;
    }
    d->atop = 0;
    {
        pu16 r = load_obj(d, d->root), pg;
        if (r == NIL) return PDF_E_XREF;
        pg = dict_get(d, r, (KS + 218));
        if (pg == NIL || NODE(d, pg)->type != N_REF) return PDF_E_XREF;
        d->pages = NODE(d, pg)->val;
        r = load_obj(d, d->pages);
        d->npages = (pi16)node_int(d, dict_getr(d, r, (KS + 224)), 0);
        d->atop = 0;
        if (d->npages <= 0) return PDF_E_PAGE;
    }
    return PDF_OK;
}

pi16 PAPI pdf_page_count(pu8 PFAR *work) { return ((PdfDoc PFAR *)work)->npages; }

/* ====================================================================== */
/* page tree                                                               */
/* ====================================================================== */
static void get_box(PdfDoc PFAR *d, pu16 a, fx PFAR *box)
{
    pi16 i;
    a = resolve(d, a);
    if (a == NIL || NODE(d, a)->type != N_ARR || NODE(d, a)->len < 4) return;
    for (i = 0; i < 4; i++) box[i] = node_fx(d, resolve(d, arr_at(d, a, (pu16)i)), 0);
    if (box[0] > box[2]) { fx t = box[0]; box[0] = box[2]; box[2] = t; }
    if (box[1] > box[3]) { fx t = box[1]; box[1] = box[3]; box[3] = t; }
}

/* Find page idx: sets d->pageobj, d->resobj, d->mbox. */
pi16 find_page(PdfDoc PFAR *d, pi16 idx)
{
    pi32 node = d->pages, resobj = -1;
    pi16 rot = 0;
    fx box[4];
    pi16 guard = 0;
    pu16 mark = d->atop;
    box[0] = 0; box[1] = 0; box[2] = FX(612); box[3] = FX(792);
    if (idx < 0 || idx >= d->npages) return PDF_E_PAGE;
    while (guard++ < 64) {
        pu16 o = load_obj(d, node), kids, k;
        if (o == NIL) break;
        if (dict_get(d, o, (KS + 230)) != NIL) resobj = node;
        if (dict_get(d, o, (KS + 240)) != NIL) get_box(d, dict_get(d, o, (KS + 240)), box);
        if (dict_get(d, o, (KS + 249)) != NIL) rot = (pi16)node_int(d, dict_getr(d, o, (KS + 249)), 0);
        kids = dict_getr(d, o, (KS + 256));
        if (kids == NIL || NODE(d, kids)->type != N_ARR) {
            o = load_obj(d, node);
            if (dict_get(d, o, (KS + 261)) != NIL) {
                fx cb[4]; cb[0] = box[0]; cb[1] = box[1]; cb[2] = box[2]; cb[3] = box[3];
                get_box(d, dict_get(d, o, (KS + 261)), cb);
                if (cb[2] > cb[0] && cb[3] > cb[1]) { box[0] = cb[0]; box[1] = cb[1]; box[2] = cb[2]; box[3] = cb[3]; }
            }
            d->pageobj = node; d->resobj = resobj;
            rot %= 360; if (rot < 0) rot += 360; d->rot = (pi16)((rot / 90) * 90);
            d->mbox[0] = box[0]; d->mbox[1] = box[1]; d->mbox[2] = box[2]; d->mbox[3] = box[3];
            d->atop = mark;
            return PDF_OK;
        }
        /* walk kids; kids array lives in arena, child loads go above it */
        for (k = NODE(d, kids)->child; k != NIL; k = NODE(d, k)->next) {
            pu16 m2 = d->atop, ko;
            pi32 kn, cnt;
            if (NODE(d, k)->type != N_REF) continue;
            kn = NODE(d, k)->val;
            ko = load_obj(d, kn);
            if (ko == NIL) { d->atop = m2; continue; }
            if (node_is(d, dict_get(d, ko, (KS + 174)), N_NAME, (KS + 218)) || dict_get(d, ko, (KS + 256)) != NIL)
                cnt = node_int(d, dict_getr(d, ko, (KS + 224)), 1);
            else cnt = 1;
            d->atop = m2;
            if (idx < cnt) { node = kn; break; }
            idx -= (pi16)cnt;
        }
        if (k == NIL) break;
        d->atop = mark;
    }
    d->atop = mark;
    return PDF_E_PAGE;
}

pi16 PAPI pdf_page_size(pu8 PFAR *work, pi16 page, pi16 PFAR *w, pi16 PFAR *h)
{
    PdfDoc PFAR *d = (PdfDoc PFAR *)work;
    pi16 e;
    e = find_page(d, page);
    if (e) return e;
    *w = (pi16)((d->mbox[2] - d->mbox[0] + 0x8000L) >> 16);
    *h = (pi16)((d->mbox[3] - d->mbox[1] + 0x8000L) >> 16);
    if (d->rot == 90 || d->rot == 270) { pi16 t = *w; *w = *h; *h = t; }
    return PDF_OK;
}

void PAPI pdf_errtext(pi16 err, char PFAR *buf, pu16 size)
{
    const char PFAR *m;
    pu16 i;
    switch (err) {
    case PDF_E_IO:      m = (KS + 269); break;
    case PDF_E_NOTPDF:  m = (KS + 280); break;
    case PDF_E_XREFSTM: m = (KS + 296); break;
    case PDF_E_XREF:    m = (KS + 343); break;
    case PDF_E_ENCRYPT: m = (KS + 374); break;
    case PDF_E_SYNTAX:  m = (KS + 394); break;
    case PDF_E_NOMEM:   m = (KS + 407); break;
    case PDF_E_PAGE:    m = (KS + 425); break;
    case PDF_E_FILTER:  m = (KS + 446); break;
    case PDF_E_INFLATE: m = (KS + 472); break;
    default:            m = (KS + 498); break;
    }
    for (i = 0; i + 1 < size && m[i]; i++) buf[i] = m[i];
    if (size) buf[i] = 0;
}

/* ---- pdfrend.c ---- */
/* SPDX-License-Identifier: MIT
 * pdfrend: fonts, content stream interpreter, rasteriser, display list. */

/* Generated by tools/gen_tables.py - do not edit. */
static const unsigned short TABSEG W_Helvetica[256] = {
  278,278,278,278,278,278,278,278,278,278,278,278,278,278,278,278,
  278,278,278,278,278,278,278,278,278,278,278,278,278,278,278,278,
  278,278,355,556,556,889,667,191,333,333,389,584,278,333,278,278,
  556,556,556,556,556,556,556,556,556,556,278,278,584,584,584,556,
  1015,667,667,722,722,667,611,778,722,278,500,667,556,833,722,778,
  667,778,722,667,611,722,667,944,667,667,611,278,278,278,469,556,
  333,556,556,500,556,556,278,556,556,222,222,500,222,833,556,556,
  556,556,333,500,278,556,500,722,500,500,500,334,260,334,584,500,
  667,667,722,667,722,778,722,556,556,556,556,556,556,500,556,556,
  556,556,278,278,278,278,556,556,556,556,556,556,556,556,556,556,
  556,400,556,556,556,350,537,611,737,737,1000,333,333,500,1000,778,
  500,584,500,500,556,556,500,500,500,500,500,370,365,500,889,611,
  611,333,584,500,556,500,500,556,556,1000,500,667,667,778,1000,944,
  556,1000,333,333,222,222,584,500,500,667,167,556,333,333,500,500,
  556,278,222,333,1000,667,667,667,667,667,278,278,278,278,778,778,
  500,778,722,722,722,278,333,333,333,333,333,333,333,333,333,333,
};
static const unsigned short TABSEG W_Helvetica_Bold[256] = {
  278,278,278,278,278,278,278,278,278,278,278,278,278,278,278,278,
  278,278,278,278,278,278,278,278,278,278,278,278,278,278,278,278,
  278,333,474,556,556,889,722,238,333,333,389,584,278,333,278,278,
  556,556,556,556,556,556,556,556,556,556,333,333,584,584,584,611,
  975,722,722,722,722,667,611,778,722,278,556,722,611,833,722,778,
  667,778,722,667,611,722,667,944,667,667,611,333,278,333,584,556,
  333,556,611,556,611,556,333,611,611,278,278,556,278,889,611,611,
  611,611,389,556,333,611,556,778,556,556,500,389,280,389,584,500,
  722,722,722,667,722,778,722,556,556,556,556,556,556,556,556,556,
  556,556,278,278,278,278,611,611,611,611,611,611,611,611,611,611,
  556,400,556,556,556,350,556,611,737,737,1000,333,333,500,1000,778,
  500,584,500,500,556,611,500,500,500,500,500,370,365,500,889,611,
  611,333,584,500,556,500,500,556,556,1000,500,722,722,778,1000,944,
  556,1000,500,500,278,278,584,500,556,667,167,556,333,333,611,611,
  556,278,278,500,1000,722,667,722,667,667,278,278,278,278,778,778,
  500,778,722,722,722,278,333,333,333,333,333,333,333,333,333,333,
};
static const unsigned short TABSEG W_Times_Roman[256] = {
  250,250,250,250,250,250,250,250,250,250,250,250,250,250,250,250,
  250,250,250,250,250,250,250,250,250,250,250,250,250,250,250,250,
  250,333,408,500,500,833,778,180,333,333,500,564,250,333,250,278,
  500,500,500,500,500,500,500,500,500,500,278,278,564,564,564,444,
  921,722,667,667,722,611,556,722,722,333,389,722,611,889,722,722,
  556,722,667,556,611,722,722,944,722,722,611,333,278,333,469,500,
  333,444,500,444,500,444,333,500,500,278,278,500,278,778,500,500,
  500,500,333,389,278,500,500,722,500,500,444,480,200,480,541,500,
  722,722,667,611,722,722,722,444,444,444,444,444,444,444,444,444,
  444,444,278,278,278,278,500,500,500,500,500,500,500,500,500,500,
  500,400,500,500,500,350,453,500,760,760,980,333,333,500,889,722,
  500,564,500,500,500,500,500,500,500,500,500,276,310,500,667,500,
  444,333,564,500,500,500,500,500,500,1000,500,722,722,722,889,722,
  500,1000,444,444,333,333,564,500,500,722,167,500,333,333,556,556,
  500,250,333,444,1000,722,611,722,611,611,333,333,333,333,722,722,
  500,722,722,722,722,278,333,333,333,333,333,333,333,333,333,333,
};
static const unsigned short TABSEG W_Times_Bold[256] = {
  250,250,250,250,250,250,250,250,250,250,250,250,250,250,250,250,
  250,250,250,250,250,250,250,250,250,250,250,250,250,250,250,250,
  250,333,555,500,500,1000,833,278,333,333,500,570,250,333,250,278,
  500,500,500,500,500,500,500,500,500,500,333,333,570,570,570,500,
  930,722,667,722,722,667,611,778,778,389,500,778,667,944,722,778,
  611,778,722,556,667,722,722,1000,722,722,667,333,278,333,581,500,
  333,500,556,444,556,444,333,500,556,278,333,556,278,833,556,500,
  556,556,444,389,333,556,500,722,500,500,444,394,220,394,520,500,
  722,722,722,667,722,778,722,500,500,500,500,500,500,444,444,444,
  444,444,278,278,278,278,556,500,500,500,500,500,556,556,556,556,
  500,400,500,500,500,350,540,556,747,747,1000,333,333,500,1000,778,
  500,570,500,500,500,556,500,500,500,500,500,300,330,500,722,500,
  500,333,570,500,500,500,500,500,500,1000,500,722,722,778,1000,722,
  500,1000,500,500,333,333,570,500,500,722,167,500,333,333,556,556,
  500,250,333,500,1000,722,667,722,667,667,389,389,389,389,778,778,
  500,778,722,722,722,278,333,333,333,333,333,333,333,333,333,333,
};
static const unsigned short TABSEG W_Times_Italic[256] = {
  250,250,250,250,250,250,250,250,250,250,250,250,250,250,250,250,
  250,250,250,250,250,250,250,250,250,250,250,250,250,250,250,250,
  250,333,420,500,500,833,778,214,333,333,500,675,250,333,250,278,
  500,500,500,500,500,500,500,500,500,500,333,333,675,675,675,500,
  920,611,611,667,722,611,611,722,722,333,444,667,556,833,667,722,
  611,722,611,500,556,722,611,833,611,556,556,389,278,389,422,500,
  333,500,500,444,500,444,278,500,500,278,278,444,278,722,500,500,
  500,500,389,389,278,500,444,667,444,444,389,400,275,400,541,500,
  611,611,667,611,667,722,722,500,500,500,500,500,500,444,444,444,
  444,444,278,278,278,278,500,500,500,500,500,500,500,500,500,500,
  500,400,500,500,500,350,523,500,760,760,980,333,333,500,889,722,
  500,675,500,500,500,500,500,500,500,500,500,276,310,500,667,500,
  500,389,675,500,500,500,500,500,500,889,500,611,611,722,944,667,
  500,889,556,556,333,333,675,500,444,556,167,500,333,333,500,500,
  500,250,333,556,1000,611,611,611,611,611,333,333,333,333,722,722,
  500,722,722,722,722,278,333,333,333,333,333,333,333,333,333,333,
};
static const unsigned short TABSEG W_Times_BoldItalic[256] = {
  250,250,250,250,250,250,250,250,250,250,250,250,250,250,250,250,
  250,250,250,250,250,250,250,250,250,250,250,250,250,250,250,250,
  250,389,555,500,500,833,778,278,333,333,500,570,250,333,250,278,
  500,500,500,500,500,500,500,500,500,500,333,333,570,570,570,500,
  832,667,667,667,722,667,667,722,778,389,500,667,611,889,722,722,
  611,722,667,556,611,722,667,889,667,611,611,333,278,333,570,500,
  333,500,500,444,500,444,333,500,556,278,278,500,278,778,556,500,
  500,500,389,389,278,556,444,667,500,444,389,348,220,348,570,500,
  667,667,667,667,722,722,722,500,500,500,500,500,500,444,444,444,
  444,444,278,278,278,278,556,500,500,500,500,500,556,556,556,556,
  500,400,500,500,500,350,500,500,747,747,1000,333,333,500,944,722,
  500,570,500,500,500,576,500,500,500,500,500,266,300,500,722,500,
  500,389,606,500,500,500,500,500,500,1000,500,667,667,722,944,722,
  500,1000,500,500,333,333,570,500,444,611,167,500,333,333,556,556,
  500,250,333,500,1000,667,667,667,667,667,389,389,389,389,722,722,
  500,722,722,722,722,278,333,333,333,333,333,333,333,333,333,333,
};
static const unsigned short TABSEG ENC_STD[256] = {
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  32,33,34,35,36,37,38,8217,40,41,42,43,44,45,46,47,
  48,49,50,51,52,53,54,55,56,57,58,59,60,61,62,63,
  64,65,66,67,68,69,70,71,72,73,74,75,76,77,78,79,
  80,81,82,83,84,85,86,87,88,89,90,91,92,93,94,95,
  8216,97,98,99,100,101,102,103,104,105,106,107,108,109,110,111,
  112,113,114,115,116,117,118,119,120,121,122,123,124,125,126,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,161,162,163,8260,165,402,167,164,39,8220,171,8249,8250,64257,64258,
  0,8211,8224,8225,183,0,182,8226,8218,8222,8221,187,8230,8240,0,191,
  0,96,180,710,732,175,728,729,168,0,730,184,0,733,731,711,
  8212,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,198,0,170,0,0,0,0,321,216,338,186,0,0,0,0,
  0,230,0,0,0,305,0,0,322,248,339,223,0,0,0,0,
};
static const unsigned short TABSEG ENC_WIN[256] = {
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,
  48,49,50,51,52,53,54,55,56,57,58,59,60,61,62,63,
  64,65,66,67,68,69,70,71,72,73,74,75,76,77,78,79,
  80,81,82,83,84,85,86,87,88,89,90,91,92,93,94,95,
  96,97,98,99,100,101,102,103,104,105,106,107,108,109,110,111,
  112,113,114,115,116,117,118,119,120,121,122,123,124,125,126,8226,
  8364,8226,8218,402,8222,8230,8224,8225,710,8240,352,8249,338,8226,381,8226,
  8226,8216,8217,8220,8221,8226,8211,8212,732,8482,353,8250,339,8226,382,376,
  32,161,162,163,164,165,166,167,168,169,170,171,172,45,174,175,
  176,177,178,179,180,181,182,183,184,185,186,187,188,189,190,191,
  192,193,194,195,196,197,198,199,200,201,202,203,204,205,206,207,
  208,209,210,211,212,213,214,215,216,217,218,219,220,221,222,223,
  224,225,226,227,228,229,230,231,232,233,234,235,236,237,238,239,
  240,241,242,243,244,245,246,247,248,249,250,251,252,253,254,255,
};
static const unsigned short TABSEG ENC_MAC[256] = {
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,
  48,49,50,51,52,53,54,55,56,57,58,59,60,61,62,63,
  64,65,66,67,68,69,70,71,72,73,74,75,76,77,78,79,
  80,81,82,83,84,85,86,87,88,89,90,91,92,93,94,95,
  96,97,98,99,100,101,102,103,104,105,106,107,108,109,110,111,
  112,113,114,115,116,117,118,119,120,121,122,123,124,125,126,0,
  196,197,199,201,209,214,220,225,224,226,228,227,229,231,233,232,
  234,235,237,236,238,239,241,243,242,244,246,245,250,249,251,252,
  8224,176,162,163,167,8226,182,223,174,169,8482,180,168,0,198,216,
  0,177,0,0,165,181,0,0,0,0,0,170,186,0,230,248,
  191,161,172,0,402,0,0,171,187,8230,32,192,195,213,338,339,
  8211,8212,8220,8221,8216,8217,247,0,255,376,8260,164,8249,8250,64257,64258,
  8225,183,8218,8222,8240,194,202,193,203,200,205,206,207,204,211,212,
  0,210,218,219,217,305,710,732,175,728,729,730,184,733,731,711,
};
static const unsigned char TABSEG U_A0_TO_GEOS[96] = {
  202,193,162,163,63,180,63,164,172,169,187,199,194,45,168,248,
  161,177,63,63,171,181,166,225,252,63,188,200,63,63,63,192,
  203,231,229,204,128,129,174,130,233,131,230,232,237,234,235,236,
  63,132,241,238,239,205,133,63,175,244,242,243,134,63,63,167,
  136,135,137,139,138,140,190,141,143,142,144,145,147,146,148,149,
  63,150,152,151,153,155,154,214,191,157,156,158,159,63,63,216,
};
static const unsigned short TABSEG UX_KEY[156] = {
  181,256,257,258,259,260,261,262,263,264,265,266,267,268,269,270,
  271,274,275,276,277,278,279,280,281,282,283,284,285,286,287,288,
  289,290,291,292,293,296,297,298,299,300,301,302,303,304,305,308,
  309,310,311,313,314,315,316,317,318,323,324,325,326,327,328,332,
  333,334,335,336,337,338,339,340,341,342,343,344,345,346,347,348,
  349,350,351,352,353,354,355,356,357,360,361,362,363,364,365,366,
  367,368,369,370,371,372,373,374,375,376,377,378,379,380,381,382,
  402,710,732,937,960,8194,8195,8201,8208,8209,8211,8212,8216,8217,8218,8220,
  8221,8222,8224,8225,8226,8230,8240,8242,8243,8249,8250,8364,8482,8706,8710,8719,
  8721,8722,8730,8734,8747,8776,8800,8804,8805,9674,64257,64258,
};
static const unsigned char TABSEG UX_VAL[156] = {
  181,65,97,65,97,65,97,67,99,67,99,67,99,67,99,68,
  100,69,101,69,101,69,101,69,101,69,101,71,103,71,103,71,
  103,71,103,72,104,73,105,73,105,73,105,73,105,73,245,74,
  106,75,107,76,108,76,108,76,108,78,110,78,110,78,110,79,
  111,79,111,79,111,206,207,82,114,82,114,82,114,83,115,83,
  115,83,115,83,115,84,116,84,116,85,117,85,117,85,117,85,
  117,85,117,85,117,87,119,89,121,217,90,122,90,122,90,122,
  196,246,247,189,185,32,32,32,45,45,208,209,212,213,226,210,
  211,227,160,224,165,201,228,39,34,220,221,219,170,182,198,184,
  183,45,195,176,186,197,173,178,179,215,222,223,
};
static const char TABSEG GN_STR[] =
  "A\0""AE\0""Aacute\0""Acircumflex\0""Adieresis\0""Agrave\0""Aring\0"
  "Atilde\0""B\0""C\0""Ccedilla\0""D\0""E\0""Eacute\0""Ecircumflex\0"
  "Edieresis\0""Egrave\0""Eth\0""Euro\0""F\0""G\0""H\0""I\0""Iacute\0"
  "Icircumflex\0""Idieresis\0""Igrave\0""J\0""K\0""L\0""Lslash\0""M\0""N\0"
  "Ntilde\0""O\0""OE\0""Oacute\0""Ocircumflex\0""Odieresis\0""Ograve\0"
  "Oslash\0""Otilde\0""P\0""Q\0""R\0""S\0""Scaron\0""T\0""Thorn\0""U\0"
  "Uacute\0""Ucircumflex\0""Udieresis\0""Ugrave\0""V\0""W\0""X\0""Y\0"
  "Yacute\0""Ydieresis\0""Z\0""Zcaron\0""a\0""aacute\0""acircumflex\0"
  "acute\0""adieresis\0""ae\0""agrave\0""ampersand\0""aring\0"
  "asciicircum\0""asciitilde\0""asterisk\0""at\0""atilde\0""b\0"
  "backslash\0""bar\0""braceleft\0""braceright\0""bracketleft\0"
  "bracketright\0""breve\0""brokenbar\0""bullet\0""c\0""caron\0""ccedilla\0"
  "cedilla\0""cent\0""circumflex\0""colon\0""comma\0""copyright\0"
  "currency\0""d\0""dagger\0""daggerdbl\0""degree\0""dieresis\0""divide\0"
  "dollar\0""dotaccent\0""dotlessi\0""e\0""eacute\0""ecircumflex\0"
  "edieresis\0""egrave\0""eight\0""ellipsis\0""emdash\0""endash\0""equal\0"
  "eth\0""exclam\0""exclamdown\0""f\0""fi\0""five\0""fl\0""florin\0""four\0"
  "fraction\0""g\0""germandbls\0""grave\0""greater\0""guillemotleft\0"
  "guillemotright\0""guilsinglleft\0""guilsinglright\0""h\0""hungarumlaut\0"
  "hyphen\0""i\0""iacute\0""icircumflex\0""idieresis\0""igrave\0""j\0""k\0"
  "l\0""less\0""logicalnot\0""lslash\0""m\0""macron\0""minus\0""mu\0"
  "multiply\0""n\0""nbspace\0""nine\0""ntilde\0""numbersign\0""o\0"
  "oacute\0""ocircumflex\0""odieresis\0""oe\0""ogonek\0""ograve\0""one\0"
  "onehalf\0""onequarter\0""onesuperior\0""ordfeminine\0""ordmasculine\0"
  "oslash\0""otilde\0""p\0""paragraph\0""parenleft\0""parenright\0"
  "percent\0""period\0""periodcentered\0""perthousand\0""plus\0"
  "plusminus\0""q\0""question\0""questiondown\0""quotedbl\0""quotedblbase\0"
  "quotedblleft\0""quotedblright\0""quoteleft\0""quoteright\0"
  "quotesinglbase\0""quotesingle\0""r\0""registered\0""ring\0""s\0"
  "scaron\0""section\0""semicolon\0""seven\0""sfthyphen\0""six\0""slash\0"
  "space\0""sterling\0""t\0""thorn\0""three\0""threequarters\0"
  "threesuperior\0""tilde\0""trademark\0""two\0""twosuperior\0""u\0"
  "uacute\0""ucircumflex\0""udieresis\0""ugrave\0""underscore\0""v\0""w\0"
  "x\0""y\0""yacute\0""ydieresis\0""yen\0""z\0""zcaron\0""zero\0"
;
static const unsigned short TABSEG GN_OFF[231] = {
  0,2,5,12,24,34,41,47,54,56,58,67,69,71,78,90,
  100,107,111,116,118,120,122,124,131,143,153,160,162,164,166,173,
  175,177,184,186,189,196,208,218,225,232,239,241,243,245,247,254,
  256,262,264,271,283,293,300,302,304,306,308,315,325,327,334,336,
  343,355,361,371,374,381,391,397,409,420,429,432,439,441,451,455,
  465,476,488,501,507,517,524,526,532,541,549,554,565,571,577,587,
  596,598,605,615,622,631,638,645,655,664,666,673,685,695,702,708,
  717,724,731,737,741,748,759,761,764,769,772,779,784,793,795,806,
  812,820,834,849,863,878,880,893,900,902,909,921,931,938,940,942,
  944,949,960,967,969,976,982,985,994,996,1004,1009,1016,1027,1029,1036,
  1048,1058,1061,1068,1075,1079,1087,1098,1110,1122,1135,1142,1149,1151,1161,1171,
  1182,1190,1197,1212,1224,1229,1239,1241,1250,1263,1272,1285,1298,1312,1322,1333,
  1348,1360,1362,1373,1378,1380,1387,1395,1405,1411,1421,1425,1431,1437,1446,1448,
  1454,1460,1474,1488,1494,1504,1508,1520,1522,1529,1541,1551,1558,1569,1571,1573,
  1575,1577,1584,1594,1598,1600,1607,
};
static const unsigned short TABSEG GN_UNI[231] = {
  65,198,193,194,196,192,197,195,66,67,199,68,69,201,202,203,
  200,208,8364,70,71,72,73,205,206,207,204,74,75,76,321,77,
  78,209,79,338,211,212,214,210,216,213,80,81,82,83,352,84,
  222,85,218,219,220,217,86,87,88,89,221,376,90,381,97,225,
  226,180,228,230,224,38,229,94,126,42,64,227,98,92,124,123,
  125,91,93,728,166,8226,99,711,231,184,162,710,58,44,169,164,
  100,8224,8225,176,168,247,36,729,305,101,233,234,235,232,56,8230,
  8212,8211,61,240,33,161,102,64257,53,64258,402,52,8260,103,223,96,
  62,171,187,8249,8250,104,733,45,105,237,238,239,236,106,107,108,
  60,172,322,109,175,8722,181,215,110,160,57,241,35,111,243,244,
  246,339,731,242,49,189,188,185,170,186,248,245,112,182,40,41,
  37,46,183,8240,43,177,113,63,191,34,8222,8220,8221,8216,8217,8218,
  39,114,174,730,115,353,167,59,55,173,54,47,32,163,116,254,
  51,190,179,732,8482,50,178,117,250,251,252,249,95,118,119,120,
  121,253,255,165,122,382,48,
};
#define GN_COUNT 231
#define UX_COUNT 156


/* Text top = baseline - size * TEXT_ASCENT / 256 (GrDrawText draws from the
 * top of the font box). Tune after looking at real GEOS output. */
#define TEXT_ASCENT 230
#define IS_WS2(c) ((c) == ' ' || (c) == '\n' || (c) == '\r' || (c) == '\t')

static const pu8 TABSEG PAL[16][3] = {
    {0,0,0},{0,0,170},{0,170,0},{0,170,170},{170,0,0},{170,0,170},{170,85,0},{170,170,170},
    {85,85,85},{85,85,255},{85,255,85},{85,255,255},{255,85,85},{255,85,255},{255,255,85},{255,255,255}
};

/* ====================================================================== */
/* output                                                                  */
/* ====================================================================== */
static pi16 dl_room(PdfDoc PFAR *d, pu16 n)
{
    if (d->trunc) return 0;
    if ((pu32)d->dlpos + n + 1 > d->dlsize) { d->trunc = 1; return 0; }
    return 1;
}
static void dl_b(PdfDoc PFAR *d, pu8 v) { d->dl[d->dlpos++] = v; }
static void dl_w(PdfDoc PFAR *d, pi16 v) { d->dl[d->dlpos++] = (pu8)v; d->dl[d->dlpos++] = (pu8)((pu16)v >> 8); }

static void out_rect(PdfDoc PFAR *d, pu8 col, pi16 x0, pi16 y0, pi16 x1, pi16 y1)
{
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 >= d->devw) x1 = d->devw - 1;
    if (y1 >= d->devh) y1 = d->devh - 1;
    if (x1 < x0 || y1 < y0) return;
    if (!dl_room(d, 10)) return;
    dl_b(d, DL_RECT); dl_b(d, col);
    dl_w(d, x0); dl_w(d, y0); dl_w(d, x1); dl_w(d, y1);
}

/* quarter pixel -> pixel range [a, b) with at least one pixel */
static void q2px(pi16 qa, pi16 qb, pi16 PFAR *a, pi16 PFAR *b)
{
    pi16 pa = (pi16)((qa + 2) >> 2), pb = (pi16)(((qb + 2) >> 2) - 1);
    if (pb < pa) { pa = (pi16)((qa + qb) >> 3); pb = pa; }
    *a = pa; *b = pb;
}

static pu8 rgb_index(pi16 r, pi16 g, pi16 b)
{
    pi16 i; pu8 best = 0; pu32 bd = 0xFFFFFFFFUL;
    for (i = 0; i < 16; i++) {
        pi32 dr = r - PAL[i][0], dg = g - PAL[i][1], db = b - PAL[i][2];
        pu32 dd = (pu32)(dr * dr * 3 + dg * dg * 4 + db * db * 2);
        if (dd < bd) { bd = dd; best = (pu8)i; }
    }
    /* Light tints (table headers, highlights) must not vanish into the white
     * page: only near-white stays white. */
    if (best == 15 && (r < 236 || g < 236 || b < 236)) {
        pi16 mx = r > g ? (r > b ? r : b) : (g > b ? g : b);
        pi16 mn = r < g ? (r < b ? r : b) : (g < b ? g : b);
        if (mx - mn < 48) return 7;
        if (b >= r && b >= g) return 11;
        if (r >= g) return 14;
        return 10;
    }
    return best;
}

static pi16 fx255(fx v)
{
    if (v <= 0) return 0;
    if (v >= FXONE) return 255;
    return (pi16)((v * 255L + 0x8000L) >> 16);
}

/* ====================================================================== */
/* span merging and polygon fill (quarter pixel coordinates)               */
/* ====================================================================== */
static void open_flush(PdfDoc PFAR *d, pu8 col, pi16 y)
{
    pi16 i;
    if (d->bm) { d->nopen = 0; return; }
    for (i = 0; i < d->nopen; i++) out_rect(d, col, d->ox0[i], d->oy[i], d->ox1[i], y - 1);
    d->nopen = 0;
}

/* cur spans for row y (sorted); merge with open rectangles */
static void row_spans(PdfDoc PFAR *d, pu8 col, pi16 y, pi16 PFAR *cx0, pi16 PFAR *cx1, pi16 nc)
{
    pi16 PFAR *nx0 = d->nx0, PFAR *nx1 = d->nx1, PFAR *ny = d->ny;
    pi16 nn = 0, i, j;
    if (d->bm) {                         /* bitmap mode: set the bits */
        pu8 PFAR *row = d->bm + (pu16)(y - d->bmy) * d->bmrow;
        for (i = 0; i < nc; i++) {
            pi16 a = cx0[i] - d->bmx, b = cx1[i] - d->bmx;
            for (j = a; j <= b; j++) row[j >> 3] |= (pu8)(0x80 >> (j & 7));
        }
        return;
    }
    for (i = 0; i < nc; i++) {
        pi16 ys = y;
        for (j = 0; j < d->nopen; j++)
            if (d->ox0[j] == cx0[i] && d->ox1[j] == cx1[i]) { ys = d->oy[j]; d->oy[j] = -32000; break; }
        if (nn < 32) { nx0[nn] = cx0[i]; nx1[nn] = cx1[i]; ny[nn] = ys; nn++; }
        else out_rect(d, col, cx0[i], ys, cx1[i], ys);
    }
    for (j = 0; j < d->nopen; j++)
        if (d->oy[j] != -32000) out_rect(d, col, d->ox0[j], d->oy[j], d->ox1[j], y - 1);
    for (i = 0; i < nn; i++) { d->ox0[i] = nx0[i]; d->ox1[i] = nx1[i]; d->oy[i] = ny[i]; }
    d->nopen = nn;
}

#define MAXX 48
static void fill_poly1(PdfDoc PFAR *d, pi16 PFAR *px, pi16 PFAR *py, pi16 PFAR *subend,
                       pi16 nsub, pu8 evenodd, pu8 col);

/* PackBits one row to out, returns bytes written */
static pu16 packbits(const pu8 PFAR *in, pu16 n, pu8 PFAR *out)
{
    pu16 i = 0, o = 0;
    while (i < n) {
        pu16 j = i;
        while (j + 1 < n && in[j + 1] == in[i] && j - i < 127) j++;
        if (j - i + 1 >= 3) {
            out[o++] = (pu8)(257 - (j - i + 1)); out[o++] = in[i]; i = j + 1;
        } else {
            pu16 st = o++, cnt = 0;
            while (i < n && cnt < 128) {
                if (i + 2 < n && in[i] == in[i + 1] && in[i] == in[i + 2]) break;
                out[o++] = in[i++]; cnt++;
            }
            out[st] = (pu8)(cnt - 1);
        }
    }
    return o;
}

/* Fill; if the rectangles take more room than a packed bitmap of the shape,
 * replace them by one DL_BITMAP record. */
static void fill_poly(PdfDoc PFAR *d, pi16 PFAR *px, pi16 PFAR *py, pi16 PFAR *subend,
                      pi16 nsub, pu8 evenodd, pu8 col)
{
    pu16 start = d->dlpos, used, rowb, h, r, o, raw, worst;
    pi16 npts = nsub ? subend[nsub - 1] : 0, i, x0 = 32767, x1 = -32767, y0 = 32767, y1 = -32767;
    pu8 PFAR *out;
    if (d->trunc) return;
    fill_poly1(d, px, py, subend, nsub, evenodd, col);
    used = d->dlpos - start;
    if (used < 40 || d->trunc) return;
    for (i = 0; i < npts; i++) {
        if (px[i] < x0) x0 = px[i];
        if (px[i] > x1) x1 = px[i];
        if (py[i] < y0) y0 = py[i];
        if (py[i] > y1) y1 = py[i];
    }
    x0 >>= 2; x1 = (x1 + 3) >> 2; y0 >>= 2; y1 = (y1 + 3) >> 2;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > d->devw) x1 = d->devw;
    if (y1 > d->devh) y1 = d->devh;
    if (x1 <= x0 || y1 <= y0) return;
    rowb = (pu16)((x1 - x0 + 7) >> 3); h = (pu16)(y1 - y0);
    if ((pu32)rowb * h > 16000UL) return;
    raw = rowb * h; worst = raw + raw / 64 + h + 16;
    if ((pu32)d->dlpos + worst + raw + 16 > d->dlsize) return;
    /* raw bits at the end of the DL buffer, packed rows after the rectangles */
    d->bm = d->dl + d->dlsize - raw; d->bmx = x0; d->bmy = y0; d->bmrow = rowb;
    p_memset(d->bm, 0, raw);
    fill_poly1(d, px, py, subend, nsub, evenodd, col);
    out = d->dl + d->dlpos + 14;
    for (r = 0, o = 0; r < h; r++) o += packbits(d->bm + r * rowb, rowb, out + o);
    d->bm = 0;
    if (o + 14 >= used) return;                           /* rectangles are smaller */
    {
        pu8 PFAR *dst = d->dl + start + 14;
        pu16 k;
        for (k = 0; k < o; k++) dst[k] = out[k];
    }
    d->dlpos = start;
    dl_b(d, DL_BITMAP); dl_b(d, col); dl_w(d, x0); dl_w(d, y0); dl_w(d, (pi16)(o + 6));
    dl_w(d, x1 - x0); dl_w(d, (pi16)h); dl_b(d, 1); dl_b(d, 0);
    d->dlpos += o;
}

static void fill_poly1(PdfDoc PFAR *d, pi16 PFAR *px, pi16 PFAR *py, pi16 PFAR *subend,
                       pi16 nsub, pu8 evenodd, pu8 col)
{
    pi16 ymin = 32767, ymax = -32767, xmin = 32767, xmax = -32767, i, s, r, r0, r1, hit = 0;
    pi16 PFAR *xs = d->xs;
    {
    pi16 npts = nsub ? subend[nsub - 1] : 0;
    if (npts < 2) return;
    for (i = 0; i < npts; i++) {
        if (py[i] < ymin) ymin = py[i];
        if (py[i] > ymax) ymax = py[i];
        if (px[i] < xmin) xmin = px[i];
        if (px[i] > xmax) xmax = px[i];
    }
    r0 = (pi16)(ymin >> 2); r1 = (pi16)((ymax + 3) >> 2);
    if (r0 < 0) r0 = 0;
    if (r1 > d->devh) r1 = d->devh;
    if (xmax < 0 || (xmin >> 2) >= d->devw) return;
    d->nopen = 0;
    for (r = r0; r < r1; r++) {
        pi16 Y = (pi16)(r * 4 + 2), nx = 0, start = 0, k;
        pi16 PFAR *cx0 = d->cx0, PFAR *cx1 = d->cx1, nc = 0;
        pi8 PFAR *dirs = d->dirs;
        pi16 wind = 0;
        i = 0;
        for (s = 0; s < nsub; s++) {
            pi16 st = i, en = subend[s], j;
            for (j = st; j < en; j++) {
                pi16 k2 = (j + 1 < en) ? j + 1 : st;
                pi16 x0 = px[j], y0 = py[j], x1 = px[k2], y1 = py[k2];
                pi8 dir;
                pi16 x, m;
                if (y0 <= Y && y1 > Y) dir = 1;
                else if (y1 <= Y && y0 > Y) dir = -1;
                else continue;
                x = (pi16)(x0 + (pi32)(Y - y0) * (x1 - x0) / (y1 - y0));
                if (nx >= MAXX) continue;
                m = nx++;
                while (m > 0 && xs[m - 1] > x) { xs[m] = xs[m - 1]; dirs[m] = dirs[m - 1]; m--; }
                xs[m] = x; dirs[m] = dir;
            }
            i = en;
        }
        for (k = 0; k < nx; k++) {
            pi16 prev = wind;
            if (evenodd) wind ^= 1; else wind += dirs[k];
            if (prev == 0 && wind != 0) start = xs[k];
            else if (prev != 0 && wind == 0 && nc < 32) {
                pi16 a, b;
                if (xs[k] <= start) continue;
                q2px(start, xs[k], &a, &b);
                if (a < 0) a = 0;
                if (b >= d->devw) b = d->devw - 1;
                if (b < a) continue;
                if (nc && a <= cx1[nc - 1] + 1) { if (b > cx1[nc - 1]) cx1[nc - 1] = b; }
                else { cx0[nc] = a; cx1[nc] = b; nc++; }
            }
        }
        if (nc) hit = 1;
        row_spans(d, col, r, cx0, cx1, nc);
    }
    open_flush(d, col, r1);
    if (!hit && !d->bm) {           /* thinner than a sample row: draw as hairline box */
        pi16 a, b, c, e;
        q2px(xmin, xmax, &a, &b); q2px(ymin, ymax, &c, &e);
        out_rect(d, col, a, c, b, e);
    }
    }
}

/* ====================================================================== */
/* paths                                                                   */
/* ====================================================================== */
static GState PFAR *GS(PdfDoc PFAR *d) { return &d->gs[d->gsp]; }

static pi16 fx2q(fx v)
{
    v = (v + 0x2000L) >> 14;
    if (v > 8000) v = 8000;
    if (v < -8000) v = -8000;
    return (pi16)v;
}

static void xform(fx PFAR *m, fx x, fx y, fx PFAR *ox, fx PFAR *oy)
{
    *ox = fx_mul(m[0], x) + fx_mul(m[2], y) + m[4];
    *oy = fx_mul(m[1], x) + fx_mul(m[3], y) + m[5];
}

static void mat_mul(fx PFAR *r, fx PFAR *m, fx PFAR *n)
{
    fx t[6];
    t[0] = fx_mul(m[0], n[0]) + fx_mul(m[1], n[2]);
    t[1] = fx_mul(m[0], n[1]) + fx_mul(m[1], n[3]);
    t[2] = fx_mul(m[2], n[0]) + fx_mul(m[3], n[2]);
    t[3] = fx_mul(m[2], n[1]) + fx_mul(m[3], n[3]);
    t[4] = fx_mul(m[4], n[0]) + fx_mul(m[5], n[2]) + n[4];
    t[5] = fx_mul(m[4], n[1]) + fx_mul(m[5], n[3]) + n[5];
    p_memcpy(r, t, sizeof(t));
}

static void path_point(PdfDoc PFAR *d, fx x, fx y)
{
    fx dx, dy;
    if (d->npts >= MAXPTS || d->nsub == 0) return;
    xform(GS(d)->ctm, x, y, &dx, &dy);
    d->px[d->npts] = fx2q(dx); d->py[d->npts] = fx2q(dy);
    d->npts++;
    d->subend[d->nsub - 1] = d->npts;
}

static void path_move(PdfDoc PFAR *d, fx x, fx y)
{
    if (d->nsub > 0 && d->subend[d->nsub - 1] - (d->nsub > 1 ? d->subend[d->nsub - 2] : 0) <= 1) {
        d->npts = d->nsub > 1 ? d->subend[d->nsub - 2] : 0;   /* drop lone moveto */
        d->nsub--;
    }
    if (d->nsub >= MAXSUB) return;
    d->subend[d->nsub] = d->npts; d->subclosed[d->nsub] = 0; d->nsub++;
    path_point(d, x, y);
    d->curx = d->startx = x; d->cury = d->starty = y;
}

static void path_line(PdfDoc PFAR *d, fx x, fx y)
{
    if (d->nsub == 0) path_move(d, d->curx, d->cury);
    path_point(d, x, y);
    d->curx = x; d->cury = y;
}

static void path_curve(PdfDoc PFAR *d, fx x1, fx y1, fx x2, fx y2, fx x3, fx y3)
{
    fx ax, ay, bx, by, cx, cy, ex, ey;
    pi32 X0, Y0, X1, Y1, X2, Y2, X3, Y3, len;
    pi16 n, i;
    if (d->nsub == 0) path_move(d, d->curx, d->cury);
    xform(GS(d)->ctm, d->curx, d->cury, &ax, &ay);
    xform(GS(d)->ctm, x1, y1, &bx, &by);
    xform(GS(d)->ctm, x2, y2, &cx, &cy);
    xform(GS(d)->ctm, x3, y3, &ex, &ey);
    X0 = fx2q(ax); Y0 = fx2q(ay); X1 = fx2q(bx); Y1 = fx2q(by);
    X2 = fx2q(cx); Y2 = fx2q(cy); X3 = fx2q(ex); Y3 = fx2q(ey);
    len = (X1 > X0 ? X1 - X0 : X0 - X1) + (Y1 > Y0 ? Y1 - Y0 : Y0 - Y1) +
          (X2 > X1 ? X2 - X1 : X1 - X2) + (Y2 > Y1 ? Y2 - Y1 : Y1 - Y2) +
          (X3 > X2 ? X3 - X2 : X2 - X3) + (Y3 > Y2 ? Y3 - Y2 : Y2 - Y3);
    n = (pi16)(len / 24);                 /* ~6 px per segment */
    if (n < 2) n = 2;
    if (n > 16) n = 16;
    for (i = 1; i <= n; i++) {
        pi32 t = i, u = n - i, n3 = (pi32)n * n * n;
        pi32 c0 = u * u * u, c1 = 3 * u * u * t, c2 = 3 * u * t * t, c3 = t * t * t;
        pi32 qx = (c0 * X0 + c1 * X1 + c2 * X2 + c3 * X3) / n3;
        pi32 qy = (c0 * Y0 + c1 * Y1 + c2 * Y2 + c3 * Y3) / n3;
        if (d->npts >= MAXPTS) break;
        d->px[d->npts] = (pi16)qx; d->py[d->npts] = (pi16)qy; d->npts++;
        d->subend[d->nsub - 1] = d->npts;
    }
    d->curx = x3; d->cury = y3;
}

static void path_close(PdfDoc PFAR *d)
{
    if (d->nsub) d->subclosed[d->nsub - 1] = 1;
    d->curx = d->startx; d->cury = d->starty;
}

static void path_rect(PdfDoc PFAR *d, fx x, fx y, fx w, fx h)
{
    path_move(d, x, y);
    path_line(d, x + w, y);
    path_line(d, x + w, y + h);
    path_line(d, x, y + h);
    path_close(d);
}

static void path_reset(PdfDoc PFAR *d) { d->npts = 0; d->nsub = 0; }

/* fast path: every subpath an axis aligned rectangle */
static pi16 fill_rects(PdfDoc PFAR *d, pu8 col)
{
    pi16 s, st = 0;
    for (s = 0; s < d->nsub; s++) {
        pi16 en = d->subend[s], n = en - st;
        pi16 PFAR *x = d->px + st, PFAR *y = d->py + st;
        if (n == 5 && x[4] == x[0] && y[4] == y[0]) n = 4;
        if (n != 4) return 0;
        if (!((x[0] == x[3] && x[1] == x[2] && y[0] == y[1] && y[2] == y[3]) ||
              (y[0] == y[3] && y[1] == y[2] && x[0] == x[1] && x[2] == x[3]))) return 0;
        st = en;
    }
    st = 0;
    for (s = 0; s < d->nsub; s++) {
        pi16 en = d->subend[s], i, xa = 32767, xb = -32767, ya = 32767, yb = -32767, a, b, c, e;
        for (i = st; i < en; i++) {
            if (d->px[i] < xa) xa = d->px[i];
            if (d->px[i] > xb) xb = d->px[i];
            if (d->py[i] < ya) ya = d->py[i];
            if (d->py[i] > yb) yb = d->py[i];
        }
        q2px(xa, xb, &a, &b); q2px(ya, yb, &c, &e);
        out_rect(d, col, a, c, b, e);
        st = en;
    }
    return 1;
}

static void do_fill(PdfDoc PFAR *d, pu8 evenodd)
{
    pu8 col = GS(d)->fillc;
    if (d->nsub == 0) return;
    if ((!evenodd || d->nsub == 1) && fill_rects(d, col)) return;
    fill_poly(d, d->px, d->py, d->subend, d->nsub, evenodd, col);
}

static fx mat_scale(fx PFAR *m)
{
    fx a = m[0] < 0 ? -m[0] : m[0], b = m[1] < 0 ? -m[1] : m[1];
    fx c = m[2] < 0 ? -m[2] : m[2], e = m[3] < 0 ? -m[3] : m[3];
    fx sx = a > b ? a + (b * 3 >> 3) : b + (a * 3 >> 3);
    fx sy = c > e ? c + (e * 3 >> 3) : e + (c * 3 >> 3);
    return (sx + sy) >> 1;
}

static void stroke_seg(PdfDoc PFAR *d, pi16 x0, pi16 y0, pi16 x1, pi16 y1, pi16 hw, pu8 col)
{
    pi16 a, b, c, e;
    if (x0 == x1 && y0 == y1) return;
    if (y0 == y1 || x0 == x1) {
        pi16 xa = x0 < x1 ? x0 : x1, xb = x0 < x1 ? x1 : x0;
        pi16 ya = y0 < y1 ? y0 : y1, yb = y0 < y1 ? y1 : y0;
        if (y0 == y1) { ya -= hw; yb += hw; } else { xa -= hw; xb += hw; }
        q2px(xa, xb, &a, &b); q2px(ya, yb, &c, &e);
        out_rect(d, col, a, c, b, e);
        return;
    }
    {
        pi32 dx = x1 - x0, dy = y1 - y0;
        pu32 len = p_isqrt((pu32)(dx * dx + dy * dy));
        pi16 nx, ny, qx[4], qy[4], se[1];
        if (len == 0) return;
        nx = (pi16)(-dy * hw / (pi32)len); ny = (pi16)(dx * hw / (pi32)len);
        if (nx == 0 && ny == 0) nx = 1;
        qx[0] = x0 + nx; qy[0] = y0 + ny; qx[1] = x1 + nx; qy[1] = y1 + ny;
        qx[2] = x1 - nx; qy[2] = y1 - ny; qx[3] = x0 - nx; qy[3] = y0 - ny;
        se[0] = 4;
        fill_poly(d, qx, qy, se, 1, 0, col);
    }
}

static void do_stroke(PdfDoc PFAR *d)
{
    GState PFAR *g = GS(d);
    fx lw = fx_mul(g->lw, mat_scale(g->ctm));
    pi16 hw = (pi16)(lw >> 15), s, st = 0;      /* half width in quarter px */
    if (hw < 2) hw = 2;
    for (s = 0; s < d->nsub; s++) {
        pi16 en = d->subend[s], i;
        for (i = st; i + 1 < en; i++) stroke_seg(d, d->px[i], d->py[i], d->px[i + 1], d->py[i + 1], hw, g->strokec);
        if (d->subclosed[s] && en - st > 2) stroke_seg(d, d->px[en - 1], d->py[en - 1], d->px[st], d->py[st], hw, g->strokec);
        st = en;
    }
}

/* ====================================================================== */
/* fonts                                                                   */
/* ====================================================================== */
static void PFAR *fp_alloc(PdfDoc PFAR *d, pu16 n)
{
    pu16 o = (d->ftop + 3) & ~3U;
    if ((pu32)o + n > FPOOL_SIZE) return 0;
    d->ftop = o + n;
    return d->fpool + o;
}

static pu8 uni_geos(pu16 u)
{
    pi16 lo, hi;
    if (u >= 32 && u < 127) return (pu8)u;
    if (u >= 0xA0 && u <= 0xFF) return U_A0_TO_GEOS[u - 0xA0];
    if (u == 0 || u < 32) return 0;
    lo = 0; hi = UX_COUNT - 1;
    while (lo <= hi) {
        pi16 m = (lo + hi) >> 1;
        if (UX_KEY[m] == u) return UX_VAL[m];
        if (UX_KEY[m] < u) lo = m + 1; else hi = m - 1;
    }
    if (u >= 0x2000 && u <= 0x200B) return ' ';
    return '?';
}

static pi16 hexn(const pu8 PFAR *s, pu16 n)
{
    pi16 v = 0; pu16 i;
    for (i = 0; i < n; i++) {
        pu8 c = s[i]; pi16 h;
        if (c >= '0' && c <= '9') h = c - '0';
        else if (c >= 'A' && c <= 'F') h = c - 'A' + 10;
        else if (c >= 'a' && c <= 'f') h = c - 'a' + 10;
        else return -1;
        v = (pi16)(v * 16 + h);
    }
    return v;
}

static pu16 glyph_uni(const pu8 PFAR *n, pu16 len)
{
    pi16 lo = 0, hi = GN_COUNT - 1;
    if (len == 7 && n[0] == 'u' && n[1] == 'n' && n[2] == 'i') { pi16 v = hexn(n + 3, 4); if (v >= 0) return (pu16)v; }
    if (len == 5 && n[0] == 'u') { pi16 v = hexn(n + 1, 4); if (v >= 0) return (pu16)v; }
    while (lo <= hi) {
        pi16 m = (lo + hi) >> 1, c = 0; pu16 i, o = GN_OFF[m];
        /* index the table directly: a pointer into it would lose its segment */
        for (i = 0; i < len; i++) {
            pu8 gc = (pu8)GN_STR[o + i];
            if (!gc) { c = 1; break; }
            if (gc != n[i]) { c = n[i] > gc ? 1 : -1; break; }
        }
        if (c == 0 && GN_STR[o + len]) c = -1;
        if (c == 0) return GN_UNI[m];
        if (c > 0) lo = m + 1; else hi = m - 1;
    }
    return 0;
}

/* ToUnicode: calls back into font tables */
static void tu_set(PdfDoc PFAR *d, Font PFAR *f, pu16 code, pu16 uni);

static pu16 tu_dst(Tok PFAR *t)
{
    pu16 u;
    if (t->len < 2) return t->len ? t->s[0] : 0;
    u = (pu16)((t->s[0] << 8) | t->s[1]);
    if (t->len == 4 && u == 'f') {
        pu16 v = (pu16)((t->s[2] << 8) | t->s[3]);
        if (v == 'i') return 0xFB01;
        if (v == 'l') return 0xFB02;
    }
    if (u >= 0xD800 && u <= 0xDFFF) return '?';
    return u;
}

static pu16 tu_code(Tok PFAR *t)
{
    if (t->len == 1) return t->s[0];
    return (pu16)((t->s[0] << 8) | t->s[1]);
}

static void load_tounicode(PdfDoc PFAR *d, Font PFAR *f, pu16 ref)
{
    pu16 mark = d->atop, sd;
    pi32 len; pu8 flate;
    Src s; Tok PFAR *t = &d->tok;
    if (ref == NIL || NODE(d, ref)->type != N_REF) return;
    sd = load_obj(d, NODE(d, ref)->val);
    if (sd == NIL || d->streamoff < 0) { d->atop = mark; return; }
    if (stream_info(d, sd, &len, &flate) != 0) { d->atop = mark; return; }
    src_stream(d, &s, d->rawbuf, d->cbuf, CBUF_SIZE, d->streamoff, len, flate);
    d->atop = mark;
    for (;;) {
        lex(&s, t);
        if (t->type == T_EOF) break;
        if (t->type != T_KW) continue;
        if (p_streq(t->s, t->len, (KS + 505))) {
            for (;;) {
                pu16 c;
                lex(&s, t); if (t->type != T_STR) break;
                c = tu_code(t);
                lex(&s, t); if (t->type != T_STR) break;
                tu_set(d, f, c, tu_dst(t));
            }
        } else if (p_streq(t->s, t->len, (KS + 517))) {
            for (;;) {
                pu16 lo, hi, u; pu32 c;
                lex(&s, t); if (t->type != T_STR) break;
                lo = tu_code(t);
                lex(&s, t); if (t->type != T_STR) break;
                hi = tu_code(t);
                lex(&s, t);
                if (t->type == T_STR) {
                    u = tu_dst(t);
                    if (f->twobyte && (pu32)hi - lo > 512) {
                        pu16 i;
                        for (i = 0; i < f->nent; i++)
                            if (f->ent[i].code >= lo && f->ent[i].code <= hi)
                                tu_set(d, f, f->ent[i].code, (pu16)(u + (f->ent[i].code - lo)));
                    } else
                        for (c = lo; c <= hi; c++) tu_set(d, f, (pu16)c, (pu16)(u + (c - lo)));
                } else if (t->type == T_ABEG) {
                    c = lo;
                    for (;;) {
                        lex(&s, t);
                        if (t->type != T_STR) break;
                        if (c <= hi) tu_set(d, f, (pu16)c, tu_dst(t));
                        c++;
                    }
                } else break;
            }
        }
    }
}

static pi16 ent_find(Font PFAR *f, pu16 code)
{
    pi16 lo = 0, hi = (pi16)f->nent - 1;
    while (lo <= hi) {
        pi16 m = (lo + hi) >> 1;
        if (f->ent[m].code == code) return m;
        if (f->ent[m].code < code) lo = m + 1; else hi = m - 1;
    }
    return -1;
}


static void ent_sort(Font PFAR *f)
{
    pi16 gap, i, j, n = (pi16)f->nent;
    for (gap = n / 2; gap > 0; gap /= 2)
        for (i = gap; i < n; i++) {
            CidEnt t = f->ent[i];
            for (j = i; j >= gap && f->ent[j - gap].code > t.code; j -= gap) f->ent[j] = f->ent[j - gap];
            f->ent[j] = t;
        }
}

static void tu_set(PdfDoc PFAR *d, Font PFAR *f, pu16 code, pu16 uni)
{
    pu8 ch = uni_geos(uni);
    (void)d;
    if (!f->twobyte) { if (code < 256 && ch) f->map[code] = ch; return; }
    {
        pi16 i = ent_find(f, code);
        if (i >= 0) { f->ent[i].ch = ch; return; }
        if (f->nent < d->entcap) {
            CidEnt PFAR *e = &f->ent[f->nent];
            pi16 j = (pi16)f->nent;
            /* insert sorted */
            while (j > 0 && f->ent[j - 1].code > code) { f->ent[j] = f->ent[j - 1]; j--; }
            e = &f->ent[j];
            e->code = code; e->w = f->dw; e->ch = ch; e->pad = 0;
            f->nent++;
        }
    }
}

static pu8 font_family(PdfDoc PFAR *d, const pu8 PFAR *n, pu16 len, pu16 desc, pu8 PFAR *style)
{
    pi32 flags = node_int(d, dict_getr(d, desc, (KS + 530)), 0);
    *style = 0;
    if (p_contains(n, len, (KS + 536)) || p_contains(n, len, (KS + 541)) || p_contains(n, len, (KS + 547)) ||
        p_contains(n, len, (KS + 553)) || p_contains(n, len, (KS + 562)) || (flags & 0x40000L)) *style |= PS_BOLD;
    if (p_contains(n, len, (KS + 567)) || p_contains(n, len, (KS + 574)) || (flags & 0x40)) *style |= PS_ITALIC;
    if (p_contains(n, len, (KS + 582)) || p_contains(n, len, (KS + 590)) || p_contains(n, len, (KS + 595)) ||
        p_contains(n, len, (KS + 602)) || p_contains(n, len, (KS + 613)) || (flags & 1)) return 2;
    if (p_contains(n, len, (KS + 618)) || p_contains(n, len, (KS + 623)) || p_contains(n, len, (KS + 629)) ||
        p_contains(n, len, (KS + 639)) || p_contains(n, len, (KS + 647)) || p_contains(n, len, (KS + 655)) ||
        p_contains(n, len, (KS + 662)) || p_contains(n, len, (KS + 671)) || p_contains(n, len, (KS + 679)) ||
        p_contains(n, len, (KS + 686)) || p_contains(n, len, (KS + 693)) || p_contains(n, len, (KS + 699))) return 1;
    if (p_contains(n, len, (KS + 709)) || p_contains(n, len, (KS + 715)) || p_contains(n, len, (KS + 721)) ||
        p_contains(n, len, (KS + 727)) || p_contains(n, len, (KS + 735)) || p_contains(n, len, (KS + 744)) ||
        p_contains(n, len, (KS + 752)) || p_contains(n, len, (KS + 757)) || p_contains(n, len, (KS + 766)) ||
        p_contains(n, len, (KS + 773)) || p_contains(n, len, (KS + 781)) || p_contains(n, len, (KS + 785)) ||
        p_contains(n, len, (KS + 797)) || p_contains(n, len, (KS + 804)) || p_contains(n, len, (KS + 811)) ||
        (flags & 2)) return 0;
    return 1;
}

static const pu16 PFAR *std_widths(const pu8 PFAR *n, pu16 len, pu8 fam, pu8 style)
{
    (void)n; (void)len;
    if (fam == 2) return 0;
    /* plain returns only: Watcom loses the table segment in ?: (uses DS) */
    if (fam == 1) {
        if (style & PS_BOLD) return W_Helvetica_Bold;
        return W_Helvetica;
    }
    if ((style & PS_BOLD) && (style & PS_ITALIC)) return W_Times_BoldItalic;
    if (style & PS_BOLD) return W_Times_Bold;
    if (style & PS_ITALIC) return W_Times_Italic;
    return W_Times_Roman;
}

static void simple_tables(PdfDoc PFAR *d, Font PFAR *f, const pu16 PFAR *enc, pu16 encobj,
                          const pu16 PFAR *stdw, pu16 fwobj, pi32 first, pu16 missing, pu8 symbol)
{
    pi16 i;
    pu16 PFAR *uni = d->scratch;
    for (i = 0; i < 256; i++) uni[i] = enc[i];
    if (encobj != NIL) {
        pu16 diff = dict_getr(d, encobj, (KS + 821)), c;
        pi16 code = 0;
        if (diff != NIL && NODE(d, diff)->type == N_ARR)
            for (c = NODE(d, diff)->child; c != NIL; c = NODE(d, c)->next) {
                Node PFAR *n = NODE(d, c);
                if (n->type == N_NUM) code = (pi16)node_int(d, c, 0);
                else if (n->type == N_NAME) {
                    if (code >= 0 && code < 256) uni[code] = glyph_uni(d->arena + n->child, n->len);
                    code++;
                }
            }
    }
    for (i = 0; i < 256; i++) {
        pu8 ch = uni_geos(uni[i]);
        if (symbol == 1) ch = (i == 0xB7) ? 0xA5 : (i >= 32 && i < 127 ? (pu8)i : (i ? '?' : 0));
        else if (symbol == 2) ch = (i > 32) ? 0xA5 : (pu8)i;
        f->map[i] = ch;
        uni[i] = missing;
        if (stdw) uni[i] = stdw[ch ? ch : ' '];
        else if (!fwobj && missing == 0) uni[i] = 600;
    }
    if (fwobj != NIL && NODE(d, fwobj)->type == N_ARR) {
        pu16 c; pi32 code = first;
        for (c = NODE(d, fwobj)->child; c != NIL; c = NODE(d, c)->next, code++)
            if (code >= 0 && code < 256) uni[code] = (pu16)node_int(d, resolve(d, c), missing);
    }
    for (i = 0; i < 256; i++) f->w4[i] = (pu8)(uni[i] > 1018 ? 255 : (uni[i] + 2) >> 2);
}

/* Type3: code -> glyph procedure (object number) via /Encoding /Differences
 * and /CharProcs; widths rescaled to 1/1000 em. */
static void load_type3(PdfDoc PFAR *d, Font PFAR *f, pu16 fd, pu16 encd)
{
    pu16 cp = dict_getr(d, fd, (KS + 833)), dif, e, i;
    pu16 fm = dict_getr(d, fd, (KS + 843));
    fx m0 = node_fx(d, resolve(d, arr_at(d, fm, 0)), 0), m1, m2;
    pu16 upm = 1000;
    pi16 code = 0;
    if (cp == NIL || NODE(d, cp)->type != N_DICT || encd == NIL) return;
    m1 = node_fx(d, resolve(d, arr_at(d, fm, 1)), 0);
    m2 = node_fx(d, resolve(d, arr_at(d, fm, 2)), 0);
    if (m0 <= 0 || m1 != 0 || m2 != 0) return;          /* only plain scaling */
    if (m0 < FXONE) {
        pu32 u = (pu32)(0xFFFFFFFFUL / (pu32)m0) >> 16;  /* about 1/m0 */
        upm = (pu16)u;
        if (u > 980 && u < 1020) upm = 1000;             /* 0.001 is not exact in 16.16 */
        else if (u > 2000 && u < 2100) upm = 2048;
    }
    dif = dict_getr(d, encd, (KS + 821));
    if (dif == NIL || NODE(d, dif)->type != N_ARR) return;
    f->t3 = (pu16 PFAR *)fp_alloc(d, 512);
    if (!f->t3) return;
    for (i = 0; i < 256; i++) f->t3[i] = 0;
    f->t3upm = upm;
    for (e = NODE(d, dif)->child; e != NIL; e = NODE(d, e)->next) {
        Node PFAR *en = NODE(d, e);
        if (en->type == N_NUM) { code = (pi16)en->val; if (!en->isint) code = (pi16)(en->val >> 16); continue; }
        if (en->type != N_NAME) continue;
        if (code >= 0 && code < 256) {
            pu16 k;
            for (k = NODE(d, cp)->child; k != NIL; ) {
                Node PFAR *kn = NODE(d, k);
                pu16 v = kn->next;
                if (v == NIL) break;
                if (kn->type == N_NAME && kn->len == en->len) {
                    pu16 j;
                    for (j = 0; j < en->len && d->arena[kn->child + j] == d->arena[en->child + j]; j++) ;
                    if (j == en->len) {
                        if (NODE(d, v)->type == N_REF && NODE(d, v)->val > 0 && NODE(d, v)->val < 65536L)
                            f->t3[code] = (pu16)NODE(d, v)->val;
                        break;
                    }
                }
                k = NODE(d, v)->next;
            }
        }
        code++;
    }
    if (upm != 1000 && f->w4)
        for (i = 0; i < 256; i++) f->w4[i] = (pu8)(((pu32)f->w4[i] * 1000U) / upm);
}

static void load_font(PdfDoc PFAR *d, const pu8 PFAR *res, pu16 reslen, pu16 fd)
{
    Font PFAR *f;
    pu16 sub, base, desc, tu;
    pu8 fam, style, symbol = 0;
    const pu8 PFAR *bn = (const pu8 PFAR *)(KS + 854); pu16 bl = 0;
    if (d->nfonts >= MAXFONTS || fd == NIL || NODE(d, fd)->type != N_DICT) return;
    f = &d->fonts[d->nfonts];
    p_memset(f, 0, sizeof(Font));
    if (reslen > sizeof(f->res) - 1) reslen = sizeof(f->res) - 1;
    p_memcpy(f->res, res, reslen);
    sub = dict_getr(d, fd, (KS + 855));
    base = dict_getr(d, fd, (KS + 863));
    if (base != NIL && NODE(d, base)->type == N_NAME) { bn = d->arena + NODE(d, base)->child; bl = NODE(d, base)->len; }
    if (bl > 7 && bn[6] == '+') { bn += 7; bl -= 7; }
    desc = dict_getr(d, fd, (KS + 872));
    if (node_is(d, sub, N_NAME, (KS + 887))) {
        pu16 df = dict_getr(d, fd, (KS + 893)), w;
        pu16 room;
        df = resolve(d, arr_at(d, df, 0));
        desc = dict_getr(d, df, (KS + 872));
        fam = font_family(d, bn, bl, desc, &style);
        f->twobyte = 1;
        f->dw = (pu16)node_int(d, dict_getr(d, df, (KS + 909)), 1000);
        room = (pu16)((FPOOL_SIZE - d->ftop) / sizeof(CidEnt));
        if (room > 1200) room = 1200;
        if (room < 16) goto done;
        f->ent = (CidEnt PFAR *)fp_alloc(d, (pu16)(room * sizeof(CidEnt)));
        d->entcap = room;
        w = dict_getr(d, df, (KS + 126));
        if (w != NIL && NODE(d, w)->type == N_ARR) {
            pu16 c = NODE(d, w)->child;
            while (c != NIL) {
                pi32 c0 = node_int(d, resolve(d, c), -1);
                pu16 nx = NODE(d, c)->next, ar;
                if (nx == NIL) break;
                ar = resolve(d, nx);
                if (ar != NIL && NODE(d, ar)->type == N_ARR) {
                    pu16 e;
                    for (e = NODE(d, ar)->child; e != NIL && f->nent < room; e = NODE(d, e)->next, c0++) {
                        CidEnt PFAR *ce = &f->ent[f->nent++];
                        ce->code = (pu16)c0; ce->w = (pu16)node_int(d, resolve(d, e), f->dw); ce->ch = '?';
                    }
                    c = NODE(d, nx)->next;
                } else {
                    pi32 c1 = node_int(d, ar, -1), cc;
                    pu16 wn = NODE(d, nx)->next;
                    pu16 wv;
                    if (wn == NIL) break;
                    wv = (pu16)node_int(d, resolve(d, wn), f->dw);
                    if (c1 - c0 <= 256)
                        for (cc = c0; cc <= c1 && f->nent < room; cc++) {
                            CidEnt PFAR *ce = &f->ent[f->nent++];
                            ce->code = (pu16)cc; ce->w = wv; ce->ch = '?';
                        }
                    c = NODE(d, wn)->next;
                }
            }
        }
        ent_sort(f);
        tu = dict_get(d, fd, (KS + 912));
        load_tounicode(d, f, tu);
        /* give back unused entries */
        d->ftop = (pu16)(((pu8 PFAR *)(f->ent + f->nent)) - d->fpool);
    } else {
        pu16 enc = dict_getr(d, fd, (KS + 922)), encd = NIL;
        const pu16 PFAR *base_enc;
        pu16 missing;
        pi32 first = node_int(d, dict_getr(d, fd, (KS + 931)), 0);
        pu16 widths = dict_getr(d, fd, (KS + 941));
        fam = font_family(d, bn, bl, desc, &style);
        if (p_contains(bn, bl, (KS + 948))) symbol = 1;
        if (p_contains(bn, bl, (KS + 955)) || p_contains(bn, bl, (KS + 963))) symbol = 2;
        if (node_is(d, sub, N_NAME, (KS + 972))) base_enc = ENC_WIN;
        else base_enc = ENC_STD;
        if (enc != NIL && NODE(d, enc)->type == N_DICT) { encd = enc; enc = dict_getr(d, encd, (KS + 981)); }
        if (node_is(d, enc, N_NAME, (KS + 994))) base_enc = ENC_WIN;
        else if (node_is(d, enc, N_NAME, (KS + 1010))) base_enc = ENC_MAC;
        else if (node_is(d, enc, N_NAME, (KS + 1027))) base_enc = ENC_STD;
        f->map = (pu8 PFAR *)fp_alloc(d, 256);
        f->w4 = (pu8 PFAR *)fp_alloc(d, 256);
        if (!f->map || !f->w4) { f->map = 0; f->w4 = 0; goto done; }
        missing = (pu16)node_int(d, dict_getr(d, desc, (KS + 1044)), widths != NIL ? 0 : 500);
        if (widths == NIL && fam == 2) missing = 600;
        simple_tables(d, f, base_enc, encd,
                      widths == NIL ? std_widths(bn, bl, fam, style) : 0,
                      widths, first, missing, symbol);
        if (!symbol) load_tounicode(d, f, dict_get(d, fd, (KS + 912)));
        f->dw = 500;
        if (node_is(d, sub, N_NAME, (KS + 1057))) load_type3(d, f, fd, encd);
    }
done:
    f->gfont = fam == 0 ? PF_ROMAN : fam == 1 ? PF_SANS : PF_MONO;
    f->style = style;
    d->nfonts++;
}

static pu16 name_hash(const pu8 PFAR *s, pu16 n)
{
    pu16 h = 0x5A17, i;
    for (i = 0; i < n; i++) h = (pu16)((h << 5) + (h >> 11) + s[i] + n);
    return h;
}

static void load_res(PdfDoc PFAR *d, pu16 res);

static void load_page_fonts(PdfDoc PFAR *d)
{
    pu16 mark = d->atop, o;
    d->nfonts = 0; d->ftop = 0; d->nxo = 0;
    if (d->resobj < 0) return;
    o = load_obj(d, d->resobj);
    load_res(d, dict_getr(d, o, (KS + 230)));
    d->atop = mark;
}

/* XObject names (replacing the table) and fonts (appended) of a resource dict */
static void load_res(PdfDoc PFAR *d, pu16 res)
{
    pu16 mark = d->atop, fonts, k;
    if (res == NIL) return;
    d->nxo = 0;
    {
        pu16 xo = dict_getr(d, res, (KS + 1063));
        if (xo != NIL && NODE(d, xo)->type == N_DICT)
            for (k = NODE(d, xo)->child; k != NIL && d->nxo < MAXXO; ) {
                Node PFAR *kn = NODE(d, k);
                pu16 v = kn->next, m2 = d->atop, i;
                if (v == NIL) break;
                if (kn->type == N_NAME) {
                    pu16 st = dict_getr(d, resolve(d, v), (KS + 855));
                    (void)i;
                    d->xohash[d->nxo] = name_hash(d->arena + kn->child, kn->len);
                    d->xoimg[d->nxo] = (pu8)(node_is(d, st, N_NAME, (KS + 1071)) ? 1 : node_is(d, st, N_NAME, (KS + 1077)) ? 2 : 0);
                    d->xoobj[d->nxo] = (NODE(d, v)->type == N_REF && NODE(d, v)->val < 65536L) ? (pu16)NODE(d, v)->val : 0;
                    d->nxo++;
                }
                d->atop = m2;
                k = NODE(d, v)->next;
            }
    }
    fonts = dict_getr(d, res, (KS + 1082));
    if (fonts == NIL || NODE(d, fonts)->type != N_DICT) { d->atop = mark; return; }
    for (k = NODE(d, fonts)->child; k != NIL; ) {
        Node PFAR *kn = NODE(d, k);
        pu16 v = kn->next, m2 = d->atop;
        if (v == NIL) break;
        if (kn->type == N_NAME) load_font(d, d->arena + kn->child, kn->len, resolve(d, v));
        d->atop = m2;
        k = NODE(d, v)->next;
    }
    d->atop = mark;
}

/* ====================================================================== */
/* text                                                                    */
/* ====================================================================== */
static void run_flush(PdfDoc PFAR *d)
{
    if (d->nrun == 0) return;
    if (dl_room(d, (pu16)(14 + d->nrun))) {
        pu8 i;
        dl_b(d, DL_TEXT); dl_b(d, d->runcol);
        dl_w(d, (pi16)d->runfont); dl_w(d, (pi16)d->runsize); dl_b(d, d->runstyle);
        dl_w(d, d->runx); dl_w(d, d->runy); dl_b(d, d->nrun);
        for (i = 0; i < d->nrun; i++) dl_b(d, d->run[i]);
    }
    d->nrun = 0;
}


static void show_glyph(PdfDoc PFAR *d, pu8 ch, pu16 w, pu8 isspace, fx PFAR *trm)
{
    GState PFAR *g = GS(d);
    fx ox, oy, size, adv, tx;
    pu16 fontid = PF_SANS; pu8 style = 0, col = g->fillc;
    pu16 qsize;
    if (g->font >= 0) { fontid = d->fonts[g->font].gfont; style = d->fonts[g->font].style; }
    if (g->tr == 1) col = g->strokec;
    xform(trm, 0, g->ts, &ox, &oy);
    size = fx_mul(g->fs, mat_scale(trm));
    qsize = (pu16)(size >> 14);
    if (isspace || ch == 0) run_flush(d);
    else if (g->tr != 7 && qsize >= 4 && ox >= -FX(4) && ox < FX(d->devw) &&
             oy >= 0 && oy - size < FX(d->devh)) {
        /* glyphs starting outside the page are clipped like in other viewers */
        pi16 px = (pi16)((ox + 0x8000L) >> 16);
        pi16 top = (pi16)((oy - ((size * TEXT_ASCENT) >> 8) + 0x8000L) >> 16);
        if (d->nrun) {
            fx dx = ox - d->lastx, dy = oy - d->lasty;
            fx tol = size >> 3;
            if (dx < 0) dx = -dx;
            if (dy < 0) dy = -dy;
            if (dx > tol || dy > (size >> 4) || d->runrot || d->runfont != fontid || d->runstyle != style ||
                d->runcol != col || d->runsize != qsize || d->nrun >= RUNMAX) run_flush(d);
        }
        if (d->nrun == 0) {
            fx ta = trm[0] < 0 ? -trm[0] : trm[0], tb = trm[1] < 0 ? -trm[1] : trm[1];
            d->runrot = (pu8)(tb > (ta >> 2));      /* baseline not horizontal */
            d->runx = px; d->runy = top; d->runfont = fontid; d->runstyle = style;
            d->runcol = col; d->runsize = qsize;
        }
        d->run[d->nrun++] = ch;
    } else run_flush(d);
    /* advance text matrix */
    adv = fx_mul(fx_div(FX(w), FX(1000)), g->fs) + g->tc + (isspace ? g->tw : 0);
    tx = fx_mul(adv, g->th);
    d->tm[4] += fx_mul(tx, d->tm[0]);
    d->tm[5] += fx_mul(tx, d->tm[1]);
    if (!isspace && ch) {
        fx ax, ay, t2[6];
        mat_mul(t2, d->tm, GS(d)->ctm);
        xform(t2, 0, g->ts, &ax, &ay);
        d->lastx = ax; d->lasty = ay;
    }
}

/* Draw Type3 glyphs as outlines when they are large enough or not upright;
 * small upright ones stay GEOS text (keeps the display list small). */
static pi16 t3_wanted(PdfDoc PFAR *d, fx PFAR *trm)
{
    GState PFAR *g = GS(d);
    fx ta = trm[0] < 0 ? -trm[0] : trm[0], tb = trm[1] < 0 ? -trm[1] : trm[1];
    fx size = fx_mul(g->fs, mat_scale(trm));
    if (tb > (ta >> 2)) return 1;
    return size >= FX(14);
}

/* Run a Type3 glyph procedure as a path in the text position. Only plain
 * (unfiltered) procedures with path operators; returns 0 if not drawn. */
static pi16 draw_t3(PdfDoc PFAR *d, Font PFAR *f, pu16 obj, fx PFAR *trm)
{
    GState PFAR *g = GS(d);
    pu16 mark = d->atop, sd;
    pi32 len, off;
    pu8 flags;
    fx gm[6], v[6];
    pi16 nv = 0, t, drawn = 0;
    Src s;
    if (g->tr == 3 || g->tr == 7 || d->gsp + 1 >= MAXGS) return 0;
    sd = load_obj(d, obj);
    off = d->streamoff;
    if (sd == NIL || off < 0 || stream_info(d, sd, &len, &flags) != 0 || flags) { d->atop = mark; return 0; }
    d->atop = mark;
    /* glyph space -> device: [fs*th/upm 0 0 fs/upm 0 rise] x trm */
    gm[0] = fx_div(fx_mul(g->fs, g->th), FX(f->t3upm)); gm[1] = 0; gm[2] = 0;
    gm[3] = fx_div(g->fs, FX(f->t3upm)); gm[4] = 0; gm[5] = g->ts;
    d->gs[d->gsp + 1] = *g; d->gsp++;
    mat_mul(GS(d)->ctm, gm, trm);
    path_reset(d);
    src_file(d, &s, d->gbuf, GBUF_SIZE, off, off + len);
    while ((t = lex(&s, &d->tok)) != T_EOF) {
        Tok PFAR *k = &d->tok;
        if (t == T_NUM) {
            if (nv == 6) { pi16 i; for (i = 1; i < 6; i++) v[i - 1] = v[i]; nv--; }
            v[nv++] = k->num;
            continue;
        }
        if (t != T_KW) { nv = 0; continue; }
#define A(i) (nv > (i) ? v[nv - 1 - (i)] : 0)
        if (k->len == 1) switch (k->s[0]) {
            case 'm': path_move(d, A(1), A(0)); break;
            case 'l': path_line(d, A(1), A(0)); break;
            case 'c': path_curve(d, A(5), A(4), A(3), A(2), A(1), A(0)); break;
            case 'v': path_curve(d, d->curx, d->cury, A(3), A(2), A(1), A(0)); break;
            case 'y': path_curve(d, A(3), A(2), A(1), A(0), A(1), A(0)); break;
            case 'h': path_close(d); break;
            case 'f': case 'F': do_fill(d, 0); path_reset(d); drawn = 1; break;
            case 'B': do_fill(d, 0); do_stroke(d); path_reset(d); drawn = 1; break;
            case 'b': path_close(d); do_fill(d, 0); do_stroke(d); path_reset(d); drawn = 1; break;
            case 'S': do_stroke(d); path_reset(d); drawn = 1; break;
            case 's': path_close(d); do_stroke(d); path_reset(d); drawn = 1; break;
            case 'n': path_reset(d); break;
            case 'w': GS(d)->lw = A(0); break;
            default: break;
        } else if (k->len == 2) {
            if (k->s[0] == 'r' && k->s[1] == 'e') path_rect(d, A(3), A(2), A(1), A(0));
            else if (k->s[0] == 'f' && k->s[1] == '*') { do_fill(d, 1); path_reset(d); drawn = 1; }
            else if (k->s[0] == 'c' && k->s[1] == 'm') {
                fx m[6];
                m[0] = A(5); m[1] = A(4); m[2] = A(3); m[3] = A(2); m[4] = A(1); m[5] = A(0);
                mat_mul(GS(d)->ctm, m, GS(d)->ctm);
            } else if (k->s[0] == 'B' && k->s[1] == 'I') break;   /* bitmap glyphs: not here */
        }
#undef A
        nv = 0;
    }
    path_reset(d);
    d->gsp--;
    d->atop = mark;
    return drawn;
}

static void show_string(PdfDoc PFAR *d, const pu8 PFAR *s, pu16 len)
{
    GState PFAR *g = GS(d);
    Font PFAR *f = g->font >= 0 ? &d->fonts[g->font] : 0;
    pu16 i = 0;
    while (i < len) {
        pu8 ch; pu16 w, code; pu8 sp = 0;
        fx trm[6];
        mat_mul(trm, d->tm, g->ctm);
        if (f && f->twobyte) {
            pi16 e;
            code = (pu16)(s[i] << 8);
            if (i + 1 < len) code |= s[i + 1];
            i += 2;
            e = ent_find(f, code);
            if (e >= 0) { ch = f->ent[e].ch; w = f->ent[e].w; }
            else { ch = '?'; w = f->dw; }
            if (ch == ' ') sp = 1;
        } else {
            code = s[i++];
            if (f && f->map) { ch = f->map[code]; w = (pu16)(f->w4[code] << 2); }
            else {
                ch = uni_geos(ENC_WIN[code]);
                if (f && f->gfont == PF_MONO) w = 600;
                else if (f && f->gfont == PF_ROMAN) w = W_Times_Roman[ch ? ch : ' '];
                else w = W_Helvetica[ch ? ch : ' '];
            }
            if (code == 32) sp = 1;
            if (ch == ' ') sp = sp ? sp : 2;
            if (f && f->t3 && f->t3[code] && t3_wanted(d, trm)) {
                run_flush(d);
                if (draw_t3(d, f, f->t3[code], trm)) { show_glyph(d, 0, w, sp == 1, trm); continue; }
            }
        }
        if (sp == 2) {           /* a space glyph without word spacing */
            run_flush(d);
            show_glyph(d, 0, w, 0, trm);
            continue;
        }
        show_glyph(d, ch, w, sp, trm);
    }
}

/* ====================================================================== */
/* content stream                                                          */
/* ====================================================================== */
static pi16 next_content(PdfDoc PFAR *d)
{
    while (d->curcont < d->ncont) {
        pu16 mark = d->atop, sd;
        pi32 len; pu8 flate;
        d->incontent = 0;
        sd = load_obj(d, d->cont[d->curcont++]);
        if (sd == NIL || d->streamoff < 0) { d->atop = mark; continue; }
        if (stream_info(d, sd, &len, &flate) != 0) { d->atop = mark; continue; }
        src_stream(d, &d->cs, d->rawbuf, d->cbuf, CBUF_SIZE, d->streamoff, len, flate);
        d->incontent = 1;
        d->atop = mark;
        return 1;
    }
    d->incontent = 0;
    return 0;
}

static pi16 clex(PdfDoc PFAR *d)
{
    for (;;) {
        pi16 t = lex(&d->cs, &d->tok);
        if (t != T_EOF) return t;
        if (!next_content(d)) return T_EOF;
    }
}

static fx opn(PdfDoc PFAR *d, pi16 i)            /* operand i of n (from bottom) */
{
    if (i < 0 || i >= d->nops || d->ops[i].type != T_NUM) return 0;
    return d->ops[i].num;
}

/* colour byte for r,g,b: a palette colour, or with true colour an entry
 * of the colour table after the display list */
static pu8 dev_color(PdfDoc PFAR *d, pi16 r, pi16 g, pi16 b)
{
    pu8 i;
    pu8 PFAR *p;
    if (!d->tc) return rgb_index(r, g, b);
    for (i = 0; i < 16; i++)
        if (PAL[i][0] == r && PAL[i][1] == g && PAL[i][2] == b) return i;
    for (i = 0, p = d->ctab; i < d->nct; i++, p += 4)
        if (p[0] == r && p[1] == g && p[2] == b) return (pu8)(16 + i);
    if (d->nct >= 240) return rgb_index(r, g, b);
    p[0] = (pu8)r; p[1] = (pu8)g; p[2] = (pu8)b; p[3] = rgb_index(r, g, b);
    return (pu8)(16 + d->nct++);
}

static void set_color(PdfDoc PFAR *d, pu8 stroke, pi16 n)
{
    pi16 b = d->nops - n;
    pu8 c;
    pi16 i, nn = 0;
    for (i = d->nops - 1; i >= 0 && d->ops[i].type == T_NUM && nn < 4; i--) nn++;
    if (n == 0) n = nn;                   /* sc / scn: count numbers */
    b = d->nops - n;
    if (b < 0) return;
    if (n == 1) { pi16 v = fx255(opn(d, b)); c = dev_color(d, v, v, v); }
    else if (n == 3) c = dev_color(d, fx255(opn(d, b)), fx255(opn(d, b + 1)), fx255(opn(d, b + 2)));
    else if (n == 4) {
        fx k = opn(d, b + 3);
        c = dev_color(d, fx255(FXONE - opn(d, b) - k), fx255(FXONE - opn(d, b + 1) - k), fx255(FXONE - opn(d, b + 2) - k));
    } else return;
    if (stroke) GS(d)->strokec = c; else GS(d)->fillc = c;
}

static void image_box(PdfDoc PFAR *d)
{
    fx x[4], y[4];
    pi16 i, xa = 32767, xb = -32767, ya = 32767, yb = -32767, a, b, c, e;
    xform(GS(d)->ctm, 0, 0, &x[0], &y[0]);
    xform(GS(d)->ctm, FXONE, 0, &x[1], &y[1]);
    xform(GS(d)->ctm, 0, FXONE, &x[2], &y[2]);
    xform(GS(d)->ctm, FXONE, FXONE, &x[3], &y[3]);
    for (i = 0; i < 4; i++) {
        pi16 qx = fx2q(x[i]), qy = fx2q(y[i]);
        if (qx < xa) xa = qx;
        if (qx > xb) xb = qx;
        if (qy < ya) ya = qy;
        if (qy > yb) yb = qy;
    }
    q2px(xa, xb, &a, &b); q2px(ya, yb, &c, &e);
    out_rect(d, 7, a, c, b, e);
    out_rect(d, 8, a, c, b, c);
    out_rect(d, 8, a, e, b, e);
    out_rect(d, 8, a, c, a, e);
    out_rect(d, 8, b, c, b, e);
}

/* ====================================================================== */
/* images: decoded to 1 bit (dithered), drawn as DL_BITMAP records         */
/* ====================================================================== */
typedef struct ImgInfo {
    pu16 w, h;
    pu8 bpc, nc, mask, inv, pred, cs;      /* cs: 0 gray 1 rgb 2 cmyk 3 indexed 4 inverted gray */
    pu8 hex;                               /* inline ASCIIHex data */
    pu16 lutlen;                           /* indexed: bytes in d->ilut source */
    pu8 base;                              /* indexed: base cs (0 gray 1 rgb 2 cmyk) */
    pu8 color, dct, nck, mkind;            /* mkind: 1 SMask, 2 stencil /Mask */
    pu16 ck[8];                            /* /Mask colour key ranges */
    pu16 mobj;                             /* mask object */
    pi32 soff, slen; pu8 sflags;           /* XObject stream */
} ImgInfo;

static const pu8 TABSEG BAYER[16] = { 8, 136, 40, 168, 200, 72, 232, 104, 56, 184, 24, 152, 248, 120, 216, 88 };

/* byte grid: 0..254 gray or 6x6x6 colour cube index, 255 = transparent */
#define TABSZ 1088                         /* R G B gray tables + 64 entry palette map */

static pi16 img_hexval(pi16 c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static pi16 img_byte(Src PFAR *s, pu8 hex)
{
    pi16 c, h1, h2;
    if (!hex) return src_get(s);
    do { c = src_get(s); if (c < 0 || c == '>') return -1; h1 = img_hexval(c); } while (h1 < 0);
    do { c = src_get(s); if (c < 0) return -1; if (c == '>') return h1 << 4; h2 = img_hexval(c); } while (h2 < 0);
    return (h1 << 4) | h2;
}

static pu8 to_gray(pu8 cs, pu8 PFAR *v)
{
    pi16 t;
    switch (cs) {
    case 1: return (pu8)((v[0] * 77U + v[1] * 150U + v[2] * 29U) >> 8);
    case 2: t = 255 - (pi16)((v[0] * 77U + v[1] * 150U + v[2] * 29U) >> 8) - v[3];
            return (pu8)(t < 0 ? 0 : t);
    case 4: return (pu8)(255 - v[0]);
    default: return v[0];
    }
}

#define SDITH(y, x) ((pi16)(((pi16)BAYER[(((y) & 3) << 2) | ((x) & 3)] - 128) / 5))

/* row offset iy * w, multiplied only when iy changes (locals liy, gro) */
#define ROWOFF(iy, w) ((iy) == liy ? gro : (liy = (iy), gro = (iy) * (w)))

static pi16 clamp255(pi16 v) { return v < 0 ? 0 : v > 255 ? 255 : v; }

/* x faded towards white by w/256 (soft mask partly transparent) */
#define TRANSP 0x0001U          /* transparent sample of the true colour grid */

static pi16 fade(pi16 x, pu8 w) { return x + (pi16)(((pu16)(255 - x) * w) >> 8); }

/* t: ordered dither at sample level (+-25) against banding of the 6 levels */
static pu8 cube(pi16 r, pi16 g, pi16 b, pi16 t)
{
    r = clamp255(r + t); g = clamp255(g + t); b = clamp255(b + t);
    return (pu8)(((r * 5 + 128) >> 8) * 36 + ((g * 5 + 128) >> 8) * 6 + ((b * 5 + 128) >> 8));
}

/* r,g,b of colour components v (0..255) in colour space cs, faded by w */
static void img_rgb(pu8 cs, pu8 color, pu8 inv, pu8 PFAR *v, pu8 w, pi16 PFAR *rgb)
{
    pi16 r, gg, b;
    if (color) {
        r = v[0]; gg = v[1]; b = v[2];
        if (cs == 2) { r = clamp255(255 - r - v[3]); gg = clamp255(255 - gg - v[3]); b = clamp255(255 - b - v[3]); }
    } else r = gg = b = to_gray(cs, v);
    if (inv) { r = 255 - r; gg = 255 - gg; b = 255 - b; }
    rgb[0] = fade(r, w); rgb[1] = fade(gg, w); rgb[2] = fade(b, w);
}

/* RGB565 sample of the true colour grid; 0xFFFF is transparent */
static pu16 pack565(pi16 r, pi16 g, pi16 b)
{
    pu16 v = (pu16)(((pu16)(r & 0xF8) << 8) | ((pu16)(g & 0xFC) << 3) | ((pu16)b >> 3));
    return v == TRANSP ? 0 : v;
}

/* grid value of colour components v (0..255) in colour space cs */
static pu8 img_value(pu8 cs, pu8 color, pu8 inv, pu8 PFAR *v, pu8 w, pi16 t)
{
    pi16 rgb[3];
    img_rgb(cs, color, inv, v, w, rgb);
    if (color) return cube(rgb[0], rgb[1], rgb[2], t);
    return (pu8)(rgb[0] >= 255 ? 254 : rgb[0]);
}

/* output tables for the grid values and the 4 level per channel palette map */
static void img_tabs(pu8 PFAR *tab, pu8 color)
{
    pu16 v, k;
    for (v = 0; v < 256; v++) {
        pu16 r = v, g = v, b = v;
        if (color && v < 216) { r = v / 36 * 51; g = v / 6 % 6 * 51; b = v % 6 * 51; }
        tab[v] = (pu8)r; tab[256 + v] = (pu8)g; tab[512 + v] = (pu8)b;
        tab[768 + v] = (pu8)((r * 77U + g * 150U + b * 29U) >> 8);
    }
    for (k = 0; k < 64; k++) {
        pi16 r = (pi16)(k >> 4) * 85, g = (pi16)((k >> 2) & 3) * 85, b = (pi16)(k & 3) * 85, i;
        pu32 bd = 0xFFFFFFFFUL;
        for (i = 0; i < 16; i++) {
            pi32 dr = r - PAL[i][0], dg = g - PAL[i][1], db = b - PAL[i][2];
            pu32 dd = (pu32)(dr * dr * 3 + dg * dg * 4 + db * db * 2);
            if (dd < bd) { bd = dd; tab[1024 + k] = (pu8)i; }
        }
    }
}

static void bm_header(PdfDoc PFAR *d, pu8 col, pi16 bx0, pi16 by0, pi16 w, pi16 h, pu16 o, pu8 type)
{
    pu8 PFAR *hdr = d->dl + d->dlpos;
    hdr[0] = DL_BITMAP; hdr[1] = col;
    hdr[2] = (pu8)bx0; hdr[3] = (pu8)((pu16)bx0 >> 8); hdr[4] = (pu8)by0; hdr[5] = (pu8)((pu16)by0 >> 8);
    hdr[6] = (pu8)(o + 6); hdr[7] = (pu8)((o + 6) >> 8);
    hdr[8] = (pu8)w; hdr[9] = (pu8)((pu16)w >> 8);
    hdr[10] = (pu8)h; hdr[11] = (pu8)((pu16)h >> 8); hdr[12] = 1; hdr[13] = type;
    d->dlpos += 14 + o;
}

/* one packed mono pass: bits where the pixel is "black" (want 1) or "white"
 * (want 0). gt == 0: bit grid; else byte grid with gray table gt. */
static pi16 img_pass(PdfDoc PFAR *d, pu8 col, pu8 want, pi16 bx0, pi16 by0, pi16 bx1, pi16 by1,
                     fx sx0, fx sy0, fx sxdx, fx sydx, fx sxdy, fx sydy,
                     pu8 PFAR *bits, pu16 rb1, pu16 w1, pu16 h1, pu8 PFAR *rowtmp, pu8 PFAR *limit,
                     pu8 PFAR *gt)
{
    pu16 rowb = (pu16)((bx1 - bx0 + 7) >> 3), o = 0, liy = 0xFFFFU, gro = 0;
    pu8 PFAR *out = d->dl + d->dlpos + 14;
    pi16 y, x, any = 0;
    for (y = by0; y < by1; y++) {
        fx sx = sx0, sy = sy0;
        p_memset(rowtmp, 0, rowb);
        for (x = 0; x < bx1 - bx0; x++) {
            if (sx >= 0 && sy >= 0) {
                pu16 ix = (pu16)(sx >> 16), iy = (pu16)(sy >> 16);
                if (ix < w1 && iy < h1) {
                    pu8 b;
                    if (gt) {
                        pu8 v = bits[ROWOFF(iy, w1) + ix];
                        b = (pu8)(v == 255 ? 2 : gt[v] < BAYER[((y & 3) << 2) | (x & 3)]);
                    } else b = (pu8)((bits[ROWOFF(iy, rb1) + (ix >> 3)] >> (7 - (ix & 7))) & 1);
                    if (b == want) { rowtmp[x >> 3] |= (pu8)(0x80 >> (x & 7)); any = 1; }
                }
            }
            sx += sxdx; sy += sydx;
        }
        if (out + o + rowb + rowb / 64 + 2 > limit) return -1;
        o += packbits(rowtmp, rowb, out + o);
        sx0 += sxdy; sy0 += sydy;
    }
    if (any) bm_header(d, col, bx0, by0, bx1 - bx0, by1 - by0, o, 0);
    return 1;
}

/* 16 colour pass (ordered dither to the EGA palette), with a mask if msk.
 * dbl: half resolution, the viewer draws every pixel 2x2 (colour byte 2). */
static pi16 img_pass4(PdfDoc PFAR *d, pi16 bx0, pi16 by0, pi16 bx1, pi16 by1,
                      fx sx0, fx sy0, fx sxdx, fx sydx, fx sxdy, fx sydy,
                      pu8 PFAR *grid, pu16 w1, pu16 h1, pu8 PFAR *rowtmp, pu8 PFAR *limit,
                      pu8 PFAR *tab, pu8 msk, pu8 sc)
{
    pu16 w = (pu16)(bx1 - bx0), hh = (pu16)(by1 - by0), mb, rl, o = 0, liy = 0xFFFFU, gro = 0;
    pu8 PFAR *out = d->dl + d->dlpos + 14;
    pi16 y, x, off[16];
    if (sc > 1) {                                   /* every pixel sc x sc */
        w = (w + sc - 1) / sc; hh = (hh + sc - 1) / sc;
        if ((pu32)w * sc > 1600) return -1;         /* viewer row buffer */
        sx0 += (sxdx + sxdy) * (sc - 1) / 2; sy0 += (sydx + sydy) * (sc - 1) / 2;
        sxdx *= sc; sydx *= sc; sxdy *= sc; sydy *= sc;
    }
    if (w > 1600) return -1;                        /* viewer row buffer */
    mb = msk ? (w + 7) >> 3 : 0; rl = mb + ((w + 1) >> 1);
    for (x = 0; x < 16; x++) off[x] = (pi16)(((pi16)BAYER[x] - 128) * 85 / 256);
    for (y = 0; y < (pi16)hh; y++) {
        fx sx = sx0, sy = sy0;
        pi16 ob = (pi16)((y & 3) << 2);
        p_memset(rowtmp, 0, rl);
        for (x = 0; x < (pi16)w; x++) {
            pi16 ix = (pi16)(sx >> 16), iy = (pi16)(sy >> 16);
            pu8 v = 255;
            if (sx >= 0 && sy >= 0 && (pu16)ix < w1 && (pu16)iy < h1) v = grid[ROWOFF((pu16)iy, w1) + (pu16)ix];
            else if (!msk) {
                if (sx < 0) ix = 0;
                if (sy < 0) iy = 0;
                if ((pu16)ix >= w1) ix = (pi16)w1 - 1;
                if ((pu16)iy >= h1) iy = (pi16)h1 - 1;
                v = grid[ROWOFF((pu16)iy, w1) + (pu16)ix];
            }
            if (v != 255) {
                pi16 t = off[ob + (x & 3)];
                pu8 c = tab[1024 + (((clamp255(tab[v] + t) * 3 + 127) >> 8) << 4)
                                 + (((clamp255(tab[256 + v] + t) * 3 + 127) >> 8) << 2)
                                 + ((clamp255(tab[512 + v] + t) * 3 + 127) >> 8)];
                if (msk) rowtmp[x >> 3] |= (pu8)(0x80 >> (x & 7));
                rowtmp[mb + (x >> 1)] |= (pu8)((x & 1) ? c : c << 4);
            }
            sx += sxdx; sy += sydx;
        }
        if (out + o + rl + rl / 64 + 2 > limit) return -1;
        o += packbits(rowtmp, rl, out + o);
        sx0 += sxdy; sy0 += sydy;
    }
    bm_header(d, (pu8)(sc > 1 ? sc : 0), bx0, by0, (pi16)w, (pi16)hh, o, (pu8)(msk ? 0x11 : 0x01));
    return 1;
}

/* true colour pass: DL_RGBIMG with every pixel scale x scale device pixels,
 * transparent pixels left out (runs of visible pixels per row) */
static pi16 img_pass24(PdfDoc PFAR *d, pi16 bx0, pi16 by0, pi16 bx1, pi16 by1,
                       fx sx0, fx sy0, fx sxdx, fx sydx, fx sxdy, fx sydy,
                       pu16 PFAR *grid, pu16 w1, pu16 h1, pu8 PFAR *limit, pu8 msk, pu8 sc)
{
    pu16 w = (pu16)((bx1 - bx0 + sc - 1) / sc), hh = (pu16)((by1 - by0 + sc - 1) / sc), o = 0, y, x;
    pu16 liy = 0xFFFFU, gro = 0;
    pu8 PFAR *hdr = d->dl + d->dlpos, PFAR *out = hdr + 10;
    if ((pu32)w * sc > 1280) return -1;                   /* viewer row buffer */
    sx0 += (sxdx + sxdy) * (sc - 1) / 2; sy0 += (sydx + sydy) * (sc - 1) / 2;
    sxdx *= sc; sydx *= sc; sxdy *= sc; sydy *= sc;
    for (y = 0; y < hh; y++) {
        fx sx = sx0, sy = sy0;
        pu16 ro = o, runs = 0, rs = 0, len = 0;
        if (out + o + 2 > limit) return -1;
        o += 2;
        for (x = 0; x <= w; x++) {
            pu16 v = TRANSP;
            if (x < w) {
                pi16 ix = (pi16)(sx >> 16), iy = (pi16)(sy >> 16);
                if (sx >= 0 && sy >= 0 && (pu16)ix < w1 && (pu16)iy < h1) v = grid[ROWOFF((pu16)iy, w1) + (pu16)ix];
                else if (!msk) {
                    if (sx < 0) ix = 0;
                    if (sy < 0) iy = 0;
                    if ((pu16)ix >= w1) ix = (pi16)w1 - 1;
                    if ((pu16)iy >= h1) iy = (pi16)h1 - 1;
                    v = grid[ROWOFF((pu16)iy, w1) + (pu16)ix];
                }
                sx += sxdx; sy += sydx;
            }
            if (v == TRANSP) {
                if (len) {                                  /* close the run */
                    pu8 PFAR *r = out + rs;
                    r[2] = (pu8)len; r[3] = (pu8)(len >> 8); r[4] = 1; r[5] = 0; r[6] = 0; r[7] = 3;
                    len = 0;
                }
                continue;
            }
            if (!len) {                                     /* open a run */
                if (out + o + 8 > limit) return -1;
                rs = o;
                out[o] = (pu8)x; out[o + 1] = (pu8)(x >> 8);
                o += 8; runs++;
            }
            if (out + o + 3 > limit) return -1;
            out[o] = (pu8)(((v >> 8) & 0xF8) | (v >> 13));
            out[o + 1] = (pu8)(((v >> 3) & 0xFC) | ((v >> 9) & 3));
            out[o + 2] = (pu8)(((v << 3) & 0xF8) | ((v >> 2) & 7));
            o += 3; len++;
        }
        out[ro] = (pu8)runs; out[ro + 1] = (pu8)(runs >> 8);
        sx0 += sxdy; sy0 += sydy;
    }
    hdr[0] = DL_RGBIMG; hdr[1] = sc;
    hdr[2] = (pu8)bx0; hdr[3] = (pu8)((pu16)bx0 >> 8); hdr[4] = (pu8)by0; hdr[5] = (pu8)((pu16)by0 >> 8);
    hdr[6] = (pu8)(o + 2); hdr[7] = (pu8)((o + 2) >> 8); hdr[8] = (pu8)hh; hdr[9] = (pu8)(hh >> 8);
    d->dlpos += 10 + o;
    return 1;
}

/* Decode the sample rows of s into grid. kind 0: bit grid of a stencil
 * (1 = paint), 1: values, 2: SMask (alpha < 128 -> 255), 3: stencil /Mask. */
static void img_rows(ImgInfo PFAR *ii, Src PFAR *s, pu8 PFAR *grid, pu16 w1, pu16 h1, pu8 kind,
                     pu8 PFAR *lut, pu8 PFAR *cur, pu8 PFAR *prev, pu8 tc)
{
    pu16 rowlen = (pu16)(((pu32)ii->w * ii->nc * ii->bpc + 7) >> 3);
    pu16 bpp = (pu16)((ii->nc * ii->bpc + 7) >> 3), rb1 = kind ? (tc ? 2 * w1 : w1) : (w1 + 7) >> 3;
    pu16 maxv = (pu16)((1U << (ii->bpc > 8 ? 8 : ii->bpc)) - 1), r, kept = 0;
    pu32 nextrow = 0;
    p_memset(prev, 0, rowlen + 1);
    for (r = 0; r < ii->h && kept < h1; r++) {
        pu16 k;
        pi16 ft = 0;
        if (ii->pred >= 10) { ft = img_byte(s, ii->hex); if (ft < 0) break; }
        for (k = 0; k < rowlen; k++) {
            pi16 v = img_byte(s, ii->hex);
            if (v < 0) return;
            if (ft) {
                pi16 left = k >= bpp ? cur[k - bpp] : 0, up = prev[k], ul = k >= bpp ? prev[k - bpp] : 0;
                if (ft == 1) v += left;
                else if (ft == 2) v += up;
                else if (ft == 3) v += (left + up) >> 1;
                else if (ft == 4) {
                    pi16 p0 = left + up - ul, pa = p0 - left, pb = p0 - up, pc = p0 - ul;
                    if (pa < 0) pa = -pa;
                    if (pb < 0) pb = -pb;
                    if (pc < 0) pc = -pc;
                    v += (pa <= pb && pa <= pc) ? left : (pb <= pc ? up : ul);
                }
            }
            cur[k] = (pu8)v;
        }
        while (r == (pu16)nextrow && kept < h1) {
            pu16 j, sc = 0, rem = 0, sq = ii->w / w1, sr = ii->w % w1;   /* sc = j * w / w1 */
            pu8 PFAR *brow = grid + kept * rb1;
            for (j = 0; j < w1; j++, sc += sq, rem += sr) {
                pu16 q, raw[4];
                pu8 v[4], g;
                if (rem >= w1) { rem -= w1; sc++; }
                for (q = 0; q < ii->nc && q < 4; q++) {
                    pu16 idx = sc * ii->nc + q, val;
                    if (ii->bpc == 8) val = cur[idx];
                    else if (ii->bpc == 16) val = cur[idx * 2];
                    else {
                        pu16 bo = idx * ii->bpc;
                        val = (pu16)((cur[bo >> 3] >> (8 - ii->bpc - (bo & 7))) & maxv);
                    }
                    raw[q] = val;
                    if (ii->bpc < 8 && ii->cs != 3 && !ii->mask) val = (pu16)(val * 255U / maxv);
                    v[q] = (pu8)val;
                }
                if (kind == 0) {
                    if ((v[0] != 0) == (ii->inv != 0)) brow[j >> 3] |= (pu8)(0x80 >> (j & 7));
                } else if (tc) {
                    pu16 PFAR *gp = (pu16 PFAR *)brow + j;
                    if (kind == 3) { if ((v[0] != 0) != (ii->inv != 0)) *gp = TRANSP; }
                    else if (kind == 2) {
                        g = to_gray(ii->cs, v);
                        if (ii->inv) g = (pu8)(255 - g);
                        *gp = g < 128 ? TRANSP : (pu16)(g == 254 ? 0 : 255 - g);
                    } else if (*gp != TRANSP) {
                        pu8 key = ii->nck >= 2 * ii->nc;
                        for (q = 0; key && q < ii->nc && q < 4; q++)
                            if (raw[q] < ii->ck[2 * q] || raw[q] > ii->ck[2 * q + 1]) key = 0;
                        if (key) *gp = TRANSP;
                        else if (ii->cs == 3) *gp = ((pu16 PFAR *)lut)[v[0]];
                        else {
                            pi16 rgb[3];
                            img_rgb(ii->cs, ii->color, ii->inv, v, (pu8)*gp, rgb);
                            *gp = pack565(rgb[0], rgb[1], rgb[2]);
                        }
                    }
                } else if (kind == 3) {
                    if ((v[0] != 0) != (ii->inv != 0)) brow[j] = 255;
                } else if (kind == 2) {
                    g = to_gray(ii->cs, v);
                    if (ii->inv) g = (pu8)(255 - g);
                    brow[j] = (pu8)(g < 128 ? 255 : 255 - g);
                } else if (brow[j] != 255) {
                    pu8 key = ii->nck >= 2 * ii->nc;
                    for (q = 0; key && q < ii->nc && q < 4; q++)
                        if (raw[q] < ii->ck[2 * q] || raw[q] > ii->ck[2 * q + 1]) key = 0;
                    if (key) brow[j] = 255;
                    else if (ii->cs == 3) brow[j] = lut[v[0]];
                    else brow[j] = img_value(ii->cs, ii->color, ii->inv, v, brow[j], SDITH(kept, j));
                }
            }
            kept++;
            nextrow = ((pu32)kept * ii->h) / h1;
        }
        { pu8 PFAR *t = prev; prev = cur; cur = t; }
    }
}

/* ---------------- baseline JPEG ---------------- */
typedef struct JHuf { pi32 maxc[17]; pu16 minc[17], vptr[17]; pu8 val[256]; pu16 fast[256]; } JHuf;
typedef struct JComp { pu8 id, h, v, tq, td, ta; pi16 pred; } JComp;
typedef struct Jpg {
    JHuf hu[4];                            /* DC 0/1, AC 0/1 */
    pu8 q[4][64];
    JComp c[3];
    pu8 nc, hmax, vmax, marker, nbits;
    pu16 W, H, ri, bitbuf, dw;           /* dw: device width of the image */
    Src PFAR *s;
    pi16 blk[64], tmp[64];
    pu8 pix[3][256];
} Jpg;

static const pu8 TABSEG ZZ[64] = {
    0, 1, 8, 16, 9, 2, 3, 10, 17, 24, 32, 25, 18, 11, 4, 5, 12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13, 6, 7, 14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51, 58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63 };
static const pi16 TABSEG JCOS[64] = {
    2896, 4017, 3784, 3406, 2896, 2276, 1567, 799, 2896, 3406, 1567, -799, -2896, -4017, -3784, -2276,
    2896, 2276, -1567, -4017, -2896, 799, 3784, 3406, 2896, 799, -3784, -2276, 2896, 3406, -1567, -4017,
    2896, -799, -3784, 2276, 2896, -3406, -1567, 4017, 2896, -2276, -1567, 4017, -2896, -799, 3784, -3406,
    2896, -3406, 1567, 799, -2896, 4017, -3784, 2276, 2896, -4017, 3784, -3406, 2896, -2276, 1567, -799 };

static pi16 jbyte(Jpg PFAR *j)
{
    pi16 c, n;
    if (j->marker) return 0;
    c = src_get(j->s);
    if (c < 0) { j->marker = 0xD9; return 0; }
    if (c != 0xFF) return c;
    do n = src_get(j->s); while (n == 0xFF);
    if (n == 0) return 0xFF;
    j->marker = (pu8)(n < 0 ? 0xD9 : n);
    return 0;
}

/* bit buffer: nbits valid bits, right aligned in bitbuf (at most 16) */
static void jfill(Jpg PFAR *j)
{
    while (j->nbits <= 8) { j->bitbuf = (pu16)((j->bitbuf << 8) | (pu8)jbyte(j)); j->nbits += 8; }
}

static pu16 jget(Jpg PFAR *j, pi16 n)             /* n <= 16 bits */
{
    pu16 v = 0;
    while (n > 0) {
        pi16 take;
        jfill(j);
        take = n < j->nbits ? n : j->nbits;
        v = (pu16)((v << take) | ((j->bitbuf >> (j->nbits - take)) & ((1U << take) - 1)));
        j->nbits = (pu8)(j->nbits - take); n -= take;
    }
    return v;
}

static pi16 jrecv(Jpg PFAR *j, pi16 n)           /* n bits, sign extended */
{
    pu16 v;
    if (n <= 0 || n > 15) return 0;
    v = jget(j, n);
    if (v < (1U << (n - 1))) return (pi16)(v - (1U << n) + 1);
    return (pi16)v;
}

static pi16 jhuff(Jpg PFAR *j, JHuf PFAR *h)
{
    pi32 code;
    pi16 l;
    pu16 e;
    jfill(j);
    e = h->fast[(j->bitbuf >> (j->nbits - 8)) & 0xFF];  /* codes up to 8 bits */
    if (e) { j->nbits = (pu8)(j->nbits - (e >> 8)); return (pi16)(e & 0xFF); }
    code = jget(j, 8);
    for (l = 9; l <= 16; l++) {
        code = (code << 1) | jget(j, 1);
        if (code <= h->maxc[l]) return h->val[(pu8)(h->vptr[l] + (pu16)(code - h->minc[l]))];
    }
    return 0;
}

static pu16 jword(Src PFAR *s) { pi16 a = src_get(s); return (pu16)((a << 8) | (src_get(s) & 0xFF)); }

/* one 8x8 block of component c into out (stride bytes per row) */
static void jblock(Jpg PFAR *j, JComp PFAR *c, pu8 PFAR *out, pu16 stride, pu8 dconly)
{
    pi16 PFAR *blk = j->blk, PFAR *tmp = j->tmp;
    pu8 PFAR *q = j->q[c->tq & 3];
    pi16 k, s, t, x, y, u;
    pu8 ac = 0;
    p_memset(blk, 0, sizeof(j->blk));
    t = jhuff(j, &j->hu[c->td & 1]);
    c->pred += jrecv(j, t);
    blk[0] = c->pred * q[0];
    for (k = 1; k < 64; ) {
        s = jhuff(j, &j->hu[2 + (c->ta & 1)]);
        t = s >> 4; s &= 15;
        if (!s) { if (t != 15) break; k += 16; continue; }
        k += t;
        if (k > 63) break;
        t = jrecv(j, s);
        blk[ZZ[k]] = t * q[k];
        ac = 1;
        k++;
    }
    if (!out) return;
    if (dconly || !ac) {
        pu8 v = (pu8)clamp255((blk[0] + 4) / 8 + 128);
        for (y = 0; y < 8; y++) p_memset(out + y * stride, v, 8);
        return;
    }
    for (y = 0; y < 8; y++) {
        pi16 PFAR *b = blk + y * 8;
        if (!(b[1] | b[2] | b[3] | b[4] | b[5] | b[6] | b[7])) {
            t = (pi16)(((pi32)b[0] * 2896) >> 12);
            for (x = 0; x < 8; x++) tmp[y * 8 + x] = t;
            continue;
        }
        for (x = 0; x < 8; x++) {
            pi32 acc = 0;
            for (u = 0; u < 8; u++) acc += (pi32)JCOS[x * 8 + u] * b[u];
            tmp[y * 8 + x] = (pi16)(acc >> 12);
        }
    }
    for (x = 0; x < 8; x++)
        for (y = 0; y < 8; y++) {
            pi32 acc = 8192;
            for (u = 0; u < 8; u++) acc += (pi32)JCOS[y * 8 + u] * tmp[u * 8 + x];
            out[y * stride + x] = (pu8)clamp255((pi16)(acc >> 14) + 128);
        }
}

/* decode a baseline JPEG into the grid. 0 = not supported */
static pi16 jpg_decode(ImgInfo PFAR *ii, Src PFAR *s, pu8 PFAR *grid, pu16 w1, pu16 h1, Jpg PFAR *j, pu8 tc, pu16 dw)
{
    pi16 c, i, n;
    pu16 len;
    p_memset(j, 0, sizeof(Jpg));
    j->s = s; j->dw = dw;
    if (src_get(s) != 0xFF || src_get(s) != 0xD8) return 0;
    for (;;) {
        do c = src_get(s); while (c >= 0 && c != 0xFF);
        do c = src_get(s); while (c == 0xFF);
        if (c < 0 || c == 0xD9) return 0;
        if (c == 0xD8 || (c >= 0xD0 && c <= 0xD7) || c == 0x01) continue;
        len = jword(s);
        if (len < 2) return 0;
        len -= 2;
        if (c == 0xC0 || c == 0xC1) {
            if (src_get(s) != 8) return 0;
            j->H = jword(s); j->W = jword(s); j->nc = (pu8)src_get(s);
            if (j->nc != 1 && j->nc != 3) return 0;
            for (i = 0; i < j->nc; i++) {
                j->c[i].id = (pu8)src_get(s); n = src_get(s); j->c[i].tq = (pu8)src_get(s);
                j->c[i].h = (pu8)((n >> 4) & 3); j->c[i].v = (pu8)(n & 3);
                if (!j->c[i].h || !j->c[i].v || j->c[i].h > 2 || j->c[i].v > 2) return 0;
                if (j->nc == 1) j->c[i].h = j->c[i].v = 1;
                if (j->c[i].h > j->hmax) j->hmax = j->c[i].h;
                if (j->c[i].v > j->vmax) j->vmax = j->c[i].v;
            }
            if (!j->W || !j->H) return 0;
        } else if (c >= 0xC2 && c <= 0xCF && c != 0xC4 && c != 0xC8 && c != 0xCC) {
            return 0;                                   /* progressive, lossless, arithmetic */
        } else if (c == 0xC4) {
            while (len >= 17) {
                pu8 cnt[16];
                pu16 tot = 0, code = 0, k = 0;
                JHuf PFAR *h;
                n = src_get(s);
                h = &j->hu[((n >> 4) & 1) * 2 + (n & 1)];
                for (i = 0; i < 16; i++) { cnt[i] = (pu8)src_get(s); tot += cnt[i]; }
                if (tot > 256 || 17 + tot > len) return 0;
                for (i = 0; i < (pi16)tot; i++) h->val[i] = (pu8)src_get(s);
                p_memset(h->fast, 0, sizeof(h->fast));
                for (i = 1; i <= 16; i++) {
                    h->vptr[i] = k; h->minc[i] = code;
                    if (i <= 8) {                           /* fast table */
                        pu16 c2, f;
                        for (c2 = 0; c2 < cnt[i - 1]; c2++)
                            for (f = 0; f < (1U << (8 - i)); f++)
                                h->fast[(pu8)(((code + c2) << (8 - i)) | f)] = (pu16)((i << 8) | h->val[(pu8)(k + c2)]);
                    }
                    code += cnt[i - 1]; k += cnt[i - 1];
                    h->maxc[i] = cnt[i - 1] ? (pi32)code - 1 : -1L;
                    code <<= 1;
                }
                len -= 17 + tot;
            }
            while (len--) src_get(s);
        } else if (c == 0xDB) {
            while (len >= 65) {
                n = src_get(s);
                for (i = 0; i < 64; i++) {
                    pu16 v = (n >> 4) ? jword(s) : (pu16)src_get(s);
                    j->q[n & 3][i] = (pu8)(v > 255 ? 255 : v);
                }
                len -= (n >> 4) ? 129 : 65;
            }
            while (len--) src_get(s);
        } else if (c == 0xDD) {
            j->ri = jword(s);
        } else if (c == 0xDA) {
            break;
        } else while (len--) src_get(s);
    }
    /* SOS */
    n = src_get(s);
    if (!j->nc || n != j->nc) return 0;                 /* one interleaved scan only */
    for (i = 0; i < n; i++) {
        pi16 id = src_get(s), t = src_get(s), q;
        for (q = 0; q < j->nc; q++)
            if (j->c[q].id == id) { j->c[q].td = (pu8)(t >> 4); j->c[q].ta = (pu8)(t & 15); }
    }
    src_get(s); src_get(s); src_get(s);
    ii->color = (pu8)(j->nc == 3);
    {
        pu16 mw = 8 * j->hmax, mh = 8 * j->vmax, mx, my, nmx = (j->W + mw - 1) / mw, nmy = (j->H + mh - 1) / mh;
        pu16 ka = 0, kb = 0, ja, jb, cnt = 0, k, jj;
        pu8 dconly = (pu8)(j->W >= 8U * w1 && j->H >= 8U * h1 && j->dw <= 2U * w1);
        for (my = 0; my < nmy; my++) {
            pu32 y0 = (pu32)my * mh;
            ka = kb;
            while (kb < h1 && ((pu32)kb * j->H) / h1 < y0 + mh) kb++;
            ja = jb = 0;
            for (mx = 0; mx < nmx; mx++) {
                pu32 x0 = (pu32)mx * mw;
                pu8 need;
                ja = jb;
                while (jb < w1 && ((pu32)jb * j->W) / w1 < x0 + mw) jb++;
                need = (pu8)(ka < kb && ja < jb);
                if (j->ri && cnt && cnt % j->ri == 0) {          /* restart interval */
                    if (!j->marker) {
                        do c = src_get(s); while (c >= 0 && c != 0xFF);
                        do c = src_get(s); while (c == 0xFF);
                        if (c < 0) return 1;
                        j->marker = (pu8)c;
                    }
                    if (j->marker < 0xD0 || j->marker > 0xD7) return 1;
                    j->marker = 0; j->nbits = 0;
                    for (i = 0; i < j->nc; i++) j->c[i].pred = 0;
                }
                cnt++;
                for (i = 0; i < j->nc; i++) {
                    JComp PFAR *cp = &j->c[i];
                    pu16 bx, by, st = 8U * cp->h;
                    for (by = 0; by < cp->v; by++)
                        for (bx = 0; bx < cp->h; bx++)
                            jblock(j, cp, need ? j->pix[i] + by * 8 * st + bx * 8 : (pu8 PFAR *)0, st, dconly);
                }
                if (!need) continue;
                for (k = ka; k < kb; k++) {
                    pu16 py = (pu16)(((pu32)k * j->H) / h1 - y0);
                    pu8 PFAR *g = grid + k * w1;
                    pu16 PFAR *g16 = (pu16 PFAR *)grid + k * w1;
                    for (jj = ja; jj < jb; jj++) {
                        pu16 px = (pu16)(((pu32)jj * j->W) / w1 - x0);
                        pi16 yv, cb, cr;
                        if (tc ? g16[jj] == TRANSP : g[jj] == 255) continue;
                        yv = j->pix[0][(py * j->c[0].v / j->vmax) * 8 * j->c[0].h + px * j->c[0].h / j->hmax];
                        if (tc) {
                            pu8 w = (pu8)g16[jj];
                            if (j->nc == 1) { yv = fade(yv, w); g16[jj] = pack565(yv, yv, yv); continue; }
                            cb = j->pix[1][(py * j->c[1].v / j->vmax) * 8 * j->c[1].h + px * j->c[1].h / j->hmax] - 128;
                            cr = j->pix[2][(py * j->c[2].v / j->vmax) * 8 * j->c[2].h + px * j->c[2].h / j->hmax] - 128;
                            g16[jj] = pack565(fade(clamp255(yv + (pi16)((cr * 359L) >> 8)), w),
                                              fade(clamp255(yv - (pi16)((cb * 88L + cr * 183L) >> 8)), w),
                                              fade(clamp255(yv + (pi16)((cb * 454L) >> 8)), w));
                            continue;
                        }
                        if (j->nc == 1) { yv = fade(yv, g[jj]); g[jj] = (pu8)(yv >= 255 ? 254 : yv); continue; }
                        cb = j->pix[1][(py * j->c[1].v / j->vmax) * 8 * j->c[1].h + px * j->c[1].h / j->hmax] - 128;
                        cr = j->pix[2][(py * j->c[2].v / j->vmax) * 8 * j->c[2].h + px * j->c[2].h / j->hmax] - 128;
                        g[jj] = cube(fade(clamp255(yv + (pi16)((cr * 359L) >> 8)), g[jj]),
                                     fade(clamp255(yv - (pi16)((cb * 88L + cr * 183L) >> 8)), g[jj]),
                                     fade(clamp255(yv + (pi16)((cb * 454L) >> 8)), g[jj]), SDITH(k, jj));
                    }
                }
            }
        }
    }
    return 1;
}

static pi16 img_info(PdfDoc PFAR *d, pu16 dict, ImgInfo PFAR *ii, pu8 inl);

/* Draw the image into the external blocks, in bands that fit; the display
 * list gets a DL_EXT record per band. -1: no room at all (nothing drawn). */
static pi16 img_ext(PdfDoc PFAR *d, pu8 kind, pu8 col, pi16 bx0, pi16 by0, pi16 bx1, pi16 by1,
                    fx sx0, fx sy0, fx sxdx, fx sydx, fx sxdy, fx sydy,
                    pu8 PFAR *grid, pu16 rb1, pu16 w1, pu16 h1, pu8 PFAR *rowtmp, pu8 PFAR *tab, pu8 msk)
{
    pu32 px = (pu32)(bx1 - bx0) * (by1 - by0), cap;
    pu8 sc = 1, maxsc = (pu8)(kind == 2 ? 8 : kind == 1 ? 4 : 1), k0 = d->extk;
    pu16 p0 = d->extpos, dl0 = d->dlpos;
    pi16 y;
    cap = (pu32)(d->next - d->extk) * (d->extsize - 16) - d->extpos;
    cap /= d->jleft ? d->jleft : 1;                         /* fair share for the images to come */
    while (sc < maxsc) {                                    /* pixel size for the room left */
        pu32 est = kind == 2 ? px * 3 : kind == 1 ? px * 5 / 8 : px / 8;
        est = est / ((pu32)sc * sc) + (pu32)(by1 - by0) / sc * 16;
        if (est + est / 8 <= cap) break;
        sc++;
    }
retry:
    y = by0;
    while (y < by1) {
        pi16 hb = by1 - y;
        for (;;) {
            pu8 PFAR *sdl = d->dl;
            pu16 spos = d->dlpos, ssz = d->dlsize, st = d->extpos, en;
            pi16 ye = y + hb > by1 ? by1 : y + hb, r;
            fx bsx = sx0 + sxdy * (y - by0), bsy = sy0 + sydy * (y - by0);
            d->dl = d->ext[d->extk]; d->dlpos = st; d->dlsize = d->extsize - 1;
            if (kind == 2)
                r = img_pass24(d, bx0, y, bx1, ye, bsx, bsy, sxdx, sydx, sxdy, sydy,
                               (pu16 PFAR *)grid, w1, h1, d->dl + d->dlsize, msk, sc);
            else if (kind == 1)
                r = img_pass4(d, bx0, y, bx1, ye, bsx, bsy, sxdx, sydx, sxdy, sydy,
                              grid, w1, h1, rowtmp, d->dl + d->dlsize, tab, 1, sc);
            else
                r = img_pass(d, col, 1, bx0, y, bx1, ye, bsx, bsy, sxdx, sydx, sxdy, sydy,
                             grid, rb1, w1, h1, rowtmp, d->dl + d->dlsize, 0);
            en = d->dlpos;
            d->dl = sdl; d->dlpos = spos; d->dlsize = ssz;
            if (r > 0) {
                if (en > st) {
                    d->ext[d->extk][en] = DL_END;
                    d->extpos = en + 1;
                    if (!dl_room(d, 6)) return 1;
                    dl_b(d, DL_EXT); dl_b(d, d->extk); dl_w(d, (pi16)st); dl_w(d, (pi16)(en + 1 - st));
                }
                y = ye;
                break;
            }
            if (hb > 8 * sc) { hb = (pi16)((hb / 2 + sc - 1) / sc * sc); continue; }
            if (d->extk + 1 < d->next) { d->extk++; d->extpos = 0; hb = by1 - y; continue; }
            /* out of room: start again with bigger pixels */
            d->extk = k0; d->extpos = p0; d->dlpos = dl0; d->trunc = 0;
            if (sc >= maxsc) return -1;
            sc++;
            goto retry;
        }
    }
    return 1;
}

/* Decode the image (and its mask) and append its records. 0 = not supported.
 * s == 0: the XObject stream described by ii->soff/slen/sflags. */
static pi16 img_draw(PdfDoc PFAR *d, ImgInfo PFAR *ii, Src PFAR *s, fx PFAR *m, pu8 col)
{
    fx cx[4], cy[4], a, b, c, e, det, sxdx, sxdy, sydx, sydy, px, py;
    pi16 i, bx0, by0, bx1, by1, wpx, hpx;
    pu16 w1, h1, rb1, rowlen = 0, mrow = 0, sc, scr, rts;
    pu8 PFAR *top = d->dl + d->dlsize, PFAR *tab, PFAR *cur, PFAR *grid, PFAR *rowtmp;
    pu8 hasm = 0;
    ImgInfo mi;
    Src own;
    if (ii->w == 0 || ii->h == 0) return 0;
    if (!ii->dct && ii->bpc != 1 && ii->bpc != 2 && ii->bpc != 4 && ii->bpc != 8 && ii->bpc != 16) return 0;
    if (d->trunc) return 0;
    xform(m, 0, 0, &cx[0], &cy[0]); xform(m, FXONE, 0, &cx[1], &cy[1]);
    xform(m, 0, FXONE, &cx[2], &cy[2]); xform(m, FXONE, FXONE, &cx[3], &cy[3]);
    a = cx[0]; b = cx[0]; c = cy[0]; e = cy[0];
    for (i = 1; i < 4; i++) {
        if (cx[i] < a) a = cx[i];
        if (cx[i] > b) b = cx[i];
        if (cy[i] < c) c = cy[i];
        if (cy[i] > e) e = cy[i];
    }
    wpx = (pi16)((b - a) >> 16) + 1; hpx = (pi16)((e - c) >> 16) + 1;
    bx0 = (pi16)(a >> 16); bx1 = (pi16)((b + 0xFFFFL) >> 16);
    by0 = (pi16)(c >> 16); by1 = (pi16)((e + 0xFFFFL) >> 16);
    if (bx0 < 0) bx0 = 0;
    if (by0 < 0) by0 = 0;
    if (bx1 > d->devw) bx1 = d->devw;
    if (by1 > d->devh) by1 = d->devh;
    if (bx1 <= bx0 || by1 <= by0) return 1;                  /* off the page */
    /* sample grid: no finer than the device */
    w1 = ii->w; h1 = ii->h;
    i = wpx > hpx ? wpx : hpx;
    if (m[1] == 0 && m[2] == 0) { if (w1 > (pu16)wpx + 1) w1 = (pu16)wpx + 1; if (h1 > (pu16)hpx + 1) h1 = (pu16)hpx + 1; }
    else if (m[0] == 0 && m[3] == 0) { if (w1 > (pu16)hpx + 1) w1 = (pu16)hpx + 1; if (h1 > (pu16)wpx + 1) h1 = (pu16)wpx + 1; }
    if (w1 > (pu16)i + 1) w1 = (pu16)i + 1;
    if (h1 > (pu16)i + 1) h1 = (pu16)i + 1;
    if (ii->mask) while ((pu32)((w1 + 7) >> 3) * h1 > 20000UL) { w1 = (pu16)(w1 * 3U / 4); h1 = (pu16)(h1 * 3U / 4); }
    else {
        /* leave room for the output: 16 colours if possible, else 1 bit */
        pu32 px = (pu32)(bx1 - bx0) * (by1 - by0), fr = (pu32)d->dlsize - d->dlpos, bud;
        pu32 e4 = px / 2 + px / 8 + (by1 - by0) * 4UL;
        pu32 e2 = px / 8 + px / 32 + (by1 - by0) * 2UL, sh, est;
        sh = fr > 9000UL ? (fr - 9000UL) / (d->jleft ? d->jleft : 1) : 3000UL;
        if (sh < 4000UL) sh = 4000UL;
        d->share = sh;
        est = e4 <= sh ? e4 : e2 <= sh ? e2 : e2 / 2;
        bud = fr > est + 9000UL ? fr - est - 5000UL : 1500UL;
        if (d->tc) bud = fr > sh + 7000UL ? fr - sh - 4000UL : 3000UL;
        if (bud > 20000UL) bud = 20000UL;
        if (d->next) {                                     /* output goes to the external blocks */
            bud = fr > 8000UL ? fr - 6000UL : 1500UL;
            if (bud > 60000UL) bud = 60000UL;
        }
        if (d->tc) bud /= 2;
        while ((pu32)w1 * h1 > bud) { w1 = (pu16)(w1 * 3U / 4); h1 = (pu16)(h1 * 3U / 4); }
    }
    if (w1 == 0 || h1 == 0) return 0;
    rb1 = ii->mask ? (pu16)((w1 + 7) >> 3) : d->tc ? 2 * w1 : w1;
    if (!ii->dct) {
        if ((pu32)ii->w * ii->nc * ii->bpc > 48000UL) return 0;
        rowlen = (pu16)(((pu32)ii->w * ii->nc * ii->bpc + 7) >> 3);
    }
    /* soft mask or stencil mask */
    if (ii->mobj && !ii->mask && !s) {
        pu16 md = load_obj(d, ii->mobj);
        pi32 moff = d->streamoff;
        if (md != NIL && moff >= 0 && img_info(d, md, &mi, 0) && mi.nc == 1 &&
            stream_info(d, md, &mi.slen, &mi.sflags) == 0 && !(mi.sflags & SF_DCT) &&
            (pu32)mi.w * mi.bpc <= 48000UL) {
            mi.soff = moff; hasm = ii->mkind;
            mrow = (pu16)(((pu32)mi.w * mi.bpc + 7) >> 3);
        }
    }
    sc = (rowlen > mrow ? rowlen : mrow) + 1;
    scr = 2 * sc;
    if (ii->dct && scr < sizeof(Jpg)) scr = sizeof(Jpg);
    rts = (pu16)(((bx1 - bx0 + 7) >> 3) + ((bx1 - bx0 + 1) >> 1) + 2);
    if ((pu32)d->dlpos + TABSZ + scr + (pu32)rb1 * h1 + rts + 256 > d->dlsize) return 0;
    tab = top - TABSZ; cur = tab - scr; grid = cur - rb1 * h1; rowtmp = grid - rts;
    if (ii->cs == 3) {                                          /* grid value of each palette entry */
        pu16 k, q, nb = ii->base == 1 ? 3 : ii->base == 2 ? 4 : 1;
        pu8 v[4];
        for (k = 0; k < 256; k++) {
            for (q = 0; q < nb; q++) v[q] = (k * nb + q < ii->lutlen) ? d->ilut[k * nb + q] : 0;
            if (d->tc) {
                pi16 rgb[3];
                img_rgb(ii->base, ii->color, 0, v, 0, rgb);
                ((pu16 PFAR *)tab)[k] = pack565(rgb[0], rgb[1], rgb[2]);
            } else tab[k] = img_value(ii->base, ii->color, 0, v, 0, 0);
        }
    }
    p_memset(grid, 0, rb1 * h1);
    if (hasm) {
        src_stream(d, &own, d->rawbuf, d->cbuf, CBUF_SIZE, mi.soff, mi.slen, mi.sflags);
        img_rows(&mi, &own, grid, w1, h1, (pu8)(hasm == 1 ? 2 : 3), tab, cur, cur + sc, (pu8)(d->tc && !ii->mask));
    }
    if (!s) {
        src_stream(d, &own, d->rawbuf, d->cbuf, CBUF_SIZE, ii->soff, ii->slen, ii->sflags);
        s = &own;
    }
    if (ii->dct) { if (!jpg_decode(ii, s, grid, w1, h1, (Jpg PFAR *)cur, d->tc, (pu16)(wpx > hpx ? wpx : hpx))) return 0; }
    else img_rows(ii, s, grid, w1, h1, (pu8)(ii->mask ? 0 : 1), tab, cur, cur + sc, (pu8)(d->tc && !ii->mask));
    /* device pixel -> sample grid (see notes: scaled by 1/16 against overflow) */
    {
        fx a2 = m[0] >> 4, b2 = m[1] >> 4, c2 = m[2] >> 4, d2 = m[3] >> 4;
        det = fx_mul(a2, d2) - fx_mul(b2, c2);
        if (det == 0) return 1;
        sxdx = (fx)(((pi32)fx_div(d2, det) * (pi32)w1) >> 4);
        sxdy = (fx)(((pi32)fx_div(-c2, det) * (pi32)w1) >> 4);
        sydx = -(fx)(((pi32)fx_div(-b2, det) * (pi32)h1) >> 4);
        sydy = -(fx)(((pi32)fx_div(a2, det) * (pi32)h1) >> 4);
    }
    px = FX(bx0) + 0x8000L - m[4]; py = FX(by0) + 0x8000L - m[5];
    {
        fx sx0 = fx_mul(sxdx, px) + fx_mul(sxdy, py);
        fx sy0 = FX(h1) + fx_mul(sydx, px) + fx_mul(sydy, py);
        pu8 axis = (pu8)((m[1] == 0 && m[2] == 0) || (m[0] == 0 && m[3] == 0)), hast = 0;
        pu8 PFAR *limit = rowtmp;
        pu16 n = rb1 * h1, k;
        pu8 kind = (pu8)(ii->mask ? 0 : d->tc ? 2 : 1);
        if (kind == 2) { n = w1 * h1; for (k = 0; k < n; k++) if (((pu16 PFAR *)grid)[k] == TRANSP) { hast = 1; break; } }
        else if (kind == 1) { for (k = 0; k < n; k++) if (grid[k] == 255) { hast = 1; break; } img_tabs(tab, ii->color); }
        if (d->extk < d->next) {                           /* external image blocks */
            pi16 r = img_ext(d, kind, col, bx0, by0, bx1, by1, sx0, sy0, sxdx, sydx, sxdy, sydy,
                             grid, rb1, w1, h1, rowtmp, tab, (pu8)(hast || !axis));
            if (r >= 0) return r;
        }
        if (ii->mask)
            return img_pass(d, col, 1, bx0, by0, bx1, by1, sx0, sy0, sxdx, sydx, sxdy, sydy,
                            grid, rb1, w1, h1, rowtmp, limit, 0) > 0;
        if (d->tc) {
            pu8 sc2;
            for (sc2 = 1; sc2 <= 8; sc2++) {
                pu32 ww = (pu32)(bx1 - bx0 + sc2 - 1) / sc2, hh2 = (pu32)(by1 - by0 + sc2 - 1) / sc2;
                if (hh2 * (12 + 3 * ww) > d->share && sc2 < 8) continue;
                if (img_pass24(d, bx0, by0, bx1, by1, sx0, sy0, sxdx, sydx, sxdy, sydy,
                               (pu16 PFAR *)grid, w1, h1, limit, (pu8)(hast || !axis), sc2) > 0) return 1;
            }
            return 0;
        }
        /* always with mask: GEOS 1 draws packed 4-bit bitmaps without a
         * mask garbled (type 0x01), with a mask (0x11) correctly */
        {
            pu8 PFAR *lim4 = limit;                        /* keep room for the other images */
            pu8 sc4;
            if ((pu32)d->dlpos + d->share < (pu32)(pu16)(limit - d->dl)) lim4 = d->dl + d->dlpos + (pu16)d->share;
            for (sc4 = 1; sc4 <= 4; sc4++)
                if (img_pass4(d, bx0, by0, bx1, by1, sx0, sy0, sxdx, sydx, sxdy, sydy,
                              grid, w1, h1, rowtmp, lim4, tab, 1, sc4) > 0) return 1;
        }
        /* too big for 16 colours: dithered black and white */
        if (axis && !hast) out_rect(d, 15, bx0, by0, bx1 - 1, by1 - 1);
        else if (img_pass(d, 15, 0, bx0, by0, bx1, by1, sx0, sy0, sxdx, sydx, sxdy, sydy,
                          grid, rb1, w1, h1, rowtmp, limit, tab + 768) < 0) return 0;
        if (img_pass(d, 0, 1, bx0, by0, bx1, by1, sx0, sy0, sxdx, sydx, sxdy, sydy,
                     grid, rb1, w1, h1, rowtmp, limit, tab + 768) < 0) return 0;
    }
    return 1;
}

/* colour space of an image (dict node or name) -> ii; 0 = unsupported */
static pi16 img_cs(PdfDoc PFAR *d, pu16 csn, ImgInfo PFAR *ii)
{
    csn = resolve(d, csn);
    if (csn == NIL) { ii->cs = 0; ii->nc = 1; return 1; }
    if (NODE(d, csn)->type == N_ARR) {
        pu16 k = resolve(d, arr_at(d, csn, 0));
        if (node_is(d, k, N_NAME, (KS + 1087)) || node_is(d, k, N_NAME, (KS + 1095))) {
            pu16 bs = resolve(d, arr_at(d, csn, 1)), lk = resolve(d, arr_at(d, csn, 3)), q;
            ImgInfo bi;
            if (!img_cs(d, bs, &bi) || bi.cs > 2) return 0;
            if (lk == NIL || NODE(d, lk)->type != N_STR) return 0;
            ii->base = bi.cs;
            ii->lutlen = NODE(d, lk)->len > sizeof(d->ilut) ? sizeof(d->ilut) : NODE(d, lk)->len;
            for (q = 0; q < ii->lutlen; q++) d->ilut[q] = d->arena[NODE(d, lk)->child + q];
            ii->cs = 3; ii->nc = 1;
            return 1;
        }
        if (node_is(d, k, N_NAME, (KS + 1097))) {
            pu16 st = resolve(d, arr_at(d, csn, 1));
            pi32 n = node_int(d, dict_getr(d, st, (KS + 105)), 3);
            ii->nc = (pu8)n; ii->cs = n == 1 ? 0 : n == 4 ? 2 : 1;
            return n == 1 || n == 3 || n == 4;
        }
        if (node_is(d, k, N_NAME, (KS + 1106)) || node_is(d, k, N_NAME, (KS + 1113))) { ii->cs = 1; ii->nc = 3; return 1; }
        if (node_is(d, k, N_NAME, (KS + 1117))) { ii->cs = 0; ii->nc = 1; return 1; }
        if (node_is(d, k, N_NAME, (KS + 1125))) { ii->cs = 4; ii->nc = 1; return 1; }
        return 0;
    }
    if (node_is(d, csn, N_NAME, (KS + 1136)) || node_is(d, csn, N_NAME, (KS + 1147))) { ii->cs = 0; ii->nc = 1; return 1; }
    if (node_is(d, csn, N_NAME, (KS + 1149)) || node_is(d, csn, N_NAME, (KS + 1159))) { ii->cs = 1; ii->nc = 3; return 1; }
    if (node_is(d, csn, N_NAME, (KS + 1163)) || node_is(d, csn, N_NAME, (KS + 1174))) { ii->cs = 2; ii->nc = 4; return 1; }
    return 0;
}

/* image parameters from a dictionary node (XObject or parsed inline dict) */
static pi16 img_info(PdfDoc PFAR *d, pu16 dict, ImgInfo PFAR *ii, pu8 inl)
{
    pu16 dn, dp;
    p_memset(ii, 0, sizeof(ImgInfo));
    ii->w = (pu16)node_int(d, (inl ? dict_getr(d, dict, (KS + 126)) : dict_getr(d, dict, (KS + 1179))), 0);
    ii->h = (pu16)node_int(d, (inl ? dict_getr(d, dict, (KS + 1185)) : dict_getr(d, dict, (KS + 1187))), 0);
    ii->bpc = (pu8)node_int(d, (inl ? dict_getr(d, dict, (KS + 1194)) : dict_getr(d, dict, (KS + 1198))), 1);
    dn = (inl ? dict_getr(d, dict, (KS + 1215)) : dict_getr(d, dict, (KS + 1218)));
    if (dn != NIL && NODE(d, dn)->type == N_BOOL && NODE(d, dn)->val) { ii->mask = 1; ii->bpc = 1; ii->nc = 1; }
    else if (!img_cs(d, (inl ? dict_getr(d, dict, (KS + 1228)) : dict_getr(d, dict, (KS + 1231))), ii)) return 0;
    dn = (inl ? dict_getr(d, dict, (KS + 1242)) : dict_getr(d, dict, (KS + 1244)));
    if (dn != NIL && NODE(d, dn)->type == N_ARR) {
        fx v0 = node_fx(d, resolve(d, arr_at(d, dn, 0)), 0), v1 = node_fx(d, resolve(d, arr_at(d, dn, 1)), FXONE);
        if (v0 > v1) ii->inv = 1;
    }
    dp = (inl ? dict_getr(d, dict, (KS + 1251)) : dict_getr(d, dict, (KS + 138)));
    if (dp != NIL && NODE(d, dp)->type == N_ARR) dp = resolve(d, arr_at(d, dp, NODE(d, dp)->len - 1));
    if (dp != NIL && NODE(d, dp)->type == N_DICT) {
        pi32 pr = node_int(d, dict_getr(d, dp, (KS + 150)), 1);
        if (pr >= 10) ii->pred = 10;
        else if (pr > 1) return 0;
    }
    ii->color = (pu8)(!ii->mask && (ii->cs == 1 || ii->cs == 2 || (ii->cs == 3 && ii->base)));
    return ii->w > 0 && ii->h > 0;
}

/* inline image: BI already read. Parses the dictionary as an arena dict. */
static void inline_image(PdfDoc PFAR *d)
{
    pu16 mark = d->atop, dict, f;
    ImgInfo ii;
    pi16 ok = 0, c, p1 = ' ', p2 = ' ';
    d->tok.len = 2; d->tok.s[0] = '<'; d->tok.s[1] = '<';
    dict = parse_inline_dict(d, &d->cs);
    if (dict != NIL && img_info(d, dict, &ii, 1)) {
        f = dict_getr(d, dict, (KS + 1254));
        if (f != NIL && NODE(d, f)->type == N_ARR && NODE(d, f)->len == 1) f = resolve(d, arr_at(d, f, 0));
        if (f == NIL) ok = 1;
        else if (node_is(d, f, N_NAME, (KS + 87)) || node_is(d, f, N_NAME, (KS + 72))) { ii.hex = 1; ok = 1; }
        else if (node_is(d, f, N_NAME, (KS + 101)) || node_is(d, f, N_NAME, (KS + 91))) { ii.dct = 1; ok = 1; }
        if (ii.pred) ok = 0;
    }
    src_get(&d->cs);                             /* single white space after ID */
    run_flush(d);
    if (ok) ok = img_draw(d, &ii, &d->cs, GS(d)->ctm, GS(d)->fillc);
    if (!ok) image_box(d);
    d->atop = mark;
    for (;;) {                                   /* data up to EI */
        c = src_get(&d->cs);
        if (c < 0) return;
        if (p2 == 'E' && c == 'I') {
            pi16 n = src_peek(&d->cs);
            if (IS_WS2(p1) && (n < 0 || n == ' ' || n == '\n' || n == '\r' || n == '\t')) return;
        }
        p1 = p2; p2 = c;
    }
}

/* XObject image: placeholder now, the picture after the content stream */
static void image_job(PdfDoc PFAR *d, pu16 obj, pu8 form)
{
    pu16 ps;
    run_flush(d);
    ps = d->dlpos;
    if (!form) image_box(d);
    else if (!obj || d->njob >= MAXJOB) return;
    if (obj && d->njob < MAXJOB && !d->trunc) {
        d->job[d->njob].obj = obj; d->job[d->njob].ps = ps; d->job[d->njob].pe = d->dlpos;
        p_memcpy(d->job[d->njob].ctm, GS(d)->ctm, sizeof(GS(d)->ctm));
        d->job[d->njob].col = GS(d)->fillc;
        d->job[d->njob].form = form;
        d->njob++;
    }
}

static void mem_rev(pu8 PFAR *p, pu16 n)
{
    pu16 i = 0, j = n;
    while (i + 1 < j) { pu8 t = p[i]; p[i] = p[j - 1]; p[j - 1] = t; i++; j--; }
}

static void run_content(PdfDoc PFAR *d);
static void set_tm(PdfDoc PFAR *d, fx a, fx b, fx c, fx e, fx f, fx g);

/* move the records [end, dlpos) to ps, replacing the placeholder [ps, pe) */
static void splice(PdfDoc PFAR *d, pu16 ps, pu16 pe, pu16 end)
{
    pu16 n = d->dlpos - end, pl = pe - ps, i, from, cnt;
    /* [P R N] -> [N P R] -> [N R] */
    mem_rev(d->dl + ps, end - ps);
    mem_rev(d->dl + end, n);
    mem_rev(d->dl + ps, end - ps + n);
    from = ps + n + pl; cnt = d->dlpos - from;
    for (i = 0; i < cnt; i++) d->dl[ps + n + i] = d->dl[from + i];
    d->dlpos -= pl;
}

/* form XObject: run its content with its matrix and resources */
static void run_form(PdfDoc PFAR *d, pi16 k)
{
    pu16 sd, mx, res;
    fx m[6];
    pi16 i;
    d->atop = 0;
    sd = load_obj(d, d->job[k].obj);
    if (sd == NIL || d->streamoff < 0) return;
    for (i = 0; i < 6; i++) m[i] = (i == 0 || i == 3) ? FXONE : 0;
    mx = dict_getr(d, sd, (KS + 1256));
    if (mx != NIL && NODE(d, mx)->type == N_ARR && NODE(d, mx)->len == 6)
        for (i = 0; i < 6; i++) m[i] = node_fx(d, resolve(d, arr_at(d, mx, (pu16)i)), m[i]);
    res = dict_getr(d, sd, (KS + 230));
    if (res != NIL) load_res(d, res);
    d->atop = 0;
    d->gsp = 0;
    p_memset(&d->gs[0], 0, sizeof(GState));
    mat_mul(d->gs[0].ctm, m, d->job[k].ctm);
    d->gs[0].fillc = d->job[k].col; d->gs[0].lw = FXONE; d->gs[0].font = -1; d->gs[0].th = FXONE;
    set_tm(d, FXONE, 0, 0, FXONE, 0, 0);
    path_reset(d);
    d->nrun = 0; d->nopen = 0;
    d->ncont = 1; d->cont[0] = d->job[k].obj; d->curcont = 0;
    if (next_content(d)) run_content(d);
    d->incontent = 0;
    run_flush(d);
}

/* draw the deferred XObjects (last first) and put them where they were used */
static void run_jobs(PdfDoc PFAR *d, pi16 base, pi16 depth)
{
    pi16 k, top = d->njob;
    for (k = top - 1; k >= base; k--) {
        pu16 end = d->dlpos, ps = d->job[k].ps, pe = d->job[k].pe;
        d->trunc = 0;
        if (d->job[k].form) {
            if (depth >= 2) continue;
            d->njob = (pu8)top;
            run_form(d, k);
            if (d->njob > top) run_jobs(d, top, depth + 1);
            d->njob = (pu8)top;
            if (d->dlpos > end) splice(d, ps, pe, end);
        } else {
            pu16 sd, mk;
            pi16 q2;
            pi32 len, off;
            pu8 flags;
            ImgInfo ii;
            d->jleft = 0;
            for (q2 = base; q2 <= k; q2++) if (!d->job[q2].form) d->jleft++;
            d->atop = 0;
            sd = load_obj(d, d->job[k].obj);
            off = d->streamoff;
            if (sd == NIL || off < 0 || !img_info(d, sd, &ii, 0)) continue;
            if (stream_info(d, sd, &len, &flags) != 0) continue;
            ii.soff = off; ii.slen = len; ii.sflags = flags; ii.dct = (pu8)((flags & SF_DCT) != 0);
            mk = dict_get(d, sd, (KS + 1263)); ii.mkind = 1;
            if (mk == NIL || NODE(d, mk)->type != N_REF) { mk = dict_get(d, sd, (KS + 1269)); ii.mkind = 2; }
            if (mk != NIL && NODE(d, mk)->type == N_REF && NODE(d, mk)->val < 65536L) ii.mobj = (pu16)NODE(d, mk)->val;
            else if (mk != NIL && NODE(d, mk)->type == N_ARR) {
                pu16 q;
                for (q = 0; q < NODE(d, mk)->len && q < 8; q++) ii.ck[q] = (pu16)node_int(d, resolve(d, arr_at(d, mk, q)), 0);
                ii.nck = (pu8)q;
            }
            if (!img_draw(d, &ii, 0, d->job[k].ctm, d->job[k].col)) { d->dlpos = end; continue; }
            splice(d, ps, pe, end);
        }
    }
    d->atop = 0;
    d->jleft = 2;
}

static void do_xobject(PdfDoc PFAR *d, const pu8 PFAR *name, pu16 len)
{
    pu8 i;
    pu16 h = name_hash(name, len);
    for (i = 0; i < d->nxo; i++) {
        if (d->xohash[i] != h) continue;
        if (d->xoimg[i] == 1) image_job(d, d->xoobj[i], 0);
        else if (d->xoimg[i] == 2) image_job(d, d->xoobj[i], 1);
        return;
    }
}

static pi16 kw(PdfDoc PFAR *d, const char PFAR *w) { return p_streq(d->tok.s, d->tok.len, w); }

static void set_tm(PdfDoc PFAR *d, fx a, fx b, fx c, fx e, fx f, fx g)
{
    d->tm[0] = a; d->tm[1] = b; d->tm[2] = c; d->tm[3] = e; d->tm[4] = f; d->tm[5] = g;
    p_memcpy(d->tlm, d->tm, sizeof(d->tm));
}

static void td(PdfDoc PFAR *d, fx tx, fx ty)
{
    fx m[6];
    m[0] = FXONE; m[1] = 0; m[2] = 0; m[3] = FXONE; m[4] = tx; m[5] = ty;
    mat_mul(d->tlm, m, d->tlm);
    p_memcpy(d->tm, d->tlm, sizeof(d->tm));
}

static void exec_op(PdfDoc PFAR *d)
{
    pi16 n = d->nops;
    GState PFAR *g = GS(d);
    pu8 c0 = d->tok.s[0];
    fx a = opn(d, n - 6), b = opn(d, n - 5), c = opn(d, n - 4), e = opn(d, n - 3), f = opn(d, n - 2), h = opn(d, n - 1);

    switch (c0) {
    case 'q': if (kw(d, (KS + 1274)) && d->gsp + 1 < MAXGS) { d->gs[d->gsp + 1] = d->gs[d->gsp]; d->gsp++; } return;
    case 'Q': if (kw(d, (KS + 1276)) && d->gsp > 0) d->gsp--; return;
    case 'c':
        if (kw(d, (KS + 1278))) { fx m[6]; m[0] = a; m[1] = b; m[2] = c; m[3] = e; m[4] = f; m[5] = h; mat_mul(g->ctm, m, g->ctm); }
        else if (kw(d, (KS + 1281))) path_curve(d, a, b, c, e, f, h);
        return;
    case 'w': if (kw(d, (KS + 1283))) g->lw = h; return;
    case 'm': if (kw(d, (KS + 1285))) path_move(d, f, h); return;
    case 'l': if (kw(d, (KS + 1287))) path_line(d, f, h); return;
    case 'v': if (kw(d, (KS + 1289))) path_curve(d, d->curx, d->cury, c, e, f, h); return;
    case 'y': if (kw(d, (KS + 1291))) path_curve(d, c, e, f, h, f, h); return;
    case 'h': if (kw(d, (KS + 1293))) path_close(d); return;
    case 'r':
        if (kw(d, (KS + 1295))) path_rect(d, c, e, f, h);
        else if (kw(d, (KS + 1298))) set_color(d, 0, 3);
        return;
    case 'R': if (kw(d, (KS + 1301))) set_color(d, 1, 3); return;
    case 'g': if (kw(d, (KS + 1304))) set_color(d, 0, 1); return;
    case 'G': if (kw(d, (KS + 1147))) set_color(d, 1, 1); return;
    case 'k': if (kw(d, (KS + 1306))) set_color(d, 0, 4); return;
    case 'K': if (kw(d, (KS + 1308))) set_color(d, 1, 4); return;
    case 's':
        if (kw(d, (KS + 1310))) { path_close(d); do_stroke(d); path_reset(d); }
        else if (kw(d, (KS + 1312)) || kw(d, (KS + 1315))) set_color(d, 0, 0);
        return;
    case 'S':
        if (kw(d, (KS + 1319))) { do_stroke(d); path_reset(d); }
        else if (kw(d, (KS + 1321)) || kw(d, (KS + 1324))) set_color(d, 1, 0);
        return;
    case 'f': case 'F':
        do_fill(d, (pu8)(d->tok.len > 1)); path_reset(d); return;
    case 'B':
        if (kw(d, (KS + 1328)) || kw(d, (KS + 1330))) { do_fill(d, (pu8)(d->tok.len > 1)); do_stroke(d); path_reset(d); }
        else if (kw(d, (KS + 1333))) { set_tm(d, FXONE, 0, 0, FXONE, 0, 0); }
        else if (kw(d, (KS + 1336))) inline_image(d);
        return;
    case 'b':
        if (kw(d, (KS + 1339)) || kw(d, (KS + 1341))) { path_close(d); do_fill(d, (pu8)(d->tok.len > 1)); do_stroke(d); path_reset(d); }
        return;
    case 'n': if (kw(d, (KS + 1344))) path_reset(d); return;
    case 'E': if (kw(d, (KS + 1346))) run_flush(d); return;
    case 'D': if (kw(d, (KS + 1349)) && n > 0 && d->ops[n - 1].type == T_NAME)
                  do_xobject(d, d->opstr + d->ops[n - 1].soff, d->ops[n - 1].slen);
              return;
    case 'T':
        if (d->tok.len != 2 && !kw(d, (KS + 1352))) return;
        switch (d->tok.s[1]) {
        case 'c': g->tc = h; return;
        case 'w': g->tw = h; return;
        case 'z': g->th = h / 100; return;
        case 'L': g->tl = h; return;
        case 's': g->ts = h; return;
        case 'r': g->tr = (pu8)(h >> 16); return;
        case 'd': td(d, f, h); return;
        case 'D': g->tl = -h; td(d, f, h); return;
        case 'm': set_tm(d, a, b, c, e, f, h); return;
        case '*': td(d, 0, -g->tl); return;
        case 'f': {
            pi16 i;
            g->fs = h; g->font = -1;
            if (n >= 2 && d->ops[n - 2].type == T_NAME) {
                const pu8 PFAR *nm = d->opstr + d->ops[n - 2].soff;
                pu16 nl = d->ops[n - 2].slen;
                for (i = d->nfonts - 1; i >= 0; i--) {
                    pu16 k; pi16 eq = 1;
                    for (k = 0; k < nl && eq; k++) if (d->fonts[i].res[k] != nm[k]) eq = 0;
                    if (eq && (nl >= sizeof(d->fonts[i].res) - 1 || d->fonts[i].res[nl] == 0)) { g->font = i; break; }
                }
            }
            return;
        }
        case 'j':
            if (n > 0 && d->ops[n - 1].type == T_STR) show_string(d, d->opstr + d->ops[n - 1].soff, d->ops[n - 1].slen);
            return;
        case 'J': {
            pi16 i, start = n;
            for (i = n - 1; i >= 0; i--) if (d->ops[i].type == T_ABEG) { start = i + 1; break; }
            for (i = start; i < n; i++) {
                if (d->ops[i].type == T_STR) show_string(d, d->opstr + d->ops[i].soff, d->ops[i].slen);
                else if (d->ops[i].type == T_NUM) {
                    fx tx = fx_mul(fx_mul(-d->ops[i].num / 1000, g->fs), g->th);
                    d->tm[4] += fx_mul(tx, d->tm[0]);
                    d->tm[5] += fx_mul(tx, d->tm[1]);
                }
            }
            return;
        }
        }
        return;
    case '\'':
        td(d, 0, -g->tl);
        if (n > 0 && d->ops[n - 1].type == T_STR) show_string(d, d->opstr + d->ops[n - 1].soff, d->ops[n - 1].slen);
        return;
    case '"':
        g->tw = opn(d, n - 3); g->tc = opn(d, n - 2);
        td(d, 0, -g->tl);
        if (n > 0 && d->ops[n - 1].type == T_STR) show_string(d, d->opstr + d->ops[n - 1].soff, d->ops[n - 1].slen);
        return;
    }
}

static void push_op(PdfDoc PFAR *d, pu8 type)
{
    Op PFAR *o;
    if (d->nops >= MAXOPS) {               /* keep the newest operands */
        pi16 i;
        for (i = 1; i < MAXOPS; i++) d->ops[i - 1] = d->ops[i];
        d->nops--;
    }
    o = &d->ops[d->nops++];
    o->type = type; o->num = d->tok.num; o->isint = d->tok.isint; o->soff = 0; o->slen = 0;
    if (type == T_STR || type == T_NAME) {
        pu16 l = d->tok.len;
        if (d->opstrtop + l > sizeof(d->opstr)) l = (pu16)(sizeof(d->opstr) - d->opstrtop);
        p_memcpy(d->opstr + d->opstrtop, d->tok.s, l);
        o->soff = d->opstrtop; o->slen = l;
        d->opstrtop += l;
    }
}

static void run_content(PdfDoc PFAR *d)
{
    pi16 t, depth;
        d->nops = 0; d->opstrtop = 0;
        for (;;) {
            t = clex(d);
            if (t == T_EOF) break;
            if (d->trunc) break;
            switch (t) {
            case T_NUM: push_op(d, T_NUM); break;
            case T_NAME: push_op(d, T_NAME); break;
            case T_STR: push_op(d, T_STR); break;
            case T_ABEG: push_op(d, T_ABEG); break;
            case T_AEND: break;
            case T_DBEG:                         /* inline property dict */
                depth = 1;
                while (depth > 0) {
                    t = clex(d);
                    if (t == T_EOF) break;
                    if (t == T_DBEG) depth++;
                    else if (t == T_DEND) depth--;
                }
                push_op(d, T_NAME);
                break;
            case T_KW:
                exec_op(d);
                d->nops = 0; d->opstrtop = 0;
                d->atop = 0;
                break;
            default: break;
            }
        }
    }

pi16 PAPI pdf_render(pu8 PFAR *work, pi16 page, pu16 scale256, pu8 PFAR *dl, pu16 dlsize, pu8 PFAR *truncated)
{
    PdfDoc PFAR *d = (PdfDoc PFAR *)work;
    pi16 e, w, h;
    fx s, x0, y0, x1, y1;
    *truncated = 0;
    if (dlsize < 16) return PDF_E_NOMEM;
    e = find_page(d, page);
    if (e) return e;
    d->atop = 0;
    d->dl = dl; d->dlsize = dlsize; d->dlpos = 0; d->trunc = 0;
    d->tc = (pu8)((scale256 & PDF_TRUECOLOR) != 0); d->nct = 0; d->ctab = dl + dlsize; d->jleft = 2;
    d->extk = 0; d->extpos = 0;
    scale256 &= ~PDF_TRUECOLOR;
    s = (fx)scale256 << 8;
    x0 = d->mbox[0]; y0 = d->mbox[1]; x1 = d->mbox[2]; y1 = d->mbox[3];
    w = (pi16)((fx_mul(x1 - x0, s) + 0x8000L) >> 16);
    h = (pi16)((fx_mul(y1 - y0, s) + 0x8000L) >> 16);
    {
        fx PFAR *m = d->base;
        switch (d->rot) {
        default:  m[0] = s; m[1] = 0; m[2] = 0; m[3] = -s; m[4] = -fx_mul(s, x0); m[5] = fx_mul(s, y1); break;
        case 90:  m[0] = 0; m[1] = s; m[2] = s; m[3] = 0;  m[4] = -fx_mul(s, y0); m[5] = -fx_mul(s, x0); break;
        case 180: m[0] = -s; m[1] = 0; m[2] = 0; m[3] = s; m[4] = fx_mul(s, x1); m[5] = -fx_mul(s, y0); break;
        case 270: m[0] = 0; m[1] = -s; m[2] = -s; m[3] = 0; m[4] = fx_mul(s, y1); m[5] = fx_mul(s, x1); break;
        }
        if (d->rot == 90 || d->rot == 270) { pi16 tt = w; w = h; h = tt; }
    }
    d->devw = w; d->devh = h;
    /* graphics state */
    d->gsp = 0;
    p_memset(&d->gs[0], 0, sizeof(GState));
    p_memcpy(d->gs[0].ctm, d->base, sizeof(d->base));
    d->gs[0].fillc = 0; d->gs[0].strokec = 0; d->gs[0].lw = FXONE;
    d->gs[0].font = -1; d->gs[0].th = FXONE;
    set_tm(d, FXONE, 0, 0, FXONE, 0, 0);
    path_reset(d);
    d->nrun = 0; d->nopen = 0;
    d->njob = 0; d->bm = 0;

    load_page_fonts(d);

    /* collect content streams */
    {
        pu16 o = load_obj(d, d->pageobj), c, k;
        d->ncont = 0; d->curcont = 0;
        c = dict_get(d, o, (KS + 1355));
        if (c != NIL && NODE(d, c)->type == N_REF) {
            pi32 num = NODE(d, c)->val;
            pu16 r = load_obj(d, num);
            if (r != NIL && NODE(d, r)->type == N_ARR) c = r;
            else d->cont[d->ncont++] = num;
        }
        if (c != NIL && NODE(d, c)->type == N_ARR)
            for (k = NODE(d, c)->child; k != NIL && d->ncont < MAXCONT; k = NODE(d, k)->next)
                if (NODE(d, k)->type == N_REF) d->cont[d->ncont++] = NODE(d, k)->val;
        d->atop = 0;
    }
    if (next_content(d)) run_content(d);
    d->incontent = 0;
    run_flush(d);
    run_jobs(d, 0, 0);
    d->dl[d->dlpos++] = DL_END;
    *truncated = d->trunc;
    return (pi16)d->dlpos;
}

/* External blocks for images (each size bytes, laid out like the display
 * list buffer); used by the following pdf_render calls. n = 0: none. */
void PAPI pdf_set_ext(pu8 PFAR *work, pu8 PFAR * PFAR *ext, pu16 n, pu16 size)
{
    PdfDoc PFAR *d = (PdfDoc PFAR *)work;
    pu16 i;
    if (n > MAXEXT) n = MAXEXT;
    for (i = 0; i < n; i++) d->ext[i] = ext[i];
    d->next = (pu8)n; d->extsize = size;
}

/* ---- lmath.c ---- */
/* SPDX-License-Identifier: MIT
 * 32-bit integer helpers that Open Watcom's 16-bit code calls implicitly
 * (no C runtime is linked). Register contracts as used by wcc -ms:
 *   __U4M/__I4M: DX:AX * CX:BX -> DX:AX
 *   __U4D/__I4D: DX:AX / CX:BX -> quotient DX:AX, remainder CX:BX
 * All other registers are preserved. Near routines in segment ALGORITHM. */
#ifdef __WATCOMC__
#pragma aux __U4M "*"
#pragma aux __I4M "*"
#pragma aux __U4D "*"
#pragma aux __I4D "*"

void __declspec(naked) __U4M(void)
{
    _asm {
        push si
        push di
        mov  si, ax
        mov  di, dx
        mul  cx
        xchg ax, di
        mul  bx
        add  di, ax
        mov  ax, si
        mul  bx
        add  dx, di
        pop  di
        pop  si
        ret
    }
}

void __declspec(naked) __I4M(void)
{
    _asm { jmp __U4M }
}

void __declspec(naked) __U4D(void)
{
    _asm {
        test cx, cx
        jnz  long_div
        push si
        mov  si, ax
        mov  ax, dx
        xor  dx, dx
        div  bx
        xchg ax, si
        div  bx
        mov  bx, dx
        xor  cx, cx
        mov  dx, si
        pop  si
        ret
    long_div:
        push si
        push di
        push bp
        xor  si, si
        xor  di, di
        mov  bp, 32
    next_bit:
        shl  ax, 1
        rcl  dx, 1
        rcl  di, 1
        rcl  si, 1
        cmp  si, cx
        jb   no_sub
        ja   do_sub
        cmp  di, bx
        jb   no_sub
    do_sub:
        sub  di, bx
        sbb  si, cx
        inc  ax
    no_sub:
        dec  bp
        jnz  next_bit
        mov  bx, di
        mov  cx, si
        pop  bp
        pop  di
        pop  si
        ret
    }
}

void __declspec(naked) __I4D(void)
{
    _asm {
        push di
        xor  di, di
        test dx, dx
        jns  dividend_pos
        neg  dx
        neg  ax
        sbb  dx, 0
        xor  di, 3
    dividend_pos:
        test cx, cx
        jns  divisor_pos
        neg  cx
        neg  bx
        sbb  cx, 0
        xor  di, 1
    divisor_pos:
        call __U4D
        test di, 1
        jz   quot_done
        neg  dx
        neg  ax
        sbb  dx, 0
    quot_done:
        test di, 2
        jz   rem_done
        neg  cx
        neg  bx
        sbb  cx, 0
    rem_done:
        pop  di
        ret
    }
}
#endif
