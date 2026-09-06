#!/usr/bin/env python3
"""
pack_scene.py — Boot Scene Container Packager
==============================================
Packs 3D mesh, per-frame baked transforms, and 4 audio wavetables
into a boot_scene.bin file conforming to the DCBS container format.

Container magic: 0x53424344 ("DCBS" little-endian)

Usage
-----
python3 pack_scene.py [options] --output boot_scene.bin

Minimal example (procedural audio, no external mesh):
    python3 pack_scene.py \\
        --transforms transforms.csv \\
        --output boot_scene.bin

Full example:
    python3 pack_scene.py \\
        --transforms transforms.csv \\
        --mesh model.obj \\
        --wav0 bell.wav --wav1 pad.raw --wav2 bass.wav --wav3 shimmer.raw \\
        --cues audio_cues.json \\
        --output boot_scene.bin

Transform file formats
----------------------
CSV:  One row per frame, 12 comma-separated floats:
      mpos_x,mpos_y,mpos_z,mrot_x,mrot_y,mrot_z,cpos_x,cpos_y,cpos_z,crot_x,crot_y,crot_z

JSON: Array of objects:
      [{"model_pos":[x,y,z], "model_rot":[rx,ry,rz], "cam_pos":[x,y,z], "cam_rot":[rx,ry,rz]}, ...]

Audio files
-----------
WAV:  Standard RIFF WAV.  Must be 16-bit signed mono.  If the file contains
      more than 256 samples only the first 256 are used; if fewer than 256
      the remainder is zero-padded.
RAW:  Raw 16-bit signed little-endian PCM.  Same 256-sample rule applies.

If --wav0..3 are omitted, four wavetables are procedurally synthesised
(celesta bell, ambient pad, warm bass, silky shimmer — matching sound.c).

Audio cue JSON format
---------------------
Array of objects:
[
  {
    "frame":        36,     // VBlank tick (0-based)
    "channel":      5,      // AICA channel 0..63
    "wavetable":    0,      // Wavetable index 0..3
    "note":         64,     // MIDI note 21..108
    "volume":       11,     // 0..15
    "pan":          24,     // 0..31
    "adsr_preset":  0       // 0=Bell 1=Strings 2=Bass 3=Shimmer
  }, ...
]
Max 32 cue entries.

Binary Layout Produced
----------------------
Offset  Bytes  Field
0       64     BootSceneHeader (magic, version, counts, offsets)
A       F×48   Transforms: F frames × 12 floats × 4 bytes
B       V×12   Vertices:   V verts  × 3 floats  × 4 bytes
C       I×2    Indices:    I uint16_t values (I must be multiple of 3)
D       C×16   Audio cues: C × 16-byte AudioCue records
E       2048   Wavetables: 4 × 256 × int16_t
"""

import struct
import sys
import os
import json
import argparse
import math

# ---------------------------------------------------------------------------
# Constants
# ---------------------------------------------------------------------------
MAGIC           = 0x53424344       # 'DCBS' little-endian
VERSION         = 1
HEADER_SIZE     = 64               # bytes (must match C struct)
WAV_SAMPLES     = 256
WAV_BYTES       = WAV_SAMPLES * 2  # 512 bytes (int16_t each)
WAV_COUNT       = 4
WAV_BLOCK_BYTES = WAV_COUNT * WAV_BYTES  # 2048 bytes
MAX_CUES        = 32
ALIGN           = 4                # 32-bit alignment for all sections


# ---------------------------------------------------------------------------
# Alignment helper
# ---------------------------------------------------------------------------
def align_up(val: int, align: int) -> int:
    return (val + align - 1) & ~(align - 1)


def pad_to(data: bytes, alignment: int) -> bytes:
    rem = len(data) % alignment
    if rem:
        data += b'\x00' * (alignment - rem)
    return data


