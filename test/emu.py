# Test harness (from GeoMaler): runs PDF-Betrachter code in a 8086 emulator (Unicorn, 16-bit mode)
# with Python stand-ins for the GeoWorks kernel calls it uses.
import os, re, struct, sys
from unicorn import Uc, UC_ARCH_X86, UC_MODE_16, UC_HOOK_CODE, UC_HOOK_INTR
from unicorn.x86_const import *
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
CS, DG, SS = 0x1000, 0x3000, 0x3100
STOP = 0xFFF0
EGA = [(0,0,0),(0,0,170),(0,170,0),(0,170,170),(170,0,0),(170,0,170),(170,85,0),(170,170,170),
       (85,85,85),(85,85,255),(85,255,85),(85,255,255),(255,85,85),(255,85,255),(255,255,85),(255,255,255)]

def symbols():
    s = {}
    for line in open(os.path.join(HERE, 'g.map')):
        m = re.match(r'\s+([0-9A-F]+)\s+([0-9A-F]+)\s+(\S+)$', line)
        if m: s[m.group(3)] = int(m.group(1), 16)
    return s

class Geos:
    def __init__(self, files_dir, vw=1024, vh=700, tc_ini=True):
        self.sym = symbols()
        raw = open(os.path.join(HERE, 'g.bin'), 'rb').read()
        ft = raw.rindex(b'G1META01')
        clen, mlen, rlen = struct.unpack('<HHH', raw[ft + 8:ft + 14])
        code = bytearray(raw[:clen])
        rel = raw[clen + mlen:clen + mlen + rlen]
        for i in range(0, len(rel), 4):
            typ, imp, site = struct.unpack('<BBH', rel[i:i + 4])
            c = site - 1
            if code[c] != 0x9a: continue
            ordn = code[c + 1:c + 3]
            code[c:c + 5] = (b'\xcd\x80' if imp == 0 else b'\xcd\x81') + ordn + b'\x90'   # int 80h/81h, ordinal, nop
        self.mu = mu = Uc(UC_ARCH_X86, UC_MODE_16)
        mu.mem_map(0, 0x100000)
        mu.mem_write(CS * 16, bytes(code))
        mu.mem_write(CS * 16 + STOP, b'\xf4')        # hlt
        self.next_seg = 0x5000
        self.handles = {}
        self.files = {}
        self.fdir = files_dir
        self.vw, self.vh = vw, vh
        self.canvas = Image.new('RGB', (vw, vh), (1, 2, 3))
        self.area = (0, 0, 0)
        self.calls = {}
        self.ini = b'[screen 0]\r\ndevice = VGA 64K colors\r\n' if tc_ini else b'[screen 0]\r\ndevice = VGA: 640x480 16-color\r\n'
        mu.hook_add(UC_HOOK_INTR, self.intr)
        mu.hook_add(UC_HOOK_CODE, lambda mu, a, sz, u: mu.emu_stop(), begin=CS * 16 + STOP, end=CS * 16 + STOP)

    # ---- memory helpers ----
    def rd(self, seg, off, n): return bytes(self.mu.mem_read(seg * 16 + off, n))
    def wr(self, seg, off, b): self.mu.mem_write(seg * 16 + off, bytes(b))
    def w(self, seg, off): return struct.unpack('<H', self.rd(seg, off, 2))[0]
    def setw(self, seg, off, v): self.wr(seg, off, struct.pack('<H', v & 0xffff))
    def cstr(self, seg, off):
        b = self.rd(seg, off, 200); return b[:b.index(0)].decode('latin1')
    def reg(self, r): return self.mu.reg_read(r)
    def setreg(self, r, v): self.mu.reg_write(r, v & 0xffff)
    def cf(self, on):
        f = self.mu.reg_read(UC_X86_REG_EFLAGS)
        self.mu.reg_write(UC_X86_REG_EFLAGS, (f | 1) if on else (f & ~1))

    # ---- kernel stand-ins ----
    def intr(self, mu, intno, ud):
        assert intno in (0x80, 0x81)
        ip = self.reg(UC_X86_REG_IP)
        o = self.w(CS, ip)
        if intno == 0x80: self.hook(mu, o)
        mu.reg_write(UC_X86_REG_IP, ip + 3)

    def hook(self, mu, o):
        self.calls[o] = self.calls.get(o, 0) + 1
        R = self.reg
        ax, bx, cx, dx, si, di, ds = (R(x) for x in (UC_X86_REG_AX, UC_X86_REG_BX, UC_X86_REG_CX, UC_X86_REG_DX, UC_X86_REG_SI, UC_X86_REG_DI, UC_X86_REG_DS))
        cf = False
        if o == 0:      # MemAlloc
            seg = self.next_seg; self.next_seg += (ax + 15) // 16 + 1
            self.wr(seg, 0, bytes(ax)); h = 0x100 + len(self.handles) * 16
            self.handles[h] = seg
            self.setreg(UC_X86_REG_AX, seg); self.setreg(UC_X86_REG_BX, h)
        elif o == 330 and getattr(self, 'draw_text', False):
            from PIL import ImageDraw, ImageFont
            t = self.rd(ds, si, 200); t = t[:t.index(0)].decode('mac_roman')
            size = getattr(self, 'fsize', 10)
            bold = getattr(self, 'bold', False)
            f = ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSans%s.ttf' % ('-Bold' if bold else ''), size + 1)
            ImageDraw.Draw(self.canvas).text((ax, bx - 1), t, fill=getattr(self, 'tcol', (0, 0, 0)), font=f)
        elif o == 359: self.tcol = EGA[ax & 15]
        elif o == 366: self.fsize = dx
        elif o == 362: self.bold = bool(ax & 0x20)
        elif o == 489:   # ObjCallSuperNoLock: class pointer ES:DI must be dgroup:0x40
            assert R(UC_X86_REG_ES) == DG and di == 0x40, 'ObjCallSuperNoLock mit ES=%04x DI=%04x' % (R(UC_X86_REG_ES), di)
            self.supers = getattr(self, 'supers', 0) + 1
        elif o == 76: self.cwd = ''
        elif o == 51: self.cwd = self.cstr(ds, dx) + '\\'
        elif o == 73: self.dirs = getattr(self, 'dirs', []) + [getattr(self, 'cwd', '')]
        elif o == 74: self.cwd = self.dirs.pop()
        elif o in (2, 5, 6, 310, 446, 447, 212, 346, 316, 359, 330, 366, 362):
            if o == 5: self.setreg(UC_X86_REG_AX, self.handles.get(bx, 0))
        elif o == 211: self.setreg(UC_X86_REG_DI, 0x77)
        elif o == 440:
            self.setreg(UC_X86_REG_AX, 0); self.setreg(UC_X86_REG_BX, 0)
            self.setreg(UC_X86_REG_CX, self.vw - 1); self.setreg(UC_X86_REG_DX, self.vh - 1)
        elif o in (52, 53):  # FileOpen / FileCreate
            name = self.cstr(ds, dx)
            if not name.startswith('\\'): name = getattr(self, 'cwd', '') + name
            if o == 52 and name.upper() == 'GEOS.INI':
                data = bytearray(self.ini)
            else:
                path = os.path.join(self.fdir, name.replace('\\', '_'))
                if o == 52:
                    if not os.path.exists(path): cf = True; self.setreg(UC_X86_REG_AX, 2)
                    else: data = bytearray(open(path, 'rb').read())
                else:
                    data = bytearray(); self.created = path
                    assert ax == 0x00c1, hex(ax)
            if not cf:
                h = 0x40 + len(self.files) * 2
                self.files[h] = {'data': data, 'pos': 0, 'name': name,
                                 'path': None if name.upper() == 'GEOS.INI' else os.path.join(self.fdir, name.replace('\\', '_'))}
                self.setreg(UC_X86_REG_AX, h)
        elif o == 54:
            f = self.files.pop(bx)
            if f['path'] and getattr(self, 'created', None) == f['path']:
                open(f['path'], 'wb').write(f['data'])
        elif o == 58:   # FileRead
            f = self.files[bx]; d = f['data'][f['pos']:f['pos'] + cx]
            self.wr(ds, dx, d); f['pos'] += len(d)
            if len(d) < cx: cf = True; self.setreg(UC_X86_REG_CX, len(d))
        elif o == 59:   # FileWrite
            f = self.files[bx]; d = self.rd(ds, dx, cx)
            f['data'][f['pos']:f['pos'] + cx] = d; f['pos'] += cx
        elif o == 60:   # FilePos
            f = self.files[bx]; off = (cx << 16 | dx)
            if off & 0x80000000: off -= 1 << 32
            base = {0: 0, 1: f['pos'], 2: len(f['data'])}[ax & 0xff]
            f['pos'] = max(0, base + off)
            self.setreg(UC_X86_REG_AX, f['pos'] & 0xffff); self.setreg(UC_X86_REG_DX, f['pos'] >> 16)
        elif o == 65:   # FileEnum: no entries
            sp = R(UC_X86_REG_SP); self.setreg(UC_X86_REG_SP, sp + 0x30)
            self.setreg(UC_X86_REG_BX, 0); self.setreg(UC_X86_REG_DX, 0)
        elif o == 75:   # FileGetCurrentPath
            self.wr(ds, si, b'\\GEOWORKS\\WORLD\0'); self.setreg(UC_X86_REG_BX, 1)
        elif o == 355:
            ah = ax >> 8
            self.area = (ax & 0xff, bx & 0xff, bx >> 8) if ah == 0x80 else EGA[ax & 15]
        elif o == 336:
            x0, y0, x1, y1 = [v - 0x10000 if v & 0x8000 else v for v in (ax, bx, cx, dx)]
            x0, y0 = max(0, x0), max(0, y0); x1, y1 = min(self.vw, x1), min(self.vh, y1)
            if x1 > x0 and y1 > y0:
                self.canvas.paste(self.area, (x0, y0, x1, y1))
        elif o == 326:  # GrDrawBitmap, 24 bit uncompacted only
            wd, ht, comp, typ = struct.unpack('<HHBB', self.rd(ds, si, 6))
            if (comp, typ) != (0, 3) or ht != 1:
                self.other_bitmaps = getattr(self, 'other_bitmaps', 0) + 1; self.cf(False); return
            px = self.rd(ds, si + 6, wd * 3)
            for i in range(wd):
                x = ax + i
                if 0 <= x < self.vw and 0 <= bx < self.vh:
                    self.canvas.putpixel((x, bx), tuple(px[i * 3:i * 3 + 3]))
        else:
            raise RuntimeError('unknown kernel call %d' % o)
        self.cf(cf)

    # ---- calling into the program ----
    def call(self, name, far=False, **regs):
        mu = self.mu
        sp = 0xFF00
        if far:
            sp -= 4; self.wr(SS, sp, struct.pack('<HH', STOP, CS))
        else:
            sp -= 2; self.wr(SS, sp, struct.pack('<H', STOP))
        mu.reg_write(UC_X86_REG_SS, SS); mu.reg_write(UC_X86_REG_SP, sp)
        mu.reg_write(UC_X86_REG_DS, DG); mu.reg_write(UC_X86_REG_ES, DG)
        mu.reg_write(UC_X86_REG_CS, CS)
        for k, v in regs.items(): mu.reg_write(getattr(sys.modules['unicorn.x86_const'], 'UC_X86_REG_' + k.upper()), v & 0xffff)
        mu.emu_start(CS * 16 + self.sym[name], CS * 16 + STOP, count=200_000_000)
        assert mu.reg_read(UC_X86_REG_SP) == 0xFF00, 'stack not balanced after %s: %x' % (name, mu.reg_read(UC_X86_REG_SP))
        assert mu.reg_read(UC_X86_REG_DS) == DG or name in (), 'DS changed by %s' % name

    def v(self, off): return self.w(DG, off)
    def setv(self, off, val): self.setw(DG, off, val)
