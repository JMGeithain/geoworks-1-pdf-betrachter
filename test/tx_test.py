import sys
from emu import Geos, DG
for f in sys.argv[1:]:
    g = Geos('/tmp')
    dl = open(f,'rb').read()[4:]
    dseg = 0x7000
    g.wr(dseg, 0, bytes(48)); g.wr(dseg, 48, dl + b'\0')
    g.setv(0xA4, dseg)
    tseg = 0x9000; g.wr(tseg, 0, bytes(16384))
    g.call('text_collect', es=tseg, di=0, cx=16000)
    t = g.rd(tseg, 0, 16384); t = t[:t.index(0)]
    print('==== %s: %d Zeichen' % (f, len(t)))
    print(t.replace(b'\r', b'\n').decode('mac_roman')[:1500])
