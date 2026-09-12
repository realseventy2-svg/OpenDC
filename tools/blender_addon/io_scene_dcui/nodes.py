import bpy
from bpy.types import NodeTree, Node, NodeSocket, Menu

# ----------------------------------------------------------------------------
# Custom Node Tree
# ----------------------------------------------------------------------------
class OPENDC_LogicTree(NodeTree):
    bl_idname = 'OpenDCLogicTreeType'
    bl_label = 'OpenDC Logic Nodes (Blueprint)'
    bl_icon = 'NODETREE'

# ----------------------------------------------------------------------------
# Custom Sockets
# ----------------------------------------------------------------------------
class OPENDC_SocketFlow(NodeSocket):
    bl_idname = 'OpenDCSocketFlow'
    bl_label = 'Execution Flow'
    def draw(self, context, layout, node, text):
        layout.label(text=text)
    def draw_color(self, context, node):
        return (1.0, 1.0, 1.0, 1.0) # White execution line

class OPENDC_SocketTarget(NodeSocket):
    bl_idname = 'OpenDCSocketTarget'
    bl_label = 'Target Object'
    def draw(self, context, layout, node, text):
        layout.label(text=text)
    def draw_color(self, context, node):
        return (0.2, 0.8, 0.4, 1.0) # Green target line

# ----------------------------------------------------------------------------
# Base OpenDC Logic Node
# ----------------------------------------------------------------------------
class OPENDC_BaseNode(Node):
    @classmethod
    def poll(cls, ntree):
        return ntree.bl_idname == 'OpenDCLogicTreeType'

# ----------------------------------------------------------------------------
# 1. Event Nodes (Triggers)
# ----------------------------------------------------------------------------
class OPENDC_EventButtonNode(OPENDC_BaseNode):
    bl_idname = 'OpenDCEventButtonNodeType'
    bl_label = 'Event: On Controller Button'
    bl_icon = 'EVENT_A'

    button: bpy.props.EnumProperty(
        name="Button",
        items=[
            ('PRESS_A', '(A) Button (Select/Confirm)', 'A button'),
            ('PRESS_B', '(B) Button (Back/Cancel)', 'B button'),
            ('PRESS_X', '(X) Button', 'X button'),
            ('PRESS_Y', '(Y) Button', 'Y button'),
            ('PRESS_START', '(START) Button (Fast-Boot)', 'Start button'),
            ('NAV_UP', 'D-Pad UP', 'Directional up'),
            ('NAV_DOWN', 'D-Pad DOWN', 'Directional down'),
            ('NAV_LEFT', 'D-Pad LEFT', 'Directional left'),
            ('NAV_RIGHT', 'D-Pad RIGHT', 'Directional right'),
        ],
        default='PRESS_A'
    )
    target_object: bpy.props.PointerProperty(
        name="Target Mesh",
        type=bpy.types.Object
    )

    def init(self, context):
        self.outputs.new('OpenDCSocketFlow', 'Execute')

    def draw_buttons(self, context, layout):
        layout.prop(self, "button", text="")
        layout.prop(self, "target_object", text="When Focused")

class OPENDC_EventFocusNode(OPENDC_BaseNode):
    bl_idname = 'OpenDCEventFocusNodeType'
    bl_label = 'Event: On UI Focus / Hover'
    bl_icon = 'RESTRICT_SELECT_OFF'

    target_object: bpy.props.PointerProperty(
        name="Target Mesh",
        type=bpy.types.Object
    )

    def init(self, context):
        self.outputs.new('OpenDCSocketFlow', 'On Focus Enter')
        self.outputs.new('OpenDCSocketFlow', 'On Focus Exit')

    def draw_buttons(self, context, layout):
        layout.prop(self, "target_object", text="UI Element")

class OPENDC_EventInitNode(OPENDC_BaseNode):
    bl_idname = 'OpenDCEventInitNodeType'
    bl_label = 'Event: On Scene Start'
    bl_icon = 'PLAY'

    def init(self, context):
        self.outputs.new('OpenDCSocketFlow', 'Execute')

class OPENDC_EventDiscNode(OPENDC_BaseNode):
    bl_idname = 'OpenDCEventDiscNodeType'
    bl_label = 'Event: On Disc State Change'
    bl_icon = 'DISC'

    def init(self, context):
        self.outputs.new('OpenDCSocketFlow', 'On Disc Inserted')
        self.outputs.new('OpenDCSocketFlow', 'On Disc Removed')

