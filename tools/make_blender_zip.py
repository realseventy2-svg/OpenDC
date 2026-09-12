import zipfile
import os
import shutil

manifest_content = """schema_version = "1.0.0"

id = "io_scene_dcui"
name = "OpenDC 3D BIOS UI & Logic Nodes Exporter"
version = "1.0.0"
tagline = "Visual Scripting Blueprints & 3D UI Scene Exporter for Sega Dreamcast OpenDC BIOS"
maintainer = "OpenDC Team"
type = "add-on"

tags = ["Import-Export"]
blender_version_min = "4.0.0"
license = [
  "SPDX:GPL-2.0-or-later",
]
"""

addon_dir = 'tools/blender_addon/io_scene_dcui'
manifest_path = os.path.join(addon_dir, 'blender_manifest.toml')

# Write UTF-8 strictly without BOM
with open(manifest_path, 'wb') as f:
    f.write(manifest_content.encode('utf-8'))

zip_path = 'tools/io_scene_dcui.zip'
if os.path.exists(zip_path):
    os.remove(zip_path)

with zipfile.ZipFile(zip_path, 'w', zipfile.ZIP_DEFLATED) as z:
    for root, dirs, files in os.walk(addon_dir):
        for file in files:
            full_p = os.path.join(root, file)
            arc_name = os.path.relpath(full_p, addon_dir)
            z.write(full_p, arc_name)

# Also copy clean files to AppData Blender extension folder
appdata = os.environ.get('APPDATA', '')
if appdata:
    ext_dir = os.path.join(appdata, 'Blender Foundation', 'Blender', '5.2', 'extensions', 'user_default', 'io_scene_dcui')
    os.makedirs(ext_dir, exist_ok=True)
    for root, dirs, files in os.walk(addon_dir):
        for file in files:
            src = os.path.join(root, file)
            rel = os.path.relpath(src, addon_dir)
            dst = os.path.join(ext_dir, rel)
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            shutil.copy2(src, dst)

print(f"Created {zip_path} with license field and updated AppData.")