# ---------------------------------------------------------------------------
# Procedural wavetable synthesis (mirrors sound.c)
# ---------------------------------------------------------------------------
_SIN_QUARTER = [
      0,   6,  12,  18,  25,  31,  37,  43,
     49,  56,  62,  68,  74,  80,  86,  92,
     97, 103, 109, 115, 120, 126, 131, 136,
    142, 147, 152, 157, 162, 167, 171, 176,
    181, 185, 189, 193, 197, 201, 205, 208,
    212, 215, 219, 222, 225, 228, 231, 233,
    236, 238, 240, 242, 244, 246, 247, 249,
    250, 251, 252, 253, 254, 254, 255, 255,
    256,
]

def _sin_fx(angle: int) -> int:
    angle &= 0xFF
    if angle <= 64:  return  _SIN_QUARTER[angle]
    if angle <= 128: return  _SIN_QUARTER[128 - angle]
    if angle <= 192: return -_SIN_QUARTER[angle - 128]
    return                  -_SIN_QUARTER[256 - angle]


def _clamp16(v: int) -> int:
    return max(-32768, min(32767, v))


def _synth_wavetable(kind: int) -> bytes:
    """
    Synthesise 256 int16_t samples for one of the 4 built-in timbres.
    kind: 0=Bell 1=Pad 2=Bass 3=Shimmer
    """
    samples = []
    for i in range(256):
        s1 = _sin_fx(i)
        s2 = _sin_fx(i * 2)
        s3 = _sin_fx(i * 3)
        if kind == 0:                            # Velvet Celesta Bell
            val = s1 * 98 + s2 * 14 + s3 * 4
        elif kind == 1:                          # Serene Ambient Pad
            val = s1 * 92 + s2 * 18 + s3 * 6
        elif kind == 2:                          # Warm Acoustic Bass
            val = s1 * 100 + s2 * 20
        else:                                    # Silky Air Bloom
            val = _sin_fx(i * 2) * 60 + _sin_fx(i * 4) * 25
        samples.append(_clamp16(val))
    return struct.pack('<' + 'h' * 256, *samples)


# ---------------------------------------------------------------------------
# WAV / RAW audio loader
# ---------------------------------------------------------------------------
def load_audio_file(path: str) -> bytes:
    """
    Load an audio file and return exactly WAV_BYTES (512 bytes = 256 × int16_t).
    Supports RIFF WAV (16-bit signed mono) and raw 16-bit PCM.
    If the source has > 256 samples, only the first 256 are used.
    If fewer, zero-pad to exactly 256 samples.
    """
    raw = open(path, 'rb').read()

    # Detect RIFF WAV
    if raw[:4] == b'RIFF' and raw[8:12] == b'WAVE':
        pcm_data = _parse_wav(raw, path)
    else:
        pcm_data = raw  # treat as raw 16-bit signed LE PCM

    # Ensure exactly 512 bytes (256 × int16_t)
    if len(pcm_data) >= WAV_BYTES:
        return pcm_data[:WAV_BYTES]
    return pcm_data + b'\x00' * (WAV_BYTES - len(pcm_data))


def _parse_wav(data: bytes, path: str) -> bytes:
    """Extract raw PCM from a RIFF WAV file."""
    import io
    buf = io.BytesIO(data)

    # RIFF header
    buf.seek(0)
    riff, riff_size = struct.unpack_from('<4sI', data, 0)
    wave = data[8:12]
    if riff != b'RIFF' or wave != b'WAVE':
        raise ValueError(f"{path}: not a valid RIFF WAVE file")

    # Scan chunks
    pos = 12
    fmt_data = None
    pcm_data = None
    while pos + 8 <= len(data):
        chunk_id = data[pos:pos+4]
        chunk_sz = struct.unpack_from('<I', data, pos+4)[0]
        chunk_data = data[pos+8: pos+8+chunk_sz]
        if chunk_id == b'fmt ':
            fmt_data = chunk_data
        elif chunk_id == b'data':
            pcm_data = chunk_data
        pos += 8 + chunk_sz

    if fmt_data is None or pcm_data is None:
        raise ValueError(f"{path}: WAV missing fmt or data chunk")

    audio_fmt  = struct.unpack_from('<H', fmt_data, 0)[0]
    channels   = struct.unpack_from('<H', fmt_data, 2)[0]
    bits       = struct.unpack_from('<H', fmt_data, 14)[0]

    if audio_fmt != 1:
        raise ValueError(f"{path}: WAV must be PCM (format 1), got {audio_fmt}")
    if channels != 1:
        raise ValueError(f"{path}: WAV must be mono (1 channel), got {channels}")
    if bits != 16:
        raise ValueError(f"{path}: WAV must be 16-bit, got {bits}")

    return pcm_data


