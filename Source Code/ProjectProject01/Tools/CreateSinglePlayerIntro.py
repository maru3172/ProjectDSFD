"""Create the isolated single-player opening prototype using existing meshes.

Run with UnrealEditor-Cmd -run=pythonscript -script=".../Tools/CreateSinglePlayerIntro.py".
An existing prototype is updated in place only with PROJECT_SINGLE_INTRO_REFRESH=1.
"""
import math
import os
import unreal

MAP = "/Game/MyProject/Level/SinglePlayerIntro"
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
exists = unreal.EditorAssetLibrary.does_asset_exist(MAP)
if exists and os.environ.get("PROJECT_SINGLE_INTRO_REFRESH") != "1":
    raise RuntimeError("Single-player intro map already exists.")

world = (unreal.EditorLoadingAndSavingUtils.load_map(MAP) if exists
         else unreal.EditorLoadingAndSavingUtils.new_blank_map(False))
if not world:
    raise RuntimeError("Unable to load the single intro world")
world.get_world_settings().set_editor_property("default_game_mode", unreal.ProjectSingleIntroGameMode)
placed = {actor.get_actor_label(): actor for actor in actors.get_all_level_actors()} if exists else {}
cube = unreal.load_asset("/Engine/BasicShapes/Cube")
cylinder = unreal.load_asset("/Engine/BasicShapes/Cylinder")
floor = unreal.load_asset("/Game/MyProject/Testing/ReactiveLighting/M_TestFloor")
wall = unreal.load_asset("/Game/MyProject/Testing/ReactiveLighting/M_TestWall")
trim = unreal.load_asset("/Game/MyProject/Testing/ReactiveLighting/M_TestFixture")
bulb = unreal.load_asset("/Game/MyProject/Lighting/M_ReactiveBulb")
idle = unreal.load_asset("/Game/Characters/Mannequins/Anims/Unarmed/MM_Idle")
walk = unreal.load_asset("/Game/Characters/Mannequins/Anims/Unarmed/Walk/MF_Unarmed_Walk_Fwd")


def spawn(cls, label, position, yaw=0):
    if label in placed:
        actor = placed[label]
        if not isinstance(actor, cls):
            raise RuntimeError("Unexpected class for " + label)
        actor.set_actor_location_and_rotation(unreal.Vector(*position), unreal.Rotator(yaw=yaw), False, False)
        return actor
    actor = actors.spawn_actor_from_class(cls, unreal.Vector(*position), unreal.Rotator(yaw=yaw))
    if not actor:
        raise RuntimeError("Cannot spawn " + label)
    actor.set_actor_label(label)
    return actor


def block(label, position, scale, material=wall):
    actor = spawn(unreal.StaticMeshActor, label, position)
    actor.static_mesh_component.set_static_mesh(cube)
    actor.static_mesh_component.set_material(0, material)
    actor.set_actor_scale3d(unreal.Vector(*scale))
    return actor


def target(label, action, position, text="", item_id="None"):
    actor = spawn(unreal.ProjectSingleIntroTarget, label, position)
    actor.set_editor_property("action", action)
    actor.set_editor_property("item_id", item_id)
    actor.set_editor_property("item_name", text)
    return actor


def product(label, action, position, text, mesh, size):
    actor = target(label, action, position, text, label)
    item = actor.get_editor_property("item_mesh")
    item.set_static_mesh(mesh)
    item.set_material(0, trim)
    item.set_relative_scale3d(unreal.Vector(*size))
    actor.get_editor_property("interaction_collision").set_box_extent(unreal.Vector(43, 43, 35))
    return actor


def figure(label, action, position, model, yaw):
    actor = target(label, action, position)
    mesh = actor.get_editor_property("character_mesh")
    mesh.set_skeletal_mesh_asset(unreal.load_asset("/Game/Characters/Mannequins/Meshes/" + model))
    mesh.set_relative_rotation(unreal.Rotator(yaw=-90), False, False)
    actor.set_actor_rotation(unreal.Rotator(yaw=yaw), False)
    actor.set_editor_property("idle_animation", idle)
    actor.set_editor_property("walk_animation", walk)
    collision = actor.get_editor_property("interaction_collision")
    collision.set_relative_location(unreal.Vector(0, 0, 95), False, False)
    collision.set_box_extent(unreal.Vector(41, 41, 95))
    return actor


def sign(label, text, position, yaw=90, size=42):
    actor = spawn(unreal.TextRenderActor, label, position, yaw)
    actor.text_render.set_text(text)
    actor.text_render.set_world_size(size)
    actor.text_render.set_horizontal_alignment(unreal.HorizTextAligment.EHTA_CENTER)
    return actor


