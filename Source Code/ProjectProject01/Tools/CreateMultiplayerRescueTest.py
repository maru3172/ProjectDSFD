"""Create the PvP rescue map, or update it with PROJECT_MULTIPLAYER_RESCUE_REFRESH=1."""
import math
import os
import unreal

MAP = "/Game/MyProject/Level/MultiplayerRescueTest"
exists = unreal.EditorAssetLibrary.does_asset_exist(MAP)
if exists and os.environ.get("PROJECT_MULTIPLAYER_RESCUE_REFRESH") != "1":
    raise RuntimeError("The rescue test map already exists")

world = (unreal.EditorLoadingAndSavingUtils.load_map(MAP) if exists
         else unreal.EditorLoadingAndSavingUtils.new_blank_map(False))
if not world:
    raise RuntimeError("Unable to load the rescue test map")
world.get_world_settings().set_editor_property("default_game_mode", unreal.ProjectMultiplayerRescueGameMode)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
placed = {actor.get_actor_label(): actor for actor in actors.get_all_level_actors()} if exists else {}
cube = unreal.load_asset("/Engine/BasicShapes/Cube")
floor = unreal.load_asset("/Game/MyProject/Testing/ReactiveLighting/M_TestFloor")
wall = unreal.load_asset("/Game/MyProject/Testing/ReactiveLighting/M_TestWall")
trim = unreal.load_asset("/Game/MyProject/Testing/ReactiveLighting/M_TestFixture")


def spawn(cls, name, xyz, yaw=0):
    if name in placed:
        actor = placed[name]
        if not isinstance(actor, cls):
            raise RuntimeError("Unexpected class for " + name)
        actor.set_actor_location_and_rotation(unreal.Vector(*xyz), unreal.Rotator(yaw=yaw), False, False)
        return actor
    actor = actors.spawn_actor_from_class(cls, unreal.Vector(*xyz), unreal.Rotator(yaw=yaw))
    if not actor:
        raise RuntimeError("Unable to spawn " + name)
    actor.set_actor_label(name)
    return actor


def box(name, xyz, scale, material=wall):
    actor = spawn(unreal.StaticMeshActor, name, xyz)
    actor.static_mesh_component.set_static_mesh(cube)
    actor.static_mesh_component.set_material(0, material)
    actor.set_actor_scale3d(unreal.Vector(*scale))
    return actor


def sign(name, text, xyz, size=48):
    actor = spawn(unreal.TextRenderActor, name, xyz, 180)
    actor.text_render.set_text(text)
    actor.text_render.set_world_size(size)
    actor.text_render.set_horizontal_alignment(unreal.HorizTextAligment.EHTA_CENTER)


box("Arena floor", (0,0,-25), (42,29,.5), floor)
box("Arena north wall", (0,1450,180), (42,.4,3.6))
box("Arena south wall", (0,-1450,180), (42,.4,3.6))
box("Arena west wall", (-2100,0,180), (.4,29,3.6))
box("Arena east wall", (2100,-558,180), (.4,17.85,3.6))
box("Arena east wall beyond stairs", (2100,1160,180), (.4,5.8,3.6))
box("Central cover", (0,-260,105), (.45,6.2,2.1), trim)
box("Shelving cover", (520,1000,112), (4.2,.45,2.24), trim)
run = 1170
rise = 440
pitch = -math.degrees(math.atan2(rise, run))
length = math.hypot(run, rise) / 100
stairs = box("Walk between 1F and B1", (2665,550,-230), (length,4.45,.22), floor)
stairs.set_actor_rotation(unreal.Rotator(pitch=pitch), False)
for side in (-1,1):
    rail = box(f"Basement ramp rail {side}", (2665,550 + side*231,-162), (length,.13,1.12), trim)
    rail.set_actor_rotation(unreal.Rotator(pitch=pitch), False)
box("Basement floor", (3815,550,-465), (11.35,12,.5), floor)
box("Basement far wall", (4380,550,-260), (.38,12,3.6))
box("Basement north wall", (3815,1150,-260), (11.35,.38,3.6))
box("Basement south wall", (3815,-50,-260), (11.35,.38,3.6))
box("Rear cage wall", (4130,550,-320), (.25,5,2.4), trim)
box("Left cage wall", (3895,310,-320), (4.8,.24,2.4), trim)
box("Right cage wall", (3895,790,-320), (4.8,.24,2.4), trim)
for index, y in enumerate(range(315,791,60)):
    bar = box(f"Cage bar {index}", (3675,y,-330), (.09,.1,2.2), trim)
    if 450 < y < 650:
        actors.destroy_actor(bar)

