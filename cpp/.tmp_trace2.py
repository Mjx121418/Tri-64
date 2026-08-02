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

rom = open('Super Mario Treasure World [v1.2.1].z64', 'rb').read()
scripts = rom[0x2abca0:0x2abca0 + 0x8000]

# 1) 找 EXECUTE(seg=0x19) 的 ROM 范围（opcode 0x00/0x01, size 0x10, seg 0x19）
pack_range = None
for off in range(0, len(scripts) - 16):
    if scripts[off] in (0x00, 0x01) and scripts[off+1] == 0x10:
        seg = struct.unpack('>H', scripts[off+2:off+4])[0]
        if seg == 0x19:
            rs, re = struct.unpack('>II', scripts[off+4:off+12])
            if re > rs and re <= len(rom):
                pack_range = (rs, re)
                print('EXECUTE seg=0x19 rom=%08X-%08X @scripts+%04X' % (rs, re, off))
                break
pack_range = (0x018e0000, 0x018e2000)

# level 9 的 pack 固定为 0x018E0000（跳转表第6项 -> 0x15:0x0458）
pack = rom[0x018e0000:0x018e2000]
print('pack 大小: %d bytes' % len(pack))

# 2) 按命令边界走课程脚本，解析 LOAD 命令
segs = {}
o = 0x1C
while o + 2 <= len(pack):
    cmd, size = pack[o], pack[o+1]
    if size == 0 or size > 0x10 or o + size > len(pack):
        break
    if cmd in (0x17, 0x18, 0x1a):
        seg = struct.unpack('>H', pack[o+2:o+4])[0]
        rs, re = struct.unpack('>II', pack[o+4:o+12])
        real = seg - 0x100 if 0x100 <= seg < 0x120 else seg
        if rs > 0 and re > rs and re <= len(rom) and real < 0x20:
            if cmd == 0x17:
                segs[real] = rom[rs:re]
                print('LOAD_RAW  seg=%02x rom=%08X-%08X (%d)' % (real, rs, re, re-rs))
            else:
                try:
                    segs[real] = mio0(rom, rs, re)
                    print('LOAD_MIO0 seg=%02x rom=%08X-%08X -> %d' % (real, rs, re, len(segs[real])))
                except Exception as e:
                    print('  mio0 fail seg=%02x: %s' % (real, e))
    o += size

class Segs:
    def __init__(self, d): self.data = d
    def read32(self, addr):
        seg, off = addr >> 24, addr & 0xFFFFFF
        return struct.unpack('>I', self.data[seg][off:off+4])[0]
    def read8(self, addr):
        seg, off = addr >> 24, addr & 0xFFFFFF
        return self.data[seg][off]

def trace(segs, entry, maxsteps=400000):
    stack = []; pc = entry; steps = 0
    events = []; cur_img = None
    lastaddrs = []
    while True:
        lastaddrs.append(pc)
        if len(lastaddrs) > 20: lastaddrs.pop(0)
        if steps > maxsteps: print('  step limit'); break
        steps += 1
        try:
            op = segs.read8(pc); w0 = segs.read32(pc); w1 = segs.read32(pc + 4)
        except KeyError:
            print('  !! DL 结束: 跳转到未加载段 pc=%08X（解释器在此返回）' % pc)
            break
        if op == 0xb8:
            if not stack: break
            pc = stack.pop(); continue
        elif op == 0x06:
            if (w0 >> 16) & 0xFF:  # push/call
                stack.append(pc + 8); pc = w1
            else:  # branch
                pc = w1
            continue
        elif op == 0xfd:
            cur_img = w1
            events.append(('SETIMG', 'img=%08X' % w1))
        elif op == 0xf5:
            events.append(('SETTILE', 'tile=%d tmem=0x%X' % ((w1>>24)&7, w0 & 0x1FF)))
        elif op == 0xf3:
            events.append(('LOADBLOCK', 'tile=%d img=%s' % ((w1>>24)&7, cur_img)))
        elif op == 0xf4:
            events.append(('LOADTILE', 'tile=%d img=%s' % ((w1>>24)&7, cur_img)))
        elif op == 0xbb:
            if w0 & 1: events.append(('TEXON', 'tile=%d' % (w0 & 0xF)))
            else: events.append(('TEXOFF', ''))
        elif op == 0xbf:
            events.append(('TRI', ''))
        pc += 8
    return events

s = Segs(segs)
ev = trace(s, 0x0e0214f0)
c = Counter(e[0] for e in ev)
print('hack DL 0x0E:0x0214F0: cmds=%d %s' % (len(ev), dict(c)))
tile_img = {}; last_img = None; problems = []
for e in ev:
    if e[0] == 'SETIMG': last_img = e[1].split('=')[1]
    elif e[0] in ('LOADBLOCK','LOADTILE'):
        tile = int(e[1].split('tile=')[1].split()[0])
        tile_img[tile] = last_img
    elif e[0] == 'TEXON':
        tile = int(e[1].split('tile=')[1])
        if tile in tile_img and tile_img[tile] != last_img:
            problems.append('tile%d img=%s lastSETIMG=%s' % (tile, tile_img[tile], last_img))
if problems:
    print('  WARNING %d cross-image switches:' % len(problems))
    for p in problems[:6]: print('   ', p)
else:
    print('  OK: all TEXON tile images == last SETIMG')
seq = []
for e in ev:
    if e[0] in ('SETIMG','LOADBLOCK','LOADTILE','TEXON','TEXOFF'):
        seq.append('%s(%s)' % (e[0][3:], e[1]))
print('  texseq: %s' % ' | '.join(seq[:50]))