# ----------------------------------------------------------------------------
# 2. Action Nodes (Animations & Visuals)
# ----------------------------------------------------------------------------
class OPENDC_ActionPlayAnimNode(OPENDC_BaseNode):
    bl_idname = 'OpenDCActionPlayAnimNodeType'
    bl_label = 'Action: Play Animation'
    bl_icon = 'ACTION'

    target_object: bpy.props.PointerProperty(name="Target", type=bpy.types.Object)
    anim_clip: bpy.props.StringProperty(name="Clip Name", default="Idle_Loop")
    loop: bpy.props.BoolProperty(name="Looping", default=False)

    def init(self, context):
        self.inputs.new('OpenDCSocketFlow', 'In')
        self.outputs.new('OpenDCSocketFlow', 'Out')

    def draw_buttons(self, context, layout):
        layout.prop(self, "target_object")
        layout.prop(self, "anim_clip")
        layout.prop(self, "loop")

class OPENDC_ActionCameraGotoNode(OPENDC_BaseNode):
    bl_idname = 'OpenDCActionCameraGotoNodeType'
    bl_label = 'Action: Camera Transition'
    bl_icon = 'CAMERA_DATA'

    target_camera: bpy.props.PointerProperty(name="Target Camera/Node", type=bpy.types.Object)
    duration_frames: bpy.props.IntProperty(name="Duration (Frames)", default=30, min=1, max=300)
    easing: bpy.props.EnumProperty(
        name="Easing",
        items=[
            ('SMOOTH', 'Smooth (Ease-In-Out)', 'Smooth acceleration and deceleration'),
            ('LINEAR', 'Linear', 'Constant speed'),
            ('BOUNCE', 'Spring / Bounce', 'Elastic overshoot'),
        ],
        default='SMOOTH'
    )

    def init(self, context):
        self.inputs.new('OpenDCSocketFlow', 'In')
        self.outputs.new('OpenDCSocketFlow', 'Out')

    def draw_buttons(self, context, layout):
        layout.prop(self, "target_camera")
        layout.prop(self, "duration_frames")
        layout.prop(self, "easing")

class OPENDC_ActionPlaySoundNode(OPENDC_BaseNode):
    bl_idname = 'OpenDCActionPlaySoundNodeType'
    bl_label = 'Action: Play Audio Chime'
    bl_icon = 'SPEAKER'

    sound_id: bpy.props.EnumProperty(
        name="Sound FX",
        items=[
            ('CLICK', 'Click / Tick (Navigation)', 'Light navigation tick'),
            ('CONFIRM', 'Confirm / Select', 'Bright confirmation chime'),
            ('CANCEL', 'Cancel / Back', 'Back / Cancel tone'),
            ('BOOT_CHIME', 'Dreamcast Boot Chime', 'Classic startup chime'),
            ('ERROR', 'Error Buzzer', 'Error tone'),
        ],
        default='CONFIRM'
    )

    def init(self, context):
        self.inputs.new('OpenDCSocketFlow', 'In')
        self.outputs.new('OpenDCSocketFlow', 'Out')

    def draw_buttons(self, context, layout):
        layout.prop(self, "sound_id", text="")

# ----------------------------------------------------------------------------
# 3. System Console Service Nodes
# ----------------------------------------------------------------------------
class OPENDC_ServiceBootDiscNode(OPENDC_BaseNode):
    bl_idname = 'OpenDCServiceBootDiscNodeType'
    bl_label = 'System: Boot GD-ROM Disc'
    bl_icon = 'PLAY'

    def init(self, context):
        self.inputs.new('OpenDCSocketFlow', 'In')

    def draw_buttons(self, context, layout):
        layout.label(text="Launches Game / Disc", icon='DISC')

class OPENDC_ServiceGotoScreenNode(OPENDC_BaseNode):
    bl_idname = 'OpenDCServiceGotoScreenNodeType'
    bl_label = 'System: Open BIOS Screen'
    bl_icon = 'WINDOW'

    target_screen: bpy.props.EnumProperty(
        name="Screen",
        items=[
            ('SYSINFO', 'Hardware & Diagnostics', 'View CPU/PVR/AICA Specs'),
            ('MAPLE', 'Maple Bus & VMU Manager', 'Inspect Peripherals & Saves'),
            ('FLASHROM', 'FlashROM Configuration', 'Language, Audio & NVRAM'),
            ('MEMORY', 'Memory Hex Inspector', 'Live SDRAM & ROM Inspector'),
            ('TEST', 'Audio/Video DAC Test', 'Hardware test pattern'),
        ],
        default='SYSINFO'
    )

    def init(self, context):
        self.inputs.new('OpenDCSocketFlow', 'In')

    def draw_buttons(self, context, layout):
        layout.prop(self, "target_screen", text="")

class OPENDC_ServiceRebootNode(OPENDC_BaseNode):
    bl_idname = 'OpenDCServiceRebootNodeType'
    bl_label = 'System: Reboot Console'
    bl_icon = 'RECOVER_LAST'

    def init(self, context):
        self.inputs.new('OpenDCSocketFlow', 'In')

