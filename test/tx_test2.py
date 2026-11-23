# text_refill end to end: DL block, ObjMessage stand-in
import sys
from emu import Geos, DG
from unicorn.x86_const import *
for f in sys.argv[1:]:
    g = Geos('/tmp')
    orig = g.hook; sent = []
    def hook(mu, o, g=g):
        if o == 150: return
        if o == 169:
            ax=g.reg(UC_X86_REG_AX); dx=g.reg(UC_X86_REG_DX); bp=g.reg(UC_X86_REG_BP)
            t=g.rd(dx,bp,20000); sent.append((hex(ax),hex(bp),t[:t.index(0)])); return
        orig(mu, o)
    g.hook = hook
    dl = open(f,'rb').read()[4:]
    dseg = 0x7000
    g.wr(dseg, 0, bytes(64000)); g.wr(dseg, 48, dl)
    g.setv(0xA4, dseg); g.setv(0xAA, 0); g.setv(0xF2, 0)
    g.call('text_refill')
    for a,b,t in sent: print(f, a, b, len(t), t[:60].decode('mac_roman').replace('\r','|'))
