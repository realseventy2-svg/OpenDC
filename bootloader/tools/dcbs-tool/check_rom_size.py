#!/usr/bin/env python3
"""
OpenDC - Dreamcast Flash ROM & Boot Scene Memory Map Analyzer
Verifies boot_scene.bin, bootloader payload, and 2MB BIOS ROM constraints.
"""

import os
import sys

# Ensure UTF-8 output on Windows consoles
if sys.platform == "win32" and hasattr(sys.stdout, "reconfigure"):
    try:
        sys.stdout.reconfigure(encoding='utf-8')
    except Exception:
        pass

def format_size(num_bytes):
    if num_bytes >= 1024 * 1024:
        return f"{num_bytes / (1024 * 1024):.2f} MB ({num_bytes:,} bytes)"
    elif num_bytes >= 1024:
        return f"{num_bytes / 1024:.1f} KB ({num_bytes:,} bytes)"
    else:
        return f"{num_bytes:,} bytes"

def draw_bar(used, total, width=28):
    pct = min(max(used / float(total), 0.0), 1.0)
    filled = int(round(pct * width))
    bar = "=" * filled + "-" * (width - filled)
    return f"[{bar}] {pct * 100.0:5.1f}%"

def check_rom_sizes():
    script_dir = os.path.dirname(os.path.abspath(__file__)) if '__file__' in globals() else os.getcwd()
    bootloader_dir = os.path.dirname(os.path.dirname(script_dir))
    opendc_dir = os.path.dirname(bootloader_dir)

    boot_scene_bin = os.path.join(bootloader_dir, "boot_scene.bin")
    boot_bin = os.path.join(bootloader_dir, "boot.bin")
    dc_boot_bin = os.path.join(bootloader_dir, "dc_boot.bin")

    TOTAL_ROM_CAPACITY = 2 * 1024 * 1024  # Exactly 2,097,152 bytes (2 MB)
    BOOT_CODE_MAX      = 64 * 1024         # 65,536 bytes (64 KB Stage 0)
    PAYLOAD_MAX        = 576 * 1024        # 589,824 bytes (576 KB Stage 1)
    BOOT_SCENE_MAX     = 1408 * 1024       # 1,441,792 bytes (1,408 KB Boot Scene)

    sz_scene = os.path.getsize(boot_scene_bin) if os.path.exists(boot_scene_bin) else 0

    # Calculate actual Stage 0 code size in first 64KB
    sz_code = 0
    if os.path.exists(boot_bin):
        with open(boot_bin, 'rb') as f:
            header_bytes = f.read(BOOT_CODE_MAX)
            idx = len(header_bytes) - 1
            while idx >= 0 and header_bytes[idx] == 0:
                idx -= 1
            sz_code = idx + 1 if idx >= 0 else 0

    # Check for custom secondary OS payload in OpenDC
    custom_payload_candidates = [
        os.path.join(opendc_dir, "bios", "custom_bios.bin"),
        os.path.join(opendc_dir, "custom_bios.bin"),
        os.path.join(bootloader_dir, "custom_bios.bin"),
    ]
    sz_payload = 0
    for cp in custom_payload_candidates:
        if os.path.exists(cp):
            sz_payload = os.path.getsize(cp)
            break

    total_actual_used = sz_code + sz_payload + sz_scene
    total_remaining_free = max(0, TOTAL_ROM_CAPACITY - total_actual_used)

    print("=======================================================")
    print("  OpenDC Flash ROM Memory Budget & Size Breakdown")
    print("=======================================================")
    print(f"  Total Flash ROM Limit : {format_size(TOTAL_ROM_CAPACITY)}")
    print("-------------------------------------------------------")
    
    # 1. Boot Scene Blob
    scene_free = max(0, BOOT_SCENE_MAX - sz_scene)
    print(f"[1] 3D Boot Scene (boot_scene.bin):")
    print(f"    Size Used : {format_size(sz_scene)}")
    print(f"    Partition : {format_size(BOOT_SCENE_MAX)} (0xA00A0000..0xA01FFFFF / 640 KB+)")
    print(f"    Remaining : {format_size(scene_free)} free headroom")
    print(f"    Usage     : {draw_bar(sz_scene, BOOT_SCENE_MAX)}")
    print()

    # 2. OpenDC Bootloader Core Engine
    code_free = max(0, BOOT_CODE_MAX - sz_code)
    print(f"[2] OpenDC Bootloader Core (Stage 0):")
    print(f"    Size Used : {format_size(sz_code)}")
    print(f"    Partition : {format_size(BOOT_CODE_MAX)} (0xA0000000..0xA000FFFF)")
    print(f"    Remaining : {format_size(code_free)} free")
    print(f"    Usage     : {draw_bar(sz_code, BOOT_CODE_MAX)}")
    print()

    # 3. Optional Secondary Payload Slot
    payload_free = max(0, PAYLOAD_MAX - sz_payload)
    print(f"[3] Secondary OS Payload Slot (Stage 1):")
    print(f"    Size Used : {format_size(sz_payload)}")
    print(f"    Partition : {format_size(PAYLOAD_MAX)} (0xA0010000..0xA009FFFF)")
    print(f"    Remaining : {format_size(payload_free)} free")
    print(f"    Usage     : {draw_bar(sz_payload, PAYLOAD_MAX)}")
    print("-------------------------------------------------------")

    # 4. Total Summary
    print(f"  TOTAL ROM USAGE SUMMARY:")
    print(f"    Total ROM Used : {format_size(total_actual_used)}")
    print(f"    Total Free ROM : {format_size(total_remaining_free)} safe headroom remaining")
    print(f"    Overall ROM    : {draw_bar(total_actual_used, TOTAL_ROM_CAPACITY)}")
    print("=======================================================")

    if total_actual_used > TOTAL_ROM_CAPACITY:
        print(f"  [ERROR] ROM OVERFLOW! Exceeded 2MB by {format_size(total_actual_used - TOTAL_ROM_CAPACITY)}.")
        return 1
    elif sz_scene > BOOT_SCENE_MAX:
        print(f"  [WARNING] boot_scene.bin exceeds allocated 1,408 KB partition!")
        return 1
    else:
        print(f"  [STATUS] OpenDC BIOS image is within safe limits for hardware & Flycast!")
        return 0

if __name__ == '__main__':
    sys.exit(check_rom_sizes())