# ---------------------------------------------------------------------------
# OBJ mesh loader
# ---------------------------------------------------------------------------
def load_obj(path: str):
    """
    Load a Wavefront OBJ file.
    Returns (vertices, indices) where:
      vertices : list of (x, y, z) float tuples
      indices  : list of int (triangle vertex indices, groups of 3)

    Only handles triangulated meshes (f v1 v2 v3 format).
    Quad faces (f v1 v2 v3 v4) are auto-triangulated as (0,1,2) + (0,2,3).
    """
    vertices = []
    indices  = []
    with open(path, 'r') as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith('#'):
                continue
            parts = line.split()
            if parts[0] == 'v':
                x, y, z = float(parts[1]), float(parts[2]), float(parts[3])
                vertices.append((x, y, z))
            elif parts[0] == 'f':
                # Parse face indices: OBJ is 1-based, may be v/vt/vn format
                face_verts = []
                for token in parts[1:]:
                    vi = int(token.split('/')[0]) - 1  # 0-based
                    face_verts.append(vi)
                # Triangulate fan
                for i in range(1, len(face_verts) - 1):
                    indices.append(face_verts[0])
                    indices.append(face_verts[i])
                    indices.append(face_verts[i + 1])
    return vertices, indices


def generate_default_mesh():
    """
    Generate a unit cube mesh as a default when no --mesh is provided.
    8 vertices, 12 triangles (36 indices).
    """
    s = 1.0
    verts = [
        (-s,-s,-s), ( s,-s,-s), ( s, s,-s), (-s, s,-s),  # back
        (-s,-s, s), ( s,-s, s), ( s, s, s), (-s, s, s),  # front
    ]
    # 6 faces × 2 triangles each
    faces = [
        # front  (z=+s)
        (4,5,6), (4,6,7),
        # back   (z=-s)
        (1,0,3), (1,3,2),
        # left   (x=-s)
        (0,4,7), (0,7,3),
        # right  (x=+s)
        (5,1,2), (5,2,6),
        # top    (y=+s)
        (7,6,2), (7,2,3),
        # bottom (y=-s)
        (0,1,5), (0,5,4),
    ]
    indices = [i for tri in faces for i in tri]
    return verts, indices


# ---------------------------------------------------------------------------
# Transform loader
# ---------------------------------------------------------------------------
def load_transforms_csv(path: str):
    """
    Load transforms from a CSV file.
    Each row: mpos_x,mpos_y,mpos_z,mrot_x,mrot_y,mrot_z,cpos_x,cpos_y,cpos_z,crot_x,crot_y,crot_z
    Returns list of float[12] lists.
    """
    frames = []
    with open(path, 'r') as f:
        for lineno, line in enumerate(f, 1):
            line = line.strip()
            if not line or line.startswith('#'):
                continue
            vals = [float(v.strip()) for v in line.split(',')]
            if len(vals) != 12:
                raise ValueError(f"CSV line {lineno}: expected 12 values, got {len(vals)}")
            frames.append(vals)
    return frames


def load_transforms_json(path: str):
    """
    Load transforms from a JSON file.
    Format: [{"model_pos":[x,y,z], "model_rot":[rx,ry,rz],
               "cam_pos":[x,y,z],  "cam_rot":[rx,ry,rz]}, ...]
    Returns list of float[12] lists.
    """
    with open(path, 'r') as f:
        data = json.load(f)
    frames = []
    for i, entry in enumerate(data):
        mp = entry.get('model_pos', [0, 0, -5])
        mr = entry.get('model_rot', [0, 0,  0])
        cp = entry.get('cam_pos',  [0, 0,  0])
        cr = entry.get('cam_rot',  [0, 0,  0])
        row = [float(v) for v in mp + mr + cp + cr]
        if len(row) != 12:
            raise ValueError(f"JSON entry {i}: could not form 12-float transform")
        frames.append(row)
    return frames


