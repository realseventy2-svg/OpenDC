import bpy

class OPENDC_PT_ObjectProperties(bpy.types.Panel):
    bl_label = "OpenDC 3D UI Properties"
    bl_idname = "OPENDC_PT_ObjectProperties"
    bl_space_type = 'VIEW_3D'
    bl_region_type = 'UI'
    bl_category = 'OpenDC UI'

    def draw(self, context):
        layout = self.layout
        obj = context.active_object
        if not obj:
            layout.label(text="Select an object to configure UI properties.")
            return

        box = layout.box()
        box.label(text=f"Object: {obj.name}", icon='OBJECT_DATA')

        box.prop(obj, "opendc_node_type", text="Node Type")
        if obj.opendc_node_type == 'INTERACTABLE':
            b_box = box.box()
            b_box.label(text="Navigation Links (Spatial)", icon='CON_LOCLIKE')
            b_box.prop(obj, "opendc_nav_up", text="D-Pad Up")
            b_box.prop(obj, "opendc_nav_down", text="D-Pad Down")
            b_box.prop(obj, "opendc_nav_left", text="D-Pad Left")
            b_box.prop(obj, "opendc_nav_right", text="D-Pad Right")

        if obj.opendc_node_type == 'TEXT_ANCHOR':
            t_box = box.box()
            t_box.label(text="Dynamic Text Binding", icon='FONT_DATA')
            t_box.prop(obj, "opendc_text_binding", text="Binding Key")

        box.prop(obj, "opendc_logic_tree", text="Logic Blueprint")

def register():
    bpy.types.Object.opendc_node_type = bpy.props.EnumProperty(
        name="OpenDC Node Type",
        items=[
            ('CONTAINER', 'Container / Group', 'Hierarchical transform group'),
            ('MESH', 'Static 3D Mesh', 'Standard 3D renderable model'),
            ('INTERACTABLE', 'Interactive Button / Orb', 'Selectable UI element'),
            ('CAMERA', 'Camera Anchor', 'Viewport viewpoint'),
            ('TEXT_ANCHOR', '3D Text Label Anchor', 'Dynamic text overlay'),
        ],
        default='MESH'
    )
    bpy.types.Object.opendc_nav_up = bpy.props.PointerProperty(name="Nav Up", type=bpy.types.Object)
    bpy.types.Object.opendc_nav_down = bpy.props.PointerProperty(name="Nav Down", type=bpy.types.Object)
    bpy.types.Object.opendc_nav_left = bpy.props.PointerProperty(name="Nav Left", type=bpy.types.Object)
    bpy.types.Object.opendc_nav_right = bpy.props.PointerProperty(name="Nav Right", type=bpy.types.Object)
    bpy.types.Object.opendc_text_binding = bpy.props.StringProperty(
        name="Text Binding",
        description="e.g. disc.title, sys.clock, maple.port_a",
        default=""
    )
    bpy.types.Object.opendc_logic_tree = bpy.props.PointerProperty(
        name="Logic Blueprint",
        type=bpy.types.NodeTree
    )
    bpy.utils.register_class(OPENDC_PT_ObjectProperties)

def unregister():
    try:
        bpy.utils.unregister_class(OPENDC_PT_ObjectProperties)
    except Exception:
        pass
    for attr in ["opendc_node_type", "opendc_nav_up", "opendc_nav_down", "opendc_nav_left", "opendc_nav_right", "opendc_text_binding", "opendc_logic_tree"]:
        try:
            delattr(bpy.types.Object, attr)
        except Exception:
            pass