def trigger(label, tag, position, extent, yaw=0):
    actor = spawn(unreal.TriggerBox, label, position, yaw)
    actor.set_editor_property("tags", [tag])
    actor.get_component_by_class(unreal.BoxComponent).set_box_extent(unreal.Vector(*extent))
    return actor


def point_light(label, position, radius, tag=None, intensity=125.0):
    light = spawn(unreal.PointLight, label, position)
    component = light.light_component
    component.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
    component.set_editor_property("intensity_units", unreal.LightUnits.CANDELAS)
    component.set_editor_property("intensity", intensity)
    component.set_editor_property("attenuation_radius", radius)
    if tag:
        light.set_editor_property("tags", [tag])
    return light


def first_floor(position):
    x, y, z = position
    return (x, y, z - 900)


def ramp(label, x, start_y, end_y, start_z, end_z, width=260):
    run = end_y - start_y
    rise = end_z - start_z
    pitch = math.degrees(math.atan2(rise, abs(run)))
    yaw = 90 if run > 0 else -90
    middle_y = (start_y + end_y) / 2
    middle_z = (start_z + end_z) / 2
    length = math.hypot(run, rise) / 100
    slope = unreal.Rotator(pitch=pitch, yaw=yaw)
    deck = block(label, (x, middle_y, middle_z - 11), (length, width / 100, .22), floor)
    deck.set_actor_rotation(slope, False)
    for side in (-1, 1):
        rail = block(f"{label} rail {side}", (x + side * width / 2, middle_y, middle_z + 49),
                     (length, .13, .98), trim)
        rail.set_actor_rotation(slope, False)


block("Upper floor", (-900, -663, -20), (13, 47.8, .4), floor)
block("Ceiling", (-900, -663, 390), (13, 47.8, .4))
block("Store rear wall", first_floor((-1410, -3025, 185)), (2.75, .5, 3.7))
block("Entrance east facade", first_floor((-480, -3025, 185)), (4.45, .5, 3.7))
block("Entrance facade header", first_floor((-980, -3025, 346)), (5.95, .65, .64), trim)
block("Entrance west pillar", first_floor((-1298, -3060, 155)), (.34, .88, 3.1), trim)
block("Entrance east pillar", first_floor((-685, -3060, 155)), (.34, .88, 3.1), trim)
block("Entrance canopy", first_floor((-980, -3180, 385)), (7.2, 3.65, .32), trim)
block("Entrance plaza", first_floor((-1000, -4220, -20)), (25, 25, .4), floor)
block("Plaza west planter", first_floor((-2230, -4220, 46)), (.52, 24.5, .92), trim)
block("Plaza east planter", first_floor((230, -4220, 46)), (.52, 24.5, .92), trim)
block("Plaza rear planter", first_floor((-1000, -5460, 46)), (24.6, .5, .92), trim)
block("West entrance display window", first_floor((-1650, -3050, 145)), (2.1, .38, 2.5), trim)
block("East entrance display window", first_floor((-170, -3050, 145)), (2.1, .38, 2.5), trim)
block("Exterior west storefront wing", first_floor((-1840, -3000, 185)), (6.2, .52, 3.7))
block("Exterior east storefront wing", first_floor((5, -3000, 185)), (5.5, .52, 3.7))
block("Exterior upper stories", (-936, -2960, -72), (24.4, 1.1, 9.0))
for index, z in enumerate((-525, -75, 389), 1):
    block(f"Exterior facade belt {index}", (-936, -3040, z), (24.8, .92, .34), trim)
for row, z in enumerate((-305, 148), 1):
    for column, x in enumerate((-1920, -1590, -1260, -930, -600, -270, 60), 1):
        block(f"Exterior window {row}.{column}", (x, -3027, z), (2.65, .2, 2.4), trim)
        block(f"Exterior window mullion {row}.{column}", (x, -3041, z), (.09, .14, 2.4))
sign("Department store entrance sign", "DEPARTMENT STORE", first_floor((-980, -3370, 401)), yaw=-90, size=45)
point_light("Entrance canopy light", first_floor((-980, -3170, 330)), 860.0)
point_light("Plaza approach light", first_floor((-1060, -4150, 395)), 1300.0)
point_light("Exterior facade wash west", (-1710, -3720, -245), 1500.0, intensity=580.0)
point_light("Exterior facade wash east", (-180, -3720, -245), 1500.0, intensity=580.0)
point_light("Vestibule light", first_floor((-1050, -2890, 335)), 680.0)