def load_transforms(path: str):
    """Auto-detect CSV vs JSON by extension."""
    ext = os.path.splitext(path)[1].lower()
    if ext in ('.json', '.js'):
        return load_transforms_json(path)
    return load_transforms_csv(path)


def generate_default_transforms(total_frames: int = 480):
    """
    Generate a default transform sequence: 3D model with smooth tumble spin,
    camera fixed at (0, 0, 7.5) looking at origin.
    """
    frames = []
    for f in range(total_frames):
        angle_y = (f * 360.0) / total_frames
        angle_x = 20.0 * math.sin((f * 2.0 * math.pi) / total_frames)
        row = [
            0.0, 0.45, 0.0,             # model pos (centered above branding)
            angle_x, angle_y, 0.0,      # model rot (smooth 3D tumble)
            0.0, 0.0, 7.5,              # camera pos
            0.0, 0.0, 0.0,              # camera rot
        ]
        frames.append(row)
    return frames



# ---------------------------------------------------------------------------
# Audio cue loader
# ---------------------------------------------------------------------------
def load_cues(path: str):
    """
    Load audio cues from a JSON file.
    Returns list of dicts with keys: frame, channel, wavetable, note, volume, pan, adsr_preset.
    """
    with open(path, 'r') as f:
        data = json.load(f)
    cues = []
    for i, entry in enumerate(data):
        cue = {
            'frame':       int(entry.get('frame',       0)),
            'channel':     int(entry.get('channel',     0)),
            'wavetable':   int(entry.get('wavetable',   0)),
            'note':        int(entry.get('note',        64)),
            'volume':      int(entry.get('volume',      11)),
            'pan':         int(entry.get('pan',          0)),
            'adsr_preset': int(entry.get('adsr_preset', 0)),
        }
        cues.append(cue)
    if len(cues) > MAX_CUES:
        print(f"WARNING: {len(cues)} cues given but max is {MAX_CUES}; truncating.")
        cues = cues[:MAX_CUES]
    return cues


def default_cues(total_frames: int = 480):
    """
    Reproduce the exact cue sequence from sound.c (extended pacing).
    """
    return [
        # Frame  2: Sub-bass foundation + strings
        {'frame': 2,   'channel': 0, 'wavetable': 2, 'note': 40, 'volume':  9, 'pan': 0x00, 'adsr_preset': 2},
        {'frame': 2,   'channel': 1, 'wavetable': 1, 'note': 52, 'volume':  8, 'pan': 0x1E, 'adsr_preset': 1},
        {'frame': 2,   'channel': 2, 'wavetable': 1, 'note': 59, 'volume':  8, 'pan': 0x0E, 'adsr_preset': 1},
        {'frame': 2,   'channel': 3, 'wavetable': 1, 'note': 68, 'volume':  8, 'pan': 0x1A, 'adsr_preset': 1},
        {'frame': 2,   'channel': 4, 'wavetable': 1, 'note': 75, 'volume':  7, 'pan': 0x0A, 'adsr_preset': 1},
        # Frame 36: First celesta bell
        {'frame': 36,  'channel': 5, 'wavetable': 0, 'note': 64, 'volume': 11, 'pan': 0x18, 'adsr_preset': 0},
        {'frame': 36,  'channel': 6, 'wavetable': 0, 'note': 64, 'volume':  8, 'pan': 0x08, 'adsr_preset': 0},
        # Frame 84: Second celesta bell
        {'frame': 84,  'channel': 7, 'wavetable': 0, 'note': 68, 'volume': 11, 'pan': 0x08, 'adsr_preset': 0},
        {'frame': 84,  'channel': 8, 'wavetable': 0, 'note': 68, 'volume':  8, 'pan': 0x18, 'adsr_preset': 0},
        # Frame 138: Third bell
        {'frame': 138, 'channel': 5, 'wavetable': 0, 'note': 71, 'volume': 11, 'pan': 0x16, 'adsr_preset': 0},
        {'frame': 138, 'channel': 6, 'wavetable': 0, 'note': 71, 'volume':  8, 'pan': 0x0A, 'adsr_preset': 0},
        # Frame 198: Climax bells + shimmer
        {'frame': 198, 'channel': 9,  'wavetable': 0, 'note': 76, 'volume': 11, 'pan': 0x00, 'adsr_preset': 0},
        {'frame': 198, 'channel': 10, 'wavetable': 0, 'note': 76, 'volume':  9, 'pan': 0x1E, 'adsr_preset': 0},
        {'frame': 198, 'channel': 11, 'wavetable': 0, 'note': 76, 'volume':  9, 'pan': 0x0E, 'adsr_preset': 0},
        {'frame': 198, 'channel': 12, 'wavetable': 3, 'note': 68, 'volume':  7, 'pan': 0x1E, 'adsr_preset': 3},
        {'frame': 198, 'channel': 13, 'wavetable': 3, 'note': 68, 'volume':  7, 'pan': 0x0E, 'adsr_preset': 3},
    ]


