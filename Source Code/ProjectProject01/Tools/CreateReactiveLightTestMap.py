"""Create a standalone lighting comparison map. Existing maps/assets are never overwritten.

UnrealEditor-Cmd ProjectDFSD.uproject -run=pythonscript -script=<this file>
Only new assets in Testing/ReactiveLighting and the new test map are saved. No cooking/config changes.
"""
import json
import os
import unreal

MAP = "/Game/MyProject/Level/MannequinLightingTest"
ROOT = "/Game/MyProject/Testing/ReactiveLighting"
if unreal.EditorAssetLibrary.does_asset_exist(MAP):
    raise RuntimeError("Test map already exists; preserve authored changes: " + MAP)

tools = unreal.AssetToolsHelpers.get_asset_tools()
lib = unreal.MaterialEditingLibrary
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
unreal.EditorAssetLibrary.make_directory(ROOT)


def surface(name, color, roughness=0.7, lamp=False):
    path = ROOT + "/" + name
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        return unreal.load_asset(path)
    mat = tools.create_asset(name, ROOT, unreal.Material, unreal.MaterialFactoryNew())
    mat.set_editor_property("used_with_skeletal_mesh", True)
    tint = lib.create_material_expression(mat, unreal.MaterialExpressionVectorParameter)
    tint.set_editor_property("parameter_name", "Color")
    tint.set_editor_property("default_value", unreal.LinearColor(*color, 1))
    lib.connect_material_property(tint, "", unreal.MaterialProperty.MP_BASE_COLOR)
    rough = lib.create_material_expression(mat, unreal.MaterialExpressionScalarParameter)
    rough.set_editor_property("parameter_name", "Roughness")
    rough.set_editor_property("default_value", roughness)
    lib.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    if lamp:
        gain = lib.create_material_expression(mat, unreal.MaterialExpressionScalarParameter)
        gain.set_editor_property("parameter_name", "EmissiveStrength")
        gain.set_editor_property("default_value", 7.0)
        multiply = lib.create_material_expression(mat, unreal.MaterialExpressionMultiply)
        lib.connect_material_expressions(tint, "", multiply, "A")
        lib.connect_material_expressions(gain, "", multiply, "B")
        lib.connect_material_property(multiply, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    lib.layout_material_expressions(mat)
    lib.recompile_material(mat)
    unreal.EditorAssetLibrary.save_loaded_asset(mat)
    return mat


floor_mat = surface("M_TestFloor", (0.18, 0.19, 0.20), 0.32)
wall_mat = surface("M_TestWall", (0.25, 0.26, 0.27))
trim_mat = surface("M_TestFixture", (0.035, 0.039, 0.045), 0.45)
body_mat = surface("M_TestMannequin", (0.31, 0.33, 0.35), 0.35)
lamp_mat = unreal.load_asset("/Game/MyProject/Lighting/M_ReactiveBulb") or surface("M_TestLamp", (0.80, 0.84, 0.88), 0.4, True)
world = unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
world.get_world_settings().set_editor_property("default_game_mode", unreal.GameModeBase)
cube = unreal.load_asset("/Engine/BasicShapes/Cube")
body_mesh = unreal.load_asset("/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple")
idle = unreal.load_asset("/Game/Characters/Mannequins/Anims/Unarmed/MM_Idle")
if not cube or not body_mesh or not idle:
    raise RuntimeError("Required existing meshes/idle animation are missing; map not saved")


def spawn(cls, label, position, rotation=unreal.Rotator()):
    actor = actors.spawn_actor_from_class(cls, unreal.Vector(*position), rotation)
    if not actor:
        raise RuntimeError("Cannot spawn " + label)
    actor.set_actor_label(label)
    return actor


def block(label, position, scale, material=wall_mat):
    actor = spawn(unreal.StaticMeshActor, label, position)
    comp = actor.static_mesh_component
    comp.set_static_mesh(cube)
    comp.set_material(0, material)
    actor.set_actor_scale3d(unreal.Vector(*scale))
    return actor


def sign(label, text, position, size=52):
    actor = spawn(unreal.TextRenderActor, label, position, unreal.Rotator(yaw=180.0))
    comp = actor.text_render
    comp.set_text(text)
    comp.set_world_size(size)
    comp.set_horizontal_alignment(unreal.HorizTextAligment.EHTA_CENTER)
    comp.set_text_render_color(unreal.Color(195, 205, 210, 255))
    return actor


def fixture(label, position):
    actor = spawn(unreal.MannequinReactiveLight, label, position)
    actor.set_editor_property("detection_radius", 1500.0)
    actor.set_editor_property("light_on_intensity", 83786.43)
    actor.set_editor_property("minimum_brightness_fraction", 0.0)
    actor.set_editor_property("emissive_off_value", 0.0)
    housing = actor.get_editor_property("fixture_mesh")
    housing.set_static_mesh(cube)
    housing.set_material(0, trim_mat)
    housing.set_relative_scale3d(unreal.Vector(2.0, 0.28, 0.10))
    bulb = actor.get_editor_property("bulb_mesh")
    bulb.set_static_mesh(cube)
    bulb.set_material(0, lamp_mat)
    bulb.set_relative_location(unreal.Vector(0, 0, -6), False, False)
    bulb.set_relative_scale3d(unreal.Vector(1.85, 0.16, 0.035))
    light = actor.get_editor_property("reactive_spot_light")
    light.set_relative_rotation(unreal.Rotator(pitch=-90.0), False, False)
    light.set_relative_location(unreal.Vector(0, 0, -10), False, False)
    light.set_editor_property("attenuation_radius", 2015.5635)
    light.set_editor_property("inner_cone_angle", 42.0)
    light.set_editor_property("outer_cone_angle", 70.0)
    light.set_editor_property("light_color", unreal.Color(215, 224, 235, 255))
    return actor


# Four enclosed showrooms connected by a dim, safe comparison corridor. Centres are 42m apart,
# so walking into one room never accidentally counts bodies in the adjacent one (15m detection).
centres = [0, 4200, 8400, 12600]
counts = [0, 3, 6, 9]
block("Comparison_Corridor_Floor", (-1500, 6300, -25), (8, 148, 0.5), floor_mat)
block("Comparison_Corridor_BackWall", (-1930, 6300, 210), (0.4, 148, 4.7))
block("Comparison_Corridor_Ceiling", (-1500, 6300, 450), (8, 148, 0.2))
report = []
for room, (cy, count) in enumerate(zip(centres, counts)):
    prefix = "Stage_%d_%dBodies" % (room, count)
    block(prefix + "_Floor", (0, cy, -25), (22, 20, 0.5), floor_mat)
    block(prefix + "_Ceiling", (0, cy, 450), (22, 20, 0.2))
    block(prefix + "_FarWall", (1100, cy, 210), (0.4, 20, 4.7))
    block(prefix + "_LeftWall", (0, cy - 1000, 210), (22, 0.4, 4.7))
    block(prefix + "_RightWall", (0, cy + 1000, 210), (22, 0.4, 4.7))
    # Door opening at x=-1100, y=cy. Keep a route to all four stations and to the relief corridor.
    for dy in [-725, 725]:
        block(prefix + "_Entrance_%d" % dy, (-1100, cy + dy, 210), (0.4, 5.5, 4.7))
    for lx, ly in [(-500, -500), (-500, 500), (450, 0)]:
        fixture(prefix + "_ReactiveLamp_%d_%d" % (lx, ly), (lx, cy + ly, 424))
    # Reserved fill fixture stays off: the requested outage must not have an artificial residual glow.
    fill = spawn(unreal.PointLight, prefix + "_EmergencyFill", (-900, cy, 220))
    fill.light_component.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
    fill.light_component.set_editor_property("intensity", 0.0)
    fill.light_component.set_editor_property("attenuation_radius", 1300.0)
    fill.light_component.set_editor_property("cast_shadows", False)
    for n in range(count):
        x = 250 + (n // 3) * 250
        y = cy + ((n % 3) - 1) * 280
        body = spawn(unreal.MannequinAICharacter, prefix + "_Body_%02d" % (n + 1), (x, y, 96), unreal.Rotator(yaw=180.0))
        body.set_editor_property("auto_possess_ai", unreal.AutoPossessAI.DISABLED)
        body.set_editor_property("ai_controller_class", None)
        body.character_movement.set_editor_property("default_land_movement_mode", unreal.MovementMode.MOVE_NONE)
        mesh = body.mesh
        mesh.set_skeletal_mesh_asset(body_mesh)
        mesh.set_relative_location(unreal.Vector(0, 0, -90), False, False)
        mesh.set_relative_rotation(unreal.Rotator(yaw=-90.0), False, False)
        mesh.set_material(0, body_mat)
        mesh.set_material(1, body_mat)
        mesh.set_animation_mode(unreal.AnimationMode.ANIMATION_SINGLE_NODE)
        mesh.set_editor_property("animation_data", unreal.SingleAnimationPlayData(anim_to_play=idle, saved_looping=True, saved_playing=True))
    sign(prefix + "_RoomNumber", "%d  /  %d" % (room, count), (1050, cy, 280), 110)
    sign(prefix + "_CorridorSign", "%d BODIES" % count, (-1880, cy, 260), 60)
    # Corridor illumination is intentionally steady, useful for checking the slow recovery after leaving.
    hall = spawn(unreal.PointLight, prefix + "_CorridorLight", (-1500, cy, 330))
    hall.light_component.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
    hall.light_component.set_editor_property("intensity", 85.0)
    hall.light_component.set_editor_property("attenuation_radius", 650.0)
    hall.light_component.set_editor_property("cast_shadows", False)
    report.append({"bodies": count, "room_center": [0, cy, 160], "reactive_lights": 3})

start = spawn(unreal.PlayerStart, "LightingTest_Start", (-650, 0, 160))
# Fixed exposure makes a failing lamp stay dark rather than letting automatic exposure brighten it again.
post = spawn(unreal.PostProcessVolume, "LightingTest_ManualExposure", (0, 0, 0))
post.set_editor_property("unbound", True)
settings = post.get_editor_property("settings")
settings.set_editor_property("override_auto_exposure_method", True)
settings.set_editor_property("auto_exposure_method", unreal.AutoExposureMethod.AEM_MANUAL)
settings.set_editor_property("override_auto_exposure_apply_physical_camera_exposure", True)
settings.set_editor_property("auto_exposure_apply_physical_camera_exposure", False)
settings.set_editor_property("override_auto_exposure_bias", True)
settings.set_editor_property("auto_exposure_bias", 0.0)
settings.set_editor_property("override_motion_blur_amount", True)
settings.set_editor_property("motion_blur_amount", 0.0)
post.set_editor_property("settings", settings)

all_actors = actors.get_all_level_actors()
body_count = sum(isinstance(a, unreal.MannequinAICharacter) for a in all_actors)
light_count = sum(isinstance(a, unreal.MannequinReactiveLight) for a in all_actors)
if body_count != 18 or light_count != 12:
    raise RuntimeError("Unexpected test-map counts: %d bodies, %d fixtures" % (body_count, light_count))
if not unreal.EditorLoadingAndSavingUtils.save_map(world, MAP):
    raise RuntimeError("Unable to save " + MAP)
directory = os.path.join(unreal.Paths.project_saved_dir(), "ReactiveLightQA")
os.makedirs(directory, exist_ok=True)
with open(os.path.join(directory, "TestMap.json"), "w", encoding="utf-8") as output:
    json.dump({"map": MAP, "default_game_mode": "GameModeBase", "rooms": report,
               "bodies": body_count, "reactive_lights": light_count}, output, ensure_ascii=False, indent=2)
unreal.log("[ReactiveLightMap] Saved " + MAP + " (0 / 3 / 6 / 9 bodies, no login/server needed)")
