import bpy
import bmesh

bpy.ops.object.select_all(action='SELECT')
bpy.ops.object.delete()

bpy.ops.object.text_add(location=(0, 0, 0))
t = bpy.context.active_object
t.data.body = \"BOOT DISC\"
t.data.size = 0.5
t.data.resolution_u = 1
t.data.extrude = 0.05
bpy.ops.object.convert(target='MESH')

bm = bmesh.new()
bm.from_mesh(t.data)
bmesh.ops.triangulate(bm, faces=bm.faces)
print(f'Triangles with resolution_u=1: {len(bm.faces)}')
bm.free()
