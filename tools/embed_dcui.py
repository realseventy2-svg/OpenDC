import sys

def convert(bin_path, out_c_path):
    with open(bin_path, 'rb') as f:
        data = f.read()

    lines = []
    lines.append('#include <stdint.h>')
    lines.append('')
    lines.append(f'const uint8_t DEFAULT_MENU_DCUI[{len(data)}] = {{')

    for i in range(0, len(data), 16):
        chunk = data[i:i+16]
        hex_vals = [f'0x{b:02X}' for b in chunk]
        lines.append('    ' + ', '.join(hex_vals) + ',')

    lines.append('};')
    lines.append('')

    with open(out_c_path, 'w') as f:
        f.write('\n'.join(lines))

    print(f'Wrote {out_c_path} ({len(data)} bytes)')

if __name__ == '__main__':
    inp = sys.argv[1] if len(sys.argv) > 1 else 'bios/res/ui/default_menu.dcui'
    out = sys.argv[2] if len(sys.argv) > 2 else 'bios/src/gui/dcui/default_scene.c'
    convert(inp, out)