block("First floor lobby", (-1285, -2110, -920), (20.5, 19.2, .4), floor)
block("First floor east wall", (-260, -2110, -715), (.4, 19.2, 3.7))
block("First floor west wall", (-2310, -2110, -715), (.4, 19.2, 3.7))
block("First floor back wall", (-1285, -1150, -715), (20.5, .4, 3.7))
block("First floor ceiling", (-890, -2485, -515), (12.6, 11.4, .4))
block("First floor directory stand", (-690, -2590, -840), (1.9, .45, 1.2), trim)
sign("First floor lobby sign", "1F   /   STAIRS", (-1090, -2210, -620), yaw=-90, size=48)
sign("First floor stair sign", "UP TO 2F", (-2050, -2400, -658), yaw=-90, size=37)
point_light("First floor lobby light", (-1120, -2390, -572), 830.0, intensity=320.0)
point_light("Lower staircase light", (-2075, -2090, -453), 570.0, intensity=260.0)
point_light("Second floor landing light", (-1920, -1320, -150), 405.0, intensity=220.0)
point_light("Upper staircase light", (-1730, -2040, 75), 345.0, intensity=210.0)
point_light("Third floor stair door light", (-1950, -2645, 275), 330.0, intensity=215.0)
ramp("Stair flight 1F to 2F", -2060, -2530, -1430, -900, -450)
block("Second floor stair landing", (-1885, -1292, -470), (6.4, 2.8, .4), floor)
block("Second floor landing guard", (-1885, -1155, -398), (6.4, .12, 1.04), trim)
sign("Second floor stair sign", "2F", (-1900, -1168, -195), yaw=-90, size=48)
ramp("Stair flight 2F to 3F", -1730, -1430, -2580, -450, 0)
block("Third floor stair landing", (-1935, -2690, -20), (8.3, 2.25, .4), floor)
block("Third floor landing guard", (-1935, -2800, 52), (8.3, .12, 1.04), trim)
sign("Third floor stair sign", "3F", (-1620, -2782, 264), yaw=90, size=49)
block("Third floor front wall", (-900, -3035, 185), (13, .4, 3.7))
block("Third floor west wall before stair door", (-1540, -2915, 185), (.4, 2.4, 3.7))
block("West wall", (-1540, -383, 185), (.4, 42.15, 3.7))
block("East wall", (-260, -663, 185), (.4, 47.8, 3.7))
block("End wall", (-900, 1715, 185), (13, .4, 3.7))
block("Hat counter", (-1240, -440, 48), (3.3, 1.4, .96), trim)
block("Accessories stand", (-525, -1630, 44), (2.8, 1.15, .88), trim)
block("Water stand", (-1280, -2000, 44), (2.8, 1.15, .88), trim)
block("Hat brim", (-1240, -440, 99), (.5, .5, .04), wall)
product("IntroHat", "Hat", (-1240, -440, 118), "모자 구매하기", cylinder, (.36, .36, .33))
product("IntroHandkerchief", "Purchase", (-525, -1630, 101), "손수건 구매하기", cube, (.35, .29, .1))
product("IntroWater", "Purchase", (-1280, -2000, 111), "생수 구매하기", cylinder, (.15, .15, .45))
sign("Hat department sign", "HATS", (-1080, -947, 330), yaw=-90)
for label, text, position, size in (
    ("Accessories sign", "ACCESSORIES", (-525, -1630, 260), 31),
    ("Water sign", "DRINKS", (-1280, -2000, 255), 31),
    ("Floor sign", "3F", (-1420, -2360, 300), 60),
):
    sign(label, text, position, yaw=-90, size=size).set_editor_property("tags", ["IntroFinaleShoppingSign"])

figure("Intro partner", "Partner", (-810, -460, 0), "SKM_Quinn_Simple", -130)
figure("Intro mannequin", "Mannequin", (-530, 795, 0), "SKM_Manny_Simple", 180)
block("Mannequin alcove side", (-375, 710, 185), (1.9, .3, 3.7), trim)
block("Mannequin alcove back", (-290, 810, 185), (.35, 2.1, 3.7), trim)
trigger("Hat counter arrival trigger", "IntroHatCounter", (-1240, -580, 110), (145, 50, 150))
trigger("Mannequin encounter trigger", "IntroMannequinPass", (-900, 825, 110), (55, 615, 150), 90)

shutter = block("Fire shutter placeholder", (-1080, -920, 470), (6.1, .3, 3), trim)
shutter.static_mesh_component.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
shutter.set_editor_property("tags", ["IntroShutter"])
if not shutter.get_attached_actors():
    for index, z in enumerate([355, 400, 445, 490, 535, 580]):
        strip = block(f"Shutter rib {index + 1}", (-1080, -899, z), (6.07, .1, .028), wall)
        strip.static_mesh_component.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
        strip.static_mesh_component.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
        strip.attach_to_actor(shutter, "", unreal.AttachmentRule.KEEP_WORLD,
                              unreal.AttachmentRule.KEEP_WORLD, unreal.AttachmentRule.KEEP_WORLD, False)
