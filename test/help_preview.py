# Renders the help page (paint_help) and the status bar in the emulator
# -> help.png, help2.png (scrolled down). python3 help_preview.py
import sys
from emu import Geos, DG
VW, VH = 900, 600
g = Geos('/tmp', VW + 15, VH + 15)
g.draw_text = True
lseg = 0x6000
g.wr(lseg, 0, bytes(2048))
for off, v in ((0xD0, lseg), (0xBE, VW), (0xC0, VH), (0xEC, VW + 15), (0xEE, VH + 15), (0xD4, 3), (0xBC, 1)):
    g.setv(off, v)
def shot(name):
    g.canvas.paste((85, 85, 85), (0, 0, VW + 15, VH + 15))
    g.call('paint_help', di=0x77)
    g.canvas.save(name)
shot('help.png')
for k in range(3):
    g.call('help_key', cx=ord('3'))
shot('help2.png')
print('ok')
