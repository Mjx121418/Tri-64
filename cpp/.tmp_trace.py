import struct
from collections import Counter

def mio0(rom, s, e):
    d = rom[s:e]
    dlen, coff, uoff = struct.unpack('>III', d[4:16])
    layout, comp, uncomp = d[16:coff], d[coff:uoff], d[uoff:]
    out = bytearray(); ci = ui = 0
    for lb in layout:
        for i in range(8):
            if lb & (0x80 >> i):
                out.append(uncomp[ui]); ui += 1
            else:
                length = (comp[ci] >> 4) + 3
                offset = ((comp[ci] & 0x0F) << 8) + comp[ci+1]
                ci += 2
                for j in range(length): out.append(out[len(out)-offset-1])
            if len(out) == dlen: break
        if len(out) == dlen: break
    return out

class Segs:
    def __init__(self): self.data = {}
    def load(self, seg, blob): self.data[seg] = blob
    def read32(self, addr):
        seg, off = addr >> 24, addr & 0xFFFFFF
        return struct.unpack('>I', self.data[seg][off:off+4])[0]
    def read8(self, addr):
        seg, off = addr >> 24, addr & 0xFFFFFF
        return self.data[seg][off]

def trace(segs, entry, maxsteps=200000):
    stack = []
    pc = entry
    steps = 0
    events = []
    cur_img = None
    while True:
        if steps > maxsteps:
            print('  step limit'); break
        steps += 1
        op = segs.read8(pc)
        w0 = segs.read32(pc)
        w1 = segs.read32(pc + 4)
        if op == 0xb8:  # ENDDL
            if not stack: break
            pc = stack.pop()
            continue
        elif op == 0x06:  # G_DL
            target = w1
            if w1 >> 24:  # branch
                pc = target
            else:
                stack.append(pc + 8)
                pc = target
            continue
        elif op == 0xfd:  # SETTEXIMAGE
            cur_img = w1
            events.append(('SETIMG', 'img=%08X' % w1))
        elif op == 0xf5:  # SETTILE
            tile = (w1 >> 24) & 7
            tmem = w0 & 0x1FF
            events.append(('SETTILE', 'tile=%d tmem=0x%X' % (tile, tmem)))
        elif op == 0xf3:  # LOADBLOCK
            tile = (w1 >> 24) & 7
            events.append(('LOADBLOCK', 'tile=%d img=%08X' % (tile, cur_img)))
        elif op == 0xf4:  # LOADTILE
            tile = (w1 >> 24) & 7
            events.append(('LOADTILE', 'tile=%d img=%08X' % (tile, cur_img)))
        elif op == 0xbb:  # TEXTURE
            on = w0 & 1
            tile = w0 & 0xF
            if on:
                events.append(('TEXON', 'tile=%d' % tile))
            else:
                events.append(('TEXOFF', ''))
        elif op == 0xbf:  # TRI1
            events.append(('TRI', ''))
        pc += 8
    return events

def analyze(name, segs, entries):
    for entry in entries:
        ev = trace(segs, entry)
        tile_img = {}
        last_img = None
        problems = []
        for e in ev:
            if e[0] == 'SETIMG':
                last_img = e[1].split('=')[1]
            elif e[0] in ('LOADBLOCK', 'LOADTILE'):
                tile = int(e[1].split('tile=')[1].split()[0])
                tile_img[tile] = last_img
            elif e[0] == 'TEXON':
                tile = int(e[1].split('tile=')[1])
                if tile in tile_img and tile_img[tile] != last_img:
                    problems.append('tile%d uses img=%s but last SETIMG=%s' % (tile, tile_img[tile], last_img))
        c = Counter(e[0] for e in ev)
        print('%s %08X: cmds=%d %s' % (name, entry, len(ev), dict(c)))
        if problems:
            print('  WARNING cross-image tile switch: %s' % problems[:3])
        else:
            print('  OK: every TEXON tile image == last SETIMG')
        tex_ev = [e for e in ev if e[0] in ('SETIMG','SETTILE','LOADBLOCK','LOADTILE','TEXON','TEXOFF')]
        print('  texseq: ' + ' | '.join('%s(%s)' % (t[0][3:], t[1]) for t in tex_ev[:16]))

rom = open('baserom.us.z64','rb').read()
segs = Segs()
segs.load(0x07, mio0(rom, 0x3fc2b0, 0x405a60))
segs.load(0x09, mio0(rom, 0x32d070, 0x334b30))
entries = [0x07004390, 0x07009d80, 0x0700a470, 0x0700a920, 0x0700dd18, 0x0700e338]
for e in entries:
    analyze('baserom', segs, [e])