# ----------------------------------------------------------------------------
# 4. Logic & Flow Nodes
# ----------------------------------------------------------------------------
class OPENDC_BranchDiscNode(OPENDC_BaseNode):
    bl_idname = 'OpenDCBranchDiscNodeType'
    bl_label = 'Logic: If Disc Present'
    bl_icon = 'QUESTION'

    def init(self, context):
        self.inputs.new('OpenDCSocketFlow', 'In')
        self.outputs.new('OpenDCSocketFlow', 'If Disc Present')
        self.outputs.new('OpenDCSocketFlow', 'If Drive Empty')

class OPENDC_DelayNode(OPENDC_BaseNode):
    bl_idname = 'OpenDCDelayNodeType'
    bl_label = 'Logic: Delay Frames'
    bl_icon = 'TIME'

    frames: bpy.props.IntProperty(name="Frames (60fps)", default=60, min=1, max=600)

    def init(self, context):
        self.inputs.new('OpenDCSocketFlow', 'In')
        self.outputs.new('OpenDCSocketFlow', 'Out')

    def draw_buttons(self, context, layout):
        layout.prop(self, "frames")

# ----------------------------------------------------------------------------
# Add Menus (Shift + A)
# ----------------------------------------------------------------------------
class OPENDC_MT_node_add_events(Menu):
    bl_label = "Events (Triggers)"
    bl_idname = "OPENDC_MT_node_add_events"
    def draw(self, context):
        layout = self.layout
        layout.operator("node.add_node", text="On Controller Button").type = "OpenDCEventButtonNodeType"
        layout.operator("node.add_node", text="On UI Focus / Hover").type = "OpenDCEventFocusNodeType"
        layout.operator("node.add_node", text="On Scene Start").type = "OpenDCEventInitNodeType"
        layout.operator("node.add_node", text="On Disc Change").type = "OpenDCEventDiscNodeType"

class OPENDC_MT_node_add_actions(Menu):
    bl_label = "Actions (Visuals & Audio)"
    bl_idname = "OPENDC_MT_node_add_actions"
    def draw(self, context):
        layout = self.layout
        layout.operator("node.add_node", text="Play Animation").type = "OpenDCActionPlayAnimNodeType"
        layout.operator("node.add_node", text="Camera Transition").type = "OpenDCActionCameraGotoNodeType"
        layout.operator("node.add_node", text="Play Audio Chime").type = "OpenDCActionPlaySoundNodeType"

class OPENDC_MT_node_add_services(Menu):
    bl_label = "System Services"
    bl_idname = "OPENDC_MT_node_add_services"
    def draw(self, context):
        layout = self.layout
        layout.operator("node.add_node", text="Boot GD-ROM Disc").type = "OpenDCServiceBootDiscNodeType"
        layout.operator("node.add_node", text="Open BIOS Screen").type = "OpenDCServiceGotoScreenNodeType"
        layout.operator("node.add_node", text="Reboot Console").type = "OpenDCServiceRebootNodeType"

class OPENDC_MT_node_add_logic(Menu):
    bl_label = "Logic & Flow"
    bl_idname = "OPENDC_MT_node_add_logic"
    def draw(self, context):
        layout = self.layout
        layout.operator("node.add_node", text="If Disc Present").type = "OpenDCBranchDiscNodeType"
        layout.operator("node.add_node", text="Delay Frames").type = "OpenDCDelayNodeType"

def draw_add_menu(self, context):
    if getattr(context.space_data, "tree_type", "") == 'OpenDCLogicTreeType':
        layout = self.layout
        layout.menu("OPENDC_MT_node_add_events", icon='EVENT_A')
        layout.menu("OPENDC_MT_node_add_actions", icon='ACTION')
        layout.menu("OPENDC_MT_node_add_services", icon='WINDOW')
        layout.menu("OPENDC_MT_node_add_logic", icon='QUESTION')

# ----------------------------------------------------------------------------
# Registration
# ----------------------------------------------------------------------------
classes = (
    OPENDC_LogicTree,
    OPENDC_SocketFlow,
    OPENDC_SocketTarget,
    OPENDC_EventButtonNode,
    OPENDC_EventFocusNode,
    OPENDC_EventInitNode,
    OPENDC_EventDiscNode,
    OPENDC_ActionPlayAnimNode,
    OPENDC_ActionCameraGotoNode,
    OPENDC_ActionPlaySoundNode,
    OPENDC_ServiceBootDiscNode,
    OPENDC_ServiceGotoScreenNode,
    OPENDC_ServiceRebootNode,
    OPENDC_BranchDiscNode,
    OPENDC_DelayNode,
    OPENDC_MT_node_add_events,
    OPENDC_MT_node_add_actions,
    OPENDC_MT_node_add_services,
    OPENDC_MT_node_add_logic,
)

def register():
    for cls in classes:
        bpy.utils.register_class(cls)
    bpy.types.NODE_MT_add.append(draw_add_menu)

def unregister():
    try:
        bpy.types.NODE_MT_add.remove(draw_add_menu)
    except Exception:
        pass
    for cls in reversed(classes):
        try:
            bpy.utils.unregister_class(cls)
        except Exception:
            pass