# ---------------------------------------------------------------------------
# Binary packer
# ---------------------------------------------------------------------------
def pack_scene(transforms, vertices, indices, cues, wavetables) -> bytes:
    """
    Pack all sections into a DCBS binary blob.

    transforms  : list of float[12] lists, one per frame
    vertices    : list of (x,y,z) float tuples
    indices     : list of int (must be multiple of 3)
    cues        : list of cue dicts
    wavetables  : list of 4 × 512-byte bytes objects

    Returns the complete binary blob as bytes.
    """
    assert len(wavetables) == 4, "Exactly 4 wavetables required"
    assert len(indices) % 3 == 0, "Index count must be a multiple of 3"
    assert len(cues) <= MAX_CUES, f"Max {MAX_CUES} audio cues"

    total_frames  = len(transforms)
    vertex_count  = len(vertices)
    index_count   = len(indices)
    cue_count     = len(cues)

    # ---- Serialise each section ----

    # Transforms: total_frames × 12 × float32 (little-endian)
    xf_bytes = b''.join(
        struct.pack('<12f', *frame) for frame in transforms
    )
    xf_bytes = pad_to(xf_bytes, ALIGN)

    # Vertices: vertex_count × 3 × float32
    vt_bytes = b''.join(
        struct.pack('<3f', *v) for v in vertices
    )
    vt_bytes = pad_to(vt_bytes, ALIGN)

    # Indices: uint16_t (packed, padded to 4-byte boundary)
    ix_bytes = struct.pack('<' + 'H' * index_count, *indices)
    ix_bytes = pad_to(ix_bytes, ALIGN)

    # Audio cues: 16 bytes each
    # Layout: uint32(4) + 6×uint8(6) + reserved[6](6) = 16 bytes
    cue_bytes = b''
    for cue in cues:
        cue_bytes += struct.pack('<IBBBBBB6x',
            cue['frame'],
            cue['channel']     & 0xFF,
            cue['wavetable']   & 0xFF,
            cue['note']        & 0xFF,
            cue['volume']      & 0xFF,
            cue['pan']         & 0xFF,
            cue['adsr_preset'] & 0xFF,
        )
    cue_bytes = pad_to(cue_bytes, ALIGN)

    # Wavetable block: 4 × 512 bytes = 2048 bytes (already aligned)
    wt_bytes = b''.join(wavetables)
    assert len(wt_bytes) == 2048

    # ---- Compute offsets ----
    off_transforms = HEADER_SIZE
    off_vertices   = off_transforms + len(xf_bytes)
    off_indices    = off_vertices   + len(vt_bytes)
    off_cues       = off_indices    + len(ix_bytes)
    off_wavetables = off_cues       + len(cue_bytes)

    # ---- Build 64-byte header ----
    # Layout (little-endian, packed):
    #   0x00 uint32 magic
    #   0x04 uint16 version
    #   0x06 uint16 flags
    #   0x08 uint32 total_frames
    #   0x0C uint32 vertex_count
    #   0x10 uint32 index_count
    #   0x14 uint32 audio_cue_count
    #   0x18 uint32 off_transforms
    #   0x1C uint32 off_vertices
    #   0x20 uint32 off_indices
    #   0x24 uint32 off_audio_cues
    #   0x28 uint32 off_wavetables
    #   0x2C uint32 reserved[0..4]  (5 × 4 = 20 bytes)
    #  ──── Total: 4+2+2 + 9×4 + 5×4 = 8+36+20 = 64 bytes
    header = struct.pack('<IHH IIII IIIII 5I',
        MAGIC,           # 0x00
        VERSION,         # 0x04
        0,               # 0x06  flags
        total_frames,    # 0x08
        vertex_count,    # 0x0C
        index_count,     # 0x10
        cue_count,       # 0x14
        off_transforms,  # 0x18
        off_vertices,    # 0x1C
        off_indices,     # 0x20
        off_cues,        # 0x24
        off_wavetables,  # 0x28
        0, 0, 0, 0, 0,  # 0x2C  reserved[0..4]
    )
    assert len(header) == HEADER_SIZE, f"Header is {len(header)} bytes, expected {HEADER_SIZE}"

    # ---- Concatenate everything ----
    blob = header + xf_bytes + vt_bytes + ix_bytes + cue_bytes + wt_bytes
    return blob


