# replay of a display list with an external (unlocked) image block -> replay.png
import sys
from emu import Geos
W,H=760,1100
g=Geos('/tmp',W,H)
g.canvas.paste((255,255,255),(0,0,W,H))
base=sys.argv[1]
dl=open(base+'-0.dl','rb').read()[4:]
dseg=0x7000; g.wr(dseg,0,bytes(64000)); g.wr(dseg,48,dl)
g.wr(dseg,64000-4096-960,open(base+'-0.ct','rb').read())
e=open(base+'-0.e0','rb').read()
eseg=0x8000; g.wr(eseg,0,e[:64000])
g.handles[0x500]=eseg
g.setw(dseg,32,0x500); g.setw(dseg,40,0)          # handle only: unlocked block
g.wr(dseg,27,b'\x01')
for off,v in ((0xA4,dseg),(0xAE,0),(0xB0,0),(0xC0,H),(0xBE,W)): g.setv(off,v)
g.call('replay',di=0x77)
g.canvas.save('replay.png'); print('calls',{k:v for k,v in g.calls.items() if k in (5,6,326)})