block("Shutter side column", (-750, -920, 185), (.5, .5, 3.7), trim)
block("Shop west frontage", (-1458, -920, 185), (1.45, .32, 3.7), wall)
block("Shop east frontage", (-495, -920, 185), (4.45, .32, 3.7), wall)
block("Shop entrance header", (-1080, -920, 340), (6.1, .38, .65), trim)
handle = product("Intro shutter control", "Shutter", (-785, -880, 137), "방화셔터 열기", cube, (.22, .1, .32))
handle.get_editor_property("interaction_collision").set_box_extent(unreal.Vector(29, 24, 35))
block("Service exit frame", (-530, 1690, 135), (3.05, .35, 2.7), trim)
if "IntroExit" in placed:
    actors.destroy_actor(placed["IntroExit"])
sign("Exit sign", "EXIT", (-530, 1664, 303), yaw=-90)

camera = spawn(unreal.CameraActor, "Finale corridor camera", (-1060, -701, 164), -90)
camera.set_editor_property("tags", ["IntroFinaleCamera"])
for cls, label, tag, xyz, pitch, yaw in (
    (unreal.CameraActor, "Opening pullback camera", "IntroOpeningCamera", (-1300, -4740, -585), 12, 74),
    (unreal.TargetPoint, "Opening wide shot", "IntroOpeningCameraEnd", (-1300, -5220, -430), 11, 78),
    (unreal.CameraActor, "Mannequin encounter camera", "IntroEncounterCamera", (-1245, 628, 155), -3, 15),
):
    shot = spawn(cls, label, xyz)
    shot.set_actor_rotation(unreal.Rotator(pitch=pitch, yaw=yaw), False)
    shot.set_editor_property("tags", [tag])
clusters = (
    (-1240, ((-110, 4), (72, -43), (149, 34))),
    (-1565, ((-191, -32), (-47, 35), (101, -12), (175, 42))),
    (-1900, ((-207, -22), (-94, 45), (16, -50), (103, 15), (207, 40))),
    (-2235, ((-203, 12), (-106, -55), (-8, 45), (80, -28), (155, 48), (220, -50))),
    (-2570, ((-221, 40), (-125, -55), (-34, 25), (55, -35), (150, 32), (237, -20))),
)
for section, (y, offsets) in enumerate(clusters, 1):
    light = point_light(f"Finale corridor light {section}", (-1070, y, 282), 545.0, "IntroFinaleLight", 300.0)
    light.light_component.set_editor_property("indirect_lighting_intensity", 0.0)
    light.light_component.set_editor_property("volumetric_scattering_intensity", 0.0)
    block(f"Finale ceiling housing {section}", (-1070, y, 352), (2.6, .23, .07), trim)
    panel = block(f"Finale diffuser {section}", (-1070, y, 344), (2.43, .19, .035), bulb)
    panel.set_editor_property("tags", ["IntroFinaleDiffuser"])
    panel.static_mesh_component.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
    panel.static_mesh_component.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
    panel.static_mesh_component.set_editor_property("affect_dynamic_indirect_lighting", False)
    if not light.get_attached_actors():
        panel.attach_to_actor(light, "", unreal.AttachmentRule.KEEP_WORLD,
                              unreal.AttachmentRule.KEEP_WORLD, unreal.AttachmentRule.KEEP_WORLD, False)
    for index, (dx, dy) in enumerate(offsets, 1):
        mannequin = figure(f"Finale mannequin {section}.{index}", "FinaleFigure",
                           (-1070 + dx, y + dy, 0), "SKM_Manny_Simple", 82 + (index * 7) % 19)
        mannequin.set_editor_property("walk_animation", None)

for index, y in enumerate([-450, 50, 570, 1120, 1515]):
    point_light(f"Ceiling light {index + 1}", (-900, y, 330), 700.0, intensity=485.0)
    block(f"Light housing {index + 1}", (-900, y, 355), (2.9, .27, .09), trim)

spawn(unreal.PlayerStart, "Intro player start", first_floor((-1060, -4630, 100)), 90)
opening_destination = spawn(unreal.TargetPoint, "Opening walk ends inside the entrance",
                            first_floor((-1060, -2770, 100)), 90)
opening_destination.set_editor_property("tags", ["IntroOpeningDestination"])
post = spawn(unreal.PostProcessVolume, "Fixed intro exposure", (0, 0, 0))
post.set_editor_property("unbound", True)
settings = post.get_editor_property("settings")
settings.set_editor_property("override_auto_exposure_method", True)
settings.set_editor_property("auto_exposure_method", unreal.AutoExposureMethod.AEM_MANUAL)
settings.set_editor_property("override_auto_exposure_apply_physical_camera_exposure", True)
settings.set_editor_property("auto_exposure_apply_physical_camera_exposure", False)
post.set_editor_property("settings", settings)

if not unreal.EditorLoadingAndSavingUtils.save_map(world, MAP):
    raise RuntimeError("Unable to save " + MAP)
unreal.log("[SingleIntro] Saved " + MAP)
