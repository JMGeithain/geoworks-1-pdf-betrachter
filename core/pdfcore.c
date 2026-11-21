/* SPDX-License-Identifier: MIT
 * pdfcore: file access, lexer, object parser, xref, inflate, page tree. */
#include "pdfint.h"

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
static const pu16 LBASE[29] = {3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258};
static const pu8  LEXT[29]  = {0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0};
static const pu16 DBASE[30] = {1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577};
static const pu8  DEXT[30]  = {0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13};
static const pu8  CLORD[19] = {16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15};

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
        if (KW(t, "true") || KW(t, "false")) {
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
        if (t->type == T_KW && KW(t, "ID")) return o;
        if (t->type != T_NAME) continue;
        c = node_str(d, N_NAME, t);
        if (c == NIL) return NIL;
        if (last == NIL) NODE(d, o)->child = c; else NODE(d, last)->next = c;
        last = c;
        lex(s, t);
        if (t->type == T_KW && KW(t, "ID")) { c = new_node(d, N_NULL); NODE(d, last)->next = c; return o; }
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
    lex(&d->lk, t); if (!KW(t, "obj")) return NIL;
    o = parse_obj(d, &d->lk);
    if (o == NIL) return NIL;
    if (NODE(d, o)->type == N_DICT) {
        lex(&d->lk, t);
        if (KW(t, "stream")) {
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
    pi32 l = node_int(d, dict_getr(d, sd, "Length"), -1);
    pi16 bad = 0;
    *flags = 0;
    f = dict_getr(d, sd, "Filter");
    if (f != NIL && NODE(d, f)->type == N_ARR) n = NODE(d, f)->len;
    for (i = 0; f != NIL && i < n; i++) {
        pu16 x = NODE(d, f)->type == N_ARR ? resolve(d, arr_at(d, f, i)) : f;
        if (node_is(d, x, N_NAME, "FlateDecode") || node_is(d, x, N_NAME, "Fl")) {
            if (*flags & SF_FLATE) bad = 1;
            *flags |= SF_FLATE;
        } else if ((node_is(d, x, N_NAME, "ASCII85Decode") || node_is(d, x, N_NAME, "A85")) && i == 0 && !*flags)
            *flags |= SF_A85;
        else if ((node_is(d, x, N_NAME, "ASCIIHexDecode") || node_is(d, x, N_NAME, "AHx")) && i == 0 && !*flags)
            *flags |= SF_AHX;
        else if ((node_is(d, x, N_NAME, "DCTDecode") || node_is(d, x, N_NAME, "DCT")) && i == n - 1)
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
    n = (pu16)node_int(d, dict_getr(d, sd, "N"), 0);
    first = node_int(d, dict_getr(d, sd, "First"), -1);
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
    lex(&d->lk, t); if (!KW(t, "obj")) return PDF_E_XREF;
    sd = parse_obj(d, &d->lk);
    if (sd == NIL || NODE(d, sd)->type != N_DICT) return PDF_E_XREF;
    lex(&d->lk, t);
    if (!KW(t, "stream")) return PDF_E_XREF;
    c = (pu16)src_get(&d->lk);
    if (c == '\r' && src_peek(&d->lk) == '\n') src_get(&d->lk);
    d->streamoff = src_tell(&d->lk);
    if (dict_get(d, sd, "Encrypt") != NIL) return PDF_E_ENCRYPT;
    if (d->root < 0) {
        pu16 r = dict_get(d, sd, "Root");
        if (r != NIL && NODE(d, r)->type == N_REF) d->root = NODE(d, r)->val;
    }
    wa = dict_getr(d, sd, "W");
    for (i = 0; i < 3; i++) w[i] = (pi16)node_int(d, resolve(d, arr_at(d, wa, (pu16)i)), 0);
    rowlen = w[0] + w[1] + w[2];
    if (rowlen <= 0 || rowlen > 32 || w[0] > 4 || w[1] > 4 || w[2] > 4) return PDF_E_XREF;
    size = node_int(d, dict_getr(d, sd, "Size"), 0);
    prev = node_int(d, dict_getr(d, sd, "Prev"), -1);
    dp = dict_getr(d, sd, "DecodeParms");
    if (dp != NIL && NODE(d, dp)->type == N_ARR) dp = resolve(d, arr_at(d, dp, 0));
    if (dp != NIL) {
        pred = (pi16)node_int(d, dict_getr(d, dp, "Predictor"), 0);
        cols = (pi16)node_int(d, dict_getr(d, dp, "Columns"), 1);
        if (pred >= 10 && cols != rowlen) return PDF_E_XREF;
    }
    if (stream_info(d, sd, &len, &flags) != 0) return PDF_E_FILTER;
    src_stream(d, &s, d->rawbuf, d->cbuf, CBUF_SIZE, d->streamoff, len, flags);
    for (i = 0; i < 32; i++) prow[i] = 0;
    ix = dict_getr(d, sd, "Index");
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
        if (node_is(d, dict_get(d, o, "Type"), N_NAME, "Catalog")) d->root = num;
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
    if (!KW(t, "xref")) return PDF_E_XREF;
    for (;;) {
        pi32 start, count;
        lex(&d->lk, t);
        if (KW(t, "trailer")) break;
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
    if (dict_get(d, tr, "Encrypt") != NIL) return PDF_E_ENCRYPT;
    if (d->root < 0) {
        pu16 r = dict_get(d, tr, "Root");
        if (r != NIL && NODE(d, r)->type == N_REF) d->root = NODE(d, r)->val;
    }
    prev = node_int(d, dict_get(d, tr, "Prev"), -1);
    {
        pi32 xs = node_int(d, dict_get(d, tr, "XRefStm"), -1);     /* hybrid file */
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
        if (tail[i] == 's' && p_streq(tail + i, 9, "startxref")) {
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
        pg = dict_get(d, r, "Pages");
        if (pg == NIL || NODE(d, pg)->type != N_REF) return PDF_E_XREF;
        d->pages = NODE(d, pg)->val;
        r = load_obj(d, d->pages);
        d->npages = (pi16)node_int(d, dict_getr(d, r, "Count"), 0);
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
        if (dict_get(d, o, "Resources") != NIL) resobj = node;
        if (dict_get(d, o, "MediaBox") != NIL) get_box(d, dict_get(d, o, "MediaBox"), box);
        if (dict_get(d, o, "Rotate") != NIL) rot = (pi16)node_int(d, dict_getr(d, o, "Rotate"), 0);
        kids = dict_getr(d, o, "Kids");
        if (kids == NIL || NODE(d, kids)->type != N_ARR) {
            o = load_obj(d, node);
            if (dict_get(d, o, "CropBox") != NIL) {
                fx cb[4]; cb[0] = box[0]; cb[1] = box[1]; cb[2] = box[2]; cb[3] = box[3];
                get_box(d, dict_get(d, o, "CropBox"), cb);
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
            if (node_is(d, dict_get(d, ko, "Type"), N_NAME, "Pages") || dict_get(d, ko, "Kids") != NIL)
                cnt = node_int(d, dict_getr(d, ko, "Count"), 1);
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
    case PDF_E_IO:      m = "Lesefehler"; break;
    case PDF_E_NOTPDF:  m = "Keine PDF-Datei"; break;
    case PDF_E_XREFSTM: m = "PDF 1.5+ (XRef-Stream) noch nicht unterstuetzt"; break;
    case PDF_E_XREF:    m = "Querverweistabelle beschaedigt"; break;
    case PDF_E_ENCRYPT: m = "Verschluesselte PDF"; break;
    case PDF_E_SYNTAX:  m = "Syntaxfehler"; break;
    case PDF_E_NOMEM:   m = "Zu wenig Speicher"; break;
    case PDF_E_PAGE:    m = "Seite nicht gefunden"; break;
    case PDF_E_FILTER:  m = "Unbekannter Stream-Filter"; break;
    case PDF_E_INFLATE: m = "Komprimierte Daten defekt"; break;
    default:            m = "Fehler"; break;
    }
    for (i = 0; i + 1 < size && m[i]; i++) buf[i] = m[i];
    if (size) buf[i] = 0;
}
