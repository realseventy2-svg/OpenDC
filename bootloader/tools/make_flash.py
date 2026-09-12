import struct
import os

def calc_crc(data):
    crc = 0xFFFF
    for b in data[:62]:
        crc ^= (b << 8)
        for _ in range(8):
            if crc & 0x8000:
                crc = ((crc << 1) ^ 0x1021) & 0xFFFF
            else:
                crc = (crc << 1) & 0xFFFF
    return crc ^ 0xFFFF

flash = bytearray([0xFF] * 131072)

# Partition 4 (64KB at 0x00000)
flash[0x0000:0x0012] = b'KATANA_FLASH____\x04\x00'
flash[0xFFC0] = 0x40

# Partition 3 (32KB at 0x10000)
flash[0x10000:0x10012] = b'KATANA_FLASH____\x03\x00'
flash[0x17FC0] = 0x40

# Partition 0 (8KB at 0x1A000)
flash[0x1A000:0x1A005] = b'00110'
flash[0x1A005:0x1A010] = b'Dreamcast  '
flash[0x1A0A0:0x1A0A5] = b'00110'
flash[0x1A0A5:0x1A0B0] = b'Dreamcast  '

# Partition 2 (16KB at 0x1C000)
flash[0x1C000:0x1C012] = b'KATANA_FLASH____\x02\x00'
sc = bytearray([0xFF] * 64)
sc[0:2] = struct.pack('<H', 5)
sc[2:6] = b'\x00\x00\x00\x00'
sc[6] = 0 # unk1
sc[7] = 1 # English
sc[8] = 0 # Stereo
sc[9] = 0 # Autostart Enabled
sc[10:14] = b'\x00\x00\x00\x00'
crc = calc_crc(sc)
sc[62:64] = struct.pack('<H', crc)
flash[0x1C040:0x1C080] = sc
flash[0x1C080:0x1C0C0] = sc
flash[0x1FFC0] = 0x3F

with open('dc_flash.bin', 'wb') as f:
    f.write(flash)
with open('dc_nvmem.bin', 'wb') as f:
    f.write(flash)
with open('bios/res/dc_flash.bin', 'wb') as f:
    f.write(flash)
with open('bootloader/res/dc_flash.bin', 'wb') as f:
    f.write(flash)

print('Generated authentic dc_flash.bin and dc_nvmem.bin: 131,072 bytes')

