"""Create the initial practice map once. Never overwrite an authored tutorial map.

Run with UnrealEditor-Cmd -run=pythonscript -script=".../Tools/CreateMultiplayerTutorial.py".
This saves only a new editor map; it does not cook/package the game.
"""
import unreal

path = "/Game/MyProject/Level/MultiplayerTutorial"
if unreal.EditorAssetLibrary.does_asset_exist(path):
    raise RuntimeError("Tutorial map already exists. Edit it in the editor; it will not be overwritten.")

world = unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
world.get_world_settings().set_editor_property("default_game_mode", unreal.ProjectProject01TutorialGameMode)
cube = unreal.load_asset("/Engine/BasicShapes/Cube")


def spawn(cls, label, location, rotation=unreal.Rotator()):
    actor = actors.spawn_actor_from_class(cls, unreal.Vector(*location), rotation)
    if not actor:
        raise RuntimeError("Unable to spawn " + label)
    actor.set_actor_label(label)
    return actor


def block(label, location, scale):
    actor = spawn(unreal.StaticMeshActor, label, location)
    component = actor.static_mesh_component
    component.set_static_mesh(cube)
    actor.set_actor_scale3d(unreal.Vector(*scale))
    return actor


block("Tutorial_Floor", (0, 0, -30), (40, 28, 0.6))
block("Tutorial_Wall_Left", (0, -1450, 180), (40, 1, 4))
block("Tutorial_Wall_Right", (0, 1450, 180), (40, 1, 4))
block("Tutorial_Wall_Back", (-2050, 0, 180), (1, 30, 4))
block("Tutorial_Wall_Front", (2050, 0, 180), (1, 30, 4))
sun = spawn(unreal.DirectionalLight, "Tutorial_Light", (0, 0, 700), unreal.Rotator(-55, -35, 0))
sun.light_component.set_editor_property("intensity", 5.0)
spawn(unreal.SkyLight, "Tutorial_SkyLight", (0, 0, 600))

plan = spawn(unreal.ProjectProject01TutorialPlan, "TutorialPlan_EditStepsAndMessages", (0, 0, 0))
for index, location in enumerate([(-1000, 0, 100), (-1000, -250, 100)]):
    point = spawn(unreal.ProjectProject01RoleSpawnPoint, "Tutorial_SurvivorSpawn_" + str(index), location)
    point.set_editor_property("spawn_role", unreal.ProjectProject01SpawnRole.SURVIVOR)
for index in range(10):
    location = (-700 + (index % 5) * 350, 500 + (index // 5) * 450, 100)
    point = spawn(unreal.ProjectProject01RoleSpawnPoint, "Tutorial_MannequinSpawn_" + str(index), location)
    point.set_editor_property("spawn_role", unreal.ProjectProject01SpawnRole.MANNEQUIN)

exit_trigger = spawn(unreal.ProjectProject01ObjectiveTrigger, "Tutorial_Escape_Condition", (1550, 0, 130))
exit_trigger.set_editor_property("required_objective", "Survivor.Escape")
exit_trigger.set_editor_property("tutorial_event", "Tutorial.Escape")
exit_trigger.volume.set_box_extent(unreal.Vector(180, 230, 150))
block("Tutorial_Exit_Platform", (1550, 0, 4), (3.5, 4.5, 0.08))
sign = spawn(unreal.TextRenderActor, "Tutorial_Exit_Sign", (1750, 0, 160), unreal.Rotator(0, 180, 0))
sign.text_render.set_text("탈출 / EXIT")
sign.text_render.set_world_size(90)

# An actor factory supplies the volume brush. Dynamic generation is scoped to
# this new map, so the user's single/multiplayer navigation settings stay intact.
nav_bounds = spawn(unreal.NavMeshBoundsVolume, "Tutorial_NavBounds", (0, 0, 150))
nav_bounds.set_actor_scale3d(unreal.Vector(21, 15, 5))
origin, extent = nav_bounds.get_actor_bounds(False)
if extent.x < 100 or extent.y < 100:
    raise RuntimeError("Navigation volume has no brush; map was not saved.")
if not unreal.ProjectProject01TutorialEditorLibrary.prepare_navigation(world):
    raise RuntimeError("Unable to initialize navigation for the new editor map")
navs = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.RecastNavMesh)
if not navs:
    raise RuntimeError("Navigation data was not created for the bounds; map was not saved.")
for nav in navs:
    nav.set_editor_property("runtime_generation", unreal.RuntimeGenerationType.DYNAMIC)

if not unreal.EditorLoadingAndSavingUtils.save_map(world, path):
    raise RuntimeError("Unable to save the new tutorial map")
unreal.log("[TutorialMap] Created " + path + "; edit TutorialPlan and Tutorial_Escape_Condition to author content.")