# ---------------------------------------------------------------------------
# Verification: hex-dump the header fields for quick sanity check
# ---------------------------------------------------------------------------
def verify_blob(blob: bytes) -> None:
    if len(blob) < HEADER_SIZE:
        print("ERROR: blob shorter than header!")
        return

    (magic, version, flags, total_frames, vertex_count, index_count, cue_count,
     off_xf, off_vt, off_ix, off_cue, off_wav) = struct.unpack_from('<IHH IIII IIIII', blob, 0)

    magic_str = ''.join(chr((magic >> (8*i)) & 0xFF) for i in range(4))
    print(f"  magic          : 0x{magic:08X}  '{magic_str}'")
    print(f"  version        : {version}")
    print(f"  flags          : {flags}")
    print(f"  total_frames   : {total_frames}")
    print(f"  vertex_count   : {vertex_count}")
    print(f"  index_count    : {index_count}  ({index_count//3} triangles)")
    print(f"  cue_count      : {cue_count}")
    print(f"  off_transforms : 0x{off_xf:08X}  ({off_xf} bytes)")
    print(f"  off_vertices   : 0x{off_vt:08X}  ({off_vt} bytes)")
    print(f"  off_indices    : 0x{off_ix:08X}  ({off_ix} bytes)")
    print(f"  off_audio_cues : 0x{off_cue:08X}  ({off_cue} bytes)")
    print(f"  off_wavetables : 0x{off_wav:08X}  ({off_wav} bytes)")
    print(f"  total size     : {len(blob):,} bytes")

    if magic != MAGIC:
        print("ERROR: Magic mismatch!")
    if version != VERSION:
        print("ERROR: Version mismatch!")


