"""Replace the nine old cone housings with slim commercial ceiling luminaires.

Keeps actor placement, original brightness, light cone and the reactive behaviour. Saves LevelTest01,
the existing fixture Blueprint defaults and one new housing material. Pre-conversion backups are retained.
"""
import json
import os
import shutil
import unreal

MAP = "/Game/MyProject/Level/LevelTest01"
ROOT = "/Game/MyProject/Lighting"
project = os.path.abspath(unreal.Paths.project_dir())
qa = os.path.join(project, "Saved", "ReactiveLightQA")
os.makedirs(os.path.join(qa, "Backups"), exist_ok=True)
backup = os.path.join(qa, "Backups", "LevelTest01_BeforeLinearFixtures.umap")
if not os.path.isfile(backup):
    shutil.copy2(os.path.join(project, "Content", "MyProject", "Level", "LevelTest01.umap"), backup)

path = ROOT + "/M_ReactiveFixture"
if unreal.EditorAssetLibrary.does_asset_exist(path):
    housing_material = unreal.load_asset(path)
else:
    unreal.EditorAssetLibrary.make_directory(ROOT)
    housing_material = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        "M_ReactiveFixture", ROOT, unreal.Material, unreal.MaterialFactoryNew())
    lib = unreal.MaterialEditingLibrary
    tint = lib.create_material_expression(housing_material, unreal.MaterialExpressionVectorParameter)
    tint.set_editor_property("parameter_name", "Color")
    tint.set_editor_property("default_value", unreal.LinearColor(0.045, 0.050, 0.058, 1))
    lib.connect_material_property(tint, "", unreal.MaterialProperty.MP_BASE_COLOR)
    for name, value, output in [("Roughness", 0.68, unreal.MaterialProperty.MP_ROUGHNESS),
                                ("Metallic", 0.25, unreal.MaterialProperty.MP_METALLIC)]:
        node = lib.create_material_expression(housing_material, unreal.MaterialExpressionScalarParameter)
        node.set_editor_property("parameter_name", name)
        node.set_editor_property("default_value", value)
        lib.connect_material_property(node, "", output)
    lib.layout_material_expressions(housing_material)
    lib.recompile_material(housing_material)
    unreal.EditorAssetLibrary.save_loaded_asset(housing_material)

diffuser_material = unreal.load_asset(ROOT + "/M_ReactiveBulb")
cube = unreal.load_asset("/Engine/BasicShapes/Cube")
if not diffuser_material or not cube:
    raise RuntimeError("Runtime diffuser material/primitive missing; map not changed")

# Also update the reusable Blueprint so newly placed lights no longer bring back the old cone.
blueprint_path = "/Game/MyProject/MannequinReactiveLight"
blueprint_file = os.path.join(project, "Content", "MyProject", "MannequinReactiveLight.uasset")
blueprint_backup = os.path.join(qa, "Backups", "MannequinReactiveLight_BeforeLinearFixtures.uasset")
if not os.path.isfile(blueprint_backup):
    shutil.copy2(blueprint_file, blueprint_backup)
blueprint = unreal.load_asset(blueprint_path)
defaults = unreal.get_default_object(unreal.EditorAssetLibrary.load_blueprint_class(blueprint_path))
for prop, scale, z, material in [
    ("fixture_mesh", (2.0, 0.28, 0.10), 0, housing_material),
    ("bulb_mesh", (1.85, 0.16, 0.035), -6, diffuser_material),
]:
    component = defaults.get_editor_property(prop)
    if not component.get_path_name().startswith(blueprint_path):
        raise RuntimeError("Refusing to edit a component outside the fixture Blueprint: " + component.get_path_name())
    component.set_static_mesh(cube)
    component.set_material(0, material)
    component.set_editor_property("relative_location", unreal.Vector(0, 0, z))
    component.set_editor_property("relative_rotation", unreal.Rotator())
    component.set_editor_property("relative_scale3d", unreal.Vector(*scale))
defaults.get_editor_property("bulb_mesh").set_editor_property("cast_shadow", False)
optical = defaults.get_editor_property("reactive_spot_light")
optical.set_editor_property("relative_location", unreal.Vector(0, 0, -10))
optical.set_editor_property("relative_rotation", unreal.Rotator(pitch=-90.0))
unreal.BlueprintEditorLibrary.compile_blueprint(blueprint)
if not unreal.EditorAssetLibrary.save_loaded_asset(blueprint, only_if_is_dirty=False):
    raise RuntimeError("Unable to save fixture Blueprint")

world = unreal.EditorLoadingAndSavingUtils.load_map(MAP)
if not world:
    raise RuntimeError("Cannot load " + MAP)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
report = []
for actor in actors:
    if not isinstance(actor, unreal.MannequinReactiveLight):
        continue
    before = actor.get_actor_transform()
    position = actor.get_actor_location()
    yaw = actor.get_actor_rotation().yaw
    intensity = actor.get_editor_property("light_on_intensity")
    housing = actor.get_editor_property("fixture_mesh")
    bulb = actor.get_editor_property("bulb_mesh")
    light = actor.get_editor_property("reactive_spot_light")
    # Explicit WORLD transforms compensate the authored -90deg pitch and 0.25 actor scale. The actor itself
    # stays in place; simply changing the mesh would otherwise stand the luminaire vertically or shrink it.
    housing.set_static_mesh(cube)
    housing.set_material(0, housing_material)
    housing.set_world_location(position, False, False)
    housing.set_world_rotation(unreal.Rotator(yaw=yaw), False, False)
    housing.set_world_scale3d(unreal.Vector(2.0, 0.28, 0.10))
    bulb.set_static_mesh(cube)
    bulb.set_material(0, diffuser_material)
    bulb.set_world_location(unreal.Vector(position.x, position.y, position.z - 6), False, False)
    bulb.set_world_rotation(unreal.Rotator(yaw=yaw), False, False)
    bulb.set_world_scale3d(unreal.Vector(1.85, 0.16, 0.035))
    bulb.set_editor_property("cast_shadow", False)
    # Keep the light outside the diffuser so the new housing does not occlude its own light.
    light.set_world_location(unreal.Vector(position.x, position.y, position.z - 10), False, False)
    light.set_world_rotation(unreal.Rotator(pitch=-90.0, yaw=yaw), False, False)
    assert actor.get_actor_transform().equals(before), actor.get_actor_label() + ": actor transform changed"
    assert actor.get_editor_property("light_on_intensity") == intensity
    report.append({"actor": actor.get_actor_label(), "position": [position.x, position.y, position.z],
                   "housing_cm": [200, 28, 10], "diffuser_cm": [185, 16, 3.5], "intensity": intensity})
if len(report) != 9:
    raise RuntimeError("Expected 9 authored fixtures; refusing to save: " + str(len(report)))
if not unreal.EditorLoadingAndSavingUtils.save_map(world, MAP):
    raise RuntimeError("Save failed")
with open(os.path.join(qa, "LinearFixturesApplied.json"), "w", encoding="utf-8") as output:
    json.dump({"map": MAP, "blueprint": blueprint_path, "backup": backup, "blueprint_backup": blueprint_backup,
               "fixtures": report}, output, ensure_ascii=False, indent=2)
unreal.log("[LinearFixtures] Saved nine slim ceiling fixtures; authored positions and intensity preserved")