station = spawn(unreal.ProjectMultiplayerRescueStation, "Rescue station", (3610,490,-310))
panel = station.get_editor_property("item_mesh")
panel.set_static_mesh(cube)
panel.set_material(0, wall)
panel.set_relative_scale3d(unreal.Vector(.32,.52,.72))
gate = box("Rescue cage gate", (3675,550,-330), (.13,1.65,2.2), trim)
gate.static_mesh_component.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
gate.set_editor_property("tags", ["IntroRescueGate"])
if not station.get_attached_actors():
    gate.attach_to_actor(station, "", unreal.AttachmentRule.KEEP_WORLD,
                         unreal.AttachmentRule.KEEP_WORLD, unreal.AttachmentRule.KEEP_WORLD, False)
sign("Rescue sign", "B1  /  RESCUE", (3638,550,-169), 49)
sign("First floor stairs sign", "1F  /  B1", (2025,550,277), 50)
for obsolete in ("First floor return point", "Basement return to 1F"):
    if obsolete in placed:
        actors.destroy_actor(placed[obsolete])
return_sign = spawn(unreal.TextRenderActor, "Basement stairs to first floor sign", (3210,550,-165))
return_sign.text_render.set_text("1F")
return_sign.text_render.set_world_size(49)
return_sign.text_render.set_horizontal_alignment(unreal.HorizTextAligment.EHTA_CENTER)

for index, y in enumerate((-410,0,410),1):
    start = spawn(unreal.ProjectProject01RoleSpawnPoint, f"Survivor {index}", (-1630,y,100))
    start.set_editor_property("spawn_role", unreal.ProjectProject01SpawnRole.SURVIVOR)
for index in range(10):
    x = 340 + (index % 5) * 310
    y = -1100 + (index // 5) * 410
    start = spawn(unreal.ProjectProject01RoleSpawnPoint, f"Mannequin {index+1}", (x,y,100), 180)
    start.set_editor_property("spawn_role", unreal.ProjectProject01SpawnRole.MANNEQUIN)

escape = spawn(unreal.TriggerBox, "Escape", (1780,970,100))
escape.set_editor_property("tags", ["ProjectProject01Escape"])
escape.get_component_by_class(unreal.BoxComponent).set_box_extent(unreal.Vector(130,180,110))
exit_sign = spawn(unreal.TextRenderActor, "Exit sign", (2040,970,230), 180)
exit_sign.text_render.set_text("EXIT")
exit_sign.text_render.set_world_size(60)
for x in (-1350,0,1110):
    for y in (-775,650):
        light = spawn(unreal.PointLight, f"Arena light {x}.{y}", (x,y,380))
        light.light_component.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
        light.light_component.set_editor_property("intensity_units", unreal.LightUnits.CANDELAS)
        light.light_component.set_editor_property("intensity", 115.0)
        light.light_component.set_editor_property("attenuation_radius", 1260.0)
for name, position in (("Basement ramp light",(2750,550,95)), ("Basement cage light",(3850,550,-50))):
    light = spawn(unreal.PointLight, name, position)
    light.light_component.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
    light.light_component.set_editor_property("intensity_units", unreal.LightUnits.CANDELAS)
    light.light_component.set_editor_property("intensity", 125.0)
    light.light_component.set_editor_property("attenuation_radius", 880.0)

bounds = spawn(unreal.NavMeshBoundsVolume, "Arena navigation", (1140,0,-85))
bounds.set_actor_scale3d(unreal.Vector(33,14.5,7))
if not unreal.ProjectProject01TutorialEditorLibrary.prepare_navigation(world):
    raise RuntimeError("Unable to prepare navigation")
for nav in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.RecastNavMesh):
    nav.set_editor_property("runtime_generation", unreal.RuntimeGenerationType.DYNAMIC)
if not unreal.EditorLoadingAndSavingUtils.save_map(world, MAP):
    raise RuntimeError("Unable to save " + MAP)
unreal.log("[RescueTestMap] Saved " + MAP)