# ---------------------------------------------------------------------------
# CLI Entry Point
# ---------------------------------------------------------------------------
def main():
    parser = argparse.ArgumentParser(
        description='Pack a boot_scene.bin DCBS container for the Sega Dreamcast.',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__,
    )

    parser.add_argument('--transforms', metavar='FILE',
        help='CSV or JSON file with per-frame transforms (12 floats per row/entry). '
             'Omit to generate a default Y-spin animation.')
    parser.add_argument('--frames', type=int, default=480, metavar='N',
        help='Number of animation frames (default: 480 = 8 seconds at 60 FPS). '
             'Only used when --transforms is omitted.')
    parser.add_argument('--mesh', metavar='FILE',
        help='Wavefront OBJ mesh file. Omit to use a built-in unit cube.')
    parser.add_argument('--wav0', metavar='FILE', help='Wavetable 0 (Bell)    WAV or RAW 16-bit mono PCM, 256 samples.')
    parser.add_argument('--wav1', metavar='FILE', help='Wavetable 1 (Strings) WAV or RAW 16-bit mono PCM, 256 samples.')
    parser.add_argument('--wav2', metavar='FILE', help='Wavetable 2 (Bass)    WAV or RAW 16-bit mono PCM, 256 samples.')
    parser.add_argument('--wav3', metavar='FILE', help='Wavetable 3 (Shimmer) WAV or RAW 16-bit mono PCM, 256 samples.')
    parser.add_argument('--cues', metavar='FILE',
        help='JSON file with audio cue list. Omit to use built-in cue sequence.')
    parser.add_argument('--output', '-o', metavar='FILE', default='boot_scene.bin',
        help='Output path (default: boot_scene.bin).')
    parser.add_argument('--verify', action='store_true',
        help='Print a header summary after writing the file.')
    parser.add_argument('--quiet', '-q', action='store_true',
        help='Suppress progress messages.')

    args = parser.parse_args()

    def log(msg):
        if not args.quiet:
            print(msg)

    # ---- Transforms ----
    if args.transforms:
        log(f"Loading transforms from: {args.transforms}")
        transforms = load_transforms(args.transforms)
        log(f"  {len(transforms)} frames loaded.")
    else:
        total_frames = args.frames
        log(f"No transforms provided — generating default Y-spin ({total_frames} frames).")
        transforms = generate_default_transforms(total_frames)

    # ---- Mesh ----
    if args.mesh:
        log(f"Loading mesh from: {args.mesh}")
        vertices, indices = load_obj(args.mesh)
        log(f"  {len(vertices)} vertices, {len(indices)//3} triangles.")
    else:
        log("No mesh provided — using built-in unit cube.")
        vertices, indices = generate_default_mesh()

    if len(vertices) == 0:
        print("ERROR: mesh has no vertices.")
        sys.exit(1)
    if len(indices) == 0 or len(indices) % 3 != 0:
        print(f"ERROR: index count {len(indices)} is not a multiple of 3.")
        sys.exit(1)
    if max(indices) >= len(vertices):
        print(f"ERROR: mesh index {max(indices)} out of range (vertex_count={len(vertices)}).")
        sys.exit(1)
    if max(indices) > 65535:
        print(f"ERROR: vertex index {max(indices)} exceeds uint16_t range.")
        sys.exit(1)

    # ---- Audio wavetables ----
    wav_files = [args.wav0, args.wav1, args.wav2, args.wav3]
    wavetables = []
    for kind, wav_path in enumerate(wav_files):
        label = ['Bell', 'Strings', 'Bass', 'Shimmer'][kind]
        if wav_path:
            log(f"Loading wavetable {kind} ({label}) from: {wav_path}")
            wt = load_audio_file(wav_path)
        else:
            log(f"Synthesising wavetable {kind} ({label}) procedurally.")
            wt = _synth_wavetable(kind)
        assert len(wt) == WAV_BYTES, f"Wavetable {kind} is {len(wt)} bytes, expected {WAV_BYTES}"
        wavetables.append(wt)

    # ---- Audio cues ----
    if args.cues:
        log(f"Loading audio cues from: {args.cues}")
        cues = load_cues(args.cues)
        log(f"  {len(cues)} cues loaded.")
    else:
        log("No cue file provided — using built-in cue sequence.")
        cues = default_cues(len(transforms))

    # ---- Pack ----
    log("Packing DCBS container...")
    blob = pack_scene(transforms, vertices, indices, cues, wavetables)

    # ---- Write ----
    with open(args.output, 'wb') as f:
        f.write(blob)
    log(f"Written: {args.output}  ({len(blob):,} bytes)")

    # ---- Verify ----
    if args.verify or not args.quiet:
        print("\nHeader dump:")
        verify_blob(blob)


if __name__ == '__main__':
    main()
