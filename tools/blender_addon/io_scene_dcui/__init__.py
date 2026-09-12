bl_info = {
    "name": "OpenDC 3D BIOS UI & Logic Nodes Exporter",
    "author": "OpenDC Team",
    "version": (1, 0, 0),
    "blender": (3, 0, 0),
    "location": "File > Export > OpenDC BIOS UI (.dcui)",
    "description": "Visual Scripting Blueprints & 3D UI Scene Exporter for Sega Dreamcast OpenDC BIOS",
    "category": "Import-Export",
}

import bpy
from . import nodes
from . import panels
from . import exporter

def menu_func_export(self, context):
    self.layout.operator(exporter.ExportDCUI.bl_idname, text="OpenDC BIOS UI (.dcui)")

def register():
    nodes.register()
    panels.register()
    exporter.register()
    bpy.types.TOPBAR_MT_file_export.append(menu_func_export)

def unregister():
    bpy.types.TOPBAR_MT_file_export.remove(menu_func_export)
    exporter.unregister()
    panels.unregister()
    nodes.unregister()

if __name__ == "__main__":
    register()
