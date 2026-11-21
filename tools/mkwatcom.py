# Produce one Watcom translation unit (build/pdfcore_w.c) from the portable core:
#  * all string literals move into a table in the PDFTABS resource
#  * all static const tables move into PDFTABS too
# so the library never touches DS/DGROUP (GEOS calls it with the caller's DS).
import re, sys, os
root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
src = ""
for f in ["pdfcore.c", "pdfrend.c", "lmath.c"]:
    t = open(os.path.join(root, "core", f)).read()
    t = t.replace('#include "pdftabs.h"', open(os.path.join(root, "core", "pdftabs.h")).read())
    if f != "pdfcore.c": t = t.replace('#include "pdfint.h"', "")
    src += "\n/* ---- %s ---- */\n" % f + t

out = []; strings = {}; blob = []
def key(s):
    if s not in strings:
        strings[s] = sum(len(b) + 1 for b in blob); blob.append(s)
    return strings[s]
i = 0; n = len(src)
while i < n:
    c = src[i]
    if src.startswith("/*", i):
        j = src.index("*/", i) + 2; out.append(src[i:j]); i = j; continue
    if src.startswith("//", i):
        j = src.index("\n", i); out.append(src[i:j]); i = j; continue
    if c == "'":
        j = i + 1
        while src[j] != "'": j += 2 if src[j] == "\\" else 1
        out.append(src[i:j+1]); i = j + 1; continue
    if c == '"':
        line_start = src.rfind("\n", 0, i) + 1
        if src[line_start:i].lstrip().startswith("#"):
            j = src.index("\n", i); out.append(src[i:j]); i = j; continue
        stmt = src[max(src.rfind(";", 0, i), src.rfind("}", 0, i)) + 1:i]
        if "static const char" in stmt and "=" in stmt and "(" not in stmt:
            m = re.match(r'("(?:[^"\\]|\\.)*"\s*)+', src[i:])
            out.append(src[i:i+m.end()]); i += m.end(); continue
        # concatenate adjacent literals
        parts = []
        while True:
            j = i + 1; buf = ""
            while src[j] != '"':
                if src[j] == "\\":
                    e = src[j+1]
                    if e == "0": buf += "\0"; j += 2; continue
                    buf += {"n":"\n","t":"\t","r":"\r","\\":"\\",'"':'"'}[e]; j += 2; continue
                buf += src[j]; j += 1
            parts.append(buf); i = j + 1
            m = re.match(r'\s*"', src[i:])
            if not m: break
            i += m.end() - 1
        s = "".join(parts)
        out.append("(KS + %d)" % key(s)); continue
    out.append(c); i += 1
body = "".join(out)
# move static const tables into PDFTABS
body = re.sub(r"static const (unsigned short|unsigned char|char|pu8|pu16|pi16|pi8|short)\s+(\w+)\[",
              r"static const \1 TABSEG \2[", body)
def esc(b):
    r = ""
    for ch in b:
        o = ord(ch)
        r += ch if 32 <= o < 127 and ch not in '"\\?' else "\\%03o" % o
    return r
ks = "".join(b + "\0" for b in blob)
lines = ['  "%s"' % esc(ks[k:k+60]) for k in range(0, len(ks), 60)]
head = ('#define TABSEG __based(__segname("PDFTABS"))\n'
        '#include "pdfint.h"\n'
        "static const char TABSEG KS[] =\n" + "\n".join(lines) + ";\n")
body = body.replace('#include "pdfint.h"', "", 1)
os.makedirs(os.path.join(root, "build"), exist_ok=True)
open(os.path.join(root, "build", "pdfcore_w.c"), "w").write(head + body)
print("strings:", len(blob), "bytes:", len(ks))

# NOTE: after compiling, tools/checkseg.sh rejects any "mov ..,ds": a table
# pointer built from DS would read the caller's data segment.
