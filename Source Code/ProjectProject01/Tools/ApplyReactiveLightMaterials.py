"""Connect the authored single-player fixtures to their runtime emissive material.

Only the named gameplay map and a new runtime material are saved. Actor transforms, fixture meshes,
base intensity, sun/sky, game mode, spawns and AI remain authored. A map backup is kept under Saved.
"""
import json
import os
import shutil
import unreal

MAP = "/Game/MyProject/Level/LevelTest01"
MATERIAL = "/Game/MyProject/Lighting/M_ReactiveBulb"
project = os.path.abspath(unreal.Paths.project_dir())
directory = os.path.join(project, "Saved", "ReactiveLightQA")
os.makedirs(os.path.join(directory, "Backups"), exist_ok=True)
source = os.path.join(project, "Content", "MyProject", "Level", "LevelTest01.umap")
backup = os.path.join(directory, "Backups", "LevelTest01_BeforeReactiveBulb.umap")
if not os.path.isfile(backup):
    shutil.copy2(source, backup)

mat = unreal.load_asset(MATERIAL) if unreal.EditorAssetLibrary.does_asset_exist(MATERIAL) else None
if mat is None:
    unreal.EditorAssetLibrary.make_directory("/Game/MyProject/Lighting")
    mat = unreal.AssetToolsHelpers.get_asset_tools().create_asset("M_ReactiveBulb", "/Game/MyProject/Lighting", unreal.Material, unreal.MaterialFactoryNew())
    lib = unreal.MaterialEditingLibrary
    color = lib.create_material_expression(mat, unreal.MaterialExpressionVectorParameter)
    color.set_editor_property("parameter_name", "Color")
    color.set_editor_property("default_value", unreal.LinearColor(0.80, 0.84, 0.88, 1))
    lib.connect_material_property(color, "", unreal.MaterialProperty.MP_BASE_COLOR)
    gain = lib.create_material_expression(mat, unreal.MaterialExpressionScalarParameter)
    gain.set_editor_property("parameter_name", "EmissiveStrength")
    gain.set_editor_property("default_value", 5.0)
    multiply = lib.create_material_expression(mat, unreal.MaterialExpressionMultiply)
    lib.connect_material_expressions(color, "", multiply, "A")
    lib.connect_material_expressions(gain, "", multiply, "B")
    lib.connect_material_property(multiply, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    lib.layout_material_expressions(mat)
    lib.recompile_material(mat)
    unreal.EditorAssetLibrary.save_loaded_asset(mat)

world = unreal.EditorLoadingAndSavingUtils.load_map(MAP)
if world is None:
    raise RuntimeError("Unable to load " + MAP)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
sphere = unreal.load_asset("/Engine/BasicShapes/Sphere")
report = []
for actor in actors:
    if not isinstance(actor, unreal.MannequinReactiveLight):
        continue
    before = actor.get_actor_transform()
    intensity = actor.get_editor_property("light_on_intensity")
    bulb = actor.get_editor_property("bulb_mesh")
    light = actor.get_editor_property("reactive_spot_light")
    fixture = actor.get_editor_property("fixture_mesh")
    fixture_mesh = fixture.get_editor_property("static_mesh")
    # Replace the editor-only preview sphere with the equivalent runtime primitive, keeping its authored transform.
    mesh = bulb.get_editor_property("static_mesh")
    if mesh is None or mesh.get_path_name().startswith("/Engine/EditorMeshes/"):
        bulb.set_static_mesh(sphere)
    bulb.set_material(0, mat)
    bulb.set_editor_property("cast_shadow", False)
    # The original bulb encloses the spotlight and the cone's lower face is level with it. Place the optical
    # source just below the bulb, rather than letting the fixture self-shadow its own downward light.
    position = bulb.get_world_location()
    if bulb.get_editor_property("static_mesh").get_path_name().startswith("/Engine/BasicShapes/Cube"):
        centre = actor.get_actor_location() # The later linear diffuser conversion already uses an external optical source.
        light.set_world_location(unreal.Vector(centre.x, centre.y, centre.z - 10.0), False, False)
    else:
        light.set_world_location(unreal.Vector(position.x, position.y, position.z - 25.0), False, False)
    assert actor.get_actor_transform().equals(before), actor.get_actor_label() + ": actor moved"
    assert actor.get_editor_property("light_on_intensity") == intensity
    assert fixture.get_editor_property("static_mesh") == fixture_mesh
    report.append({"actor": actor.get_actor_label(), "base_intensity": intensity,
                   "bulb_material": MATERIAL, "bulb_mesh": bulb.get_editor_property("static_mesh").get_path_name(),
                   "position": str(actor.get_actor_location())})
if len(report) != 9:
    raise RuntimeError("Expected the nine authored fixtures; refusing to save: " + str(len(report)))
if not unreal.EditorLoadingAndSavingUtils.save_map(world, MAP):
    raise RuntimeError("Unable to save " + MAP)
with open(os.path.join(directory, "ExistingLightsApplied.json"), "w", encoding="utf-8") as output:
    json.dump({"map": MAP, "backup": backup, "fixtures": report}, output, ensure_ascii=False, indent=2)
unreal.log("[ReactiveLightApply] Connected 9 existing fixtures; authored transforms, housing meshes and base intensities preserved")
