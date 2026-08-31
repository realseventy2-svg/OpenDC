from pathlib import Path
import hashlib,re,struct
p=Path('dc_boot.bin')
d=p.read_bytes()
print('size:',hex(len(d)))
print('md5 :',hashlib.md5(d).hexdigest())
for s in (b'SEGA SEGAKATANA', b'KABUTO', b'Ver.1.01d'):
    for m in re.finditer(re.escape(s),d): print(s.decode(errors='ignore'),hex(m.start()),'CPU',hex(0xA0000000+m.start()))
print('\nfirst 0x120 bytes as little-endian SH-4 words:')
for off in range(0,0x120,2):
    w=struct.unpack_from('<H',d,off)[0]
    print(f'{off:06x}  {w:04x}')
