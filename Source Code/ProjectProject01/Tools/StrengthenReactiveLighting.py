"""Apply the stronger requested lighting presentation to the fixture Blueprint and existing/test maps."""
import json
import math
import os
import shutil
import unreal

root = os.path.abspath(unreal.Paths.project_dir())
qa = os.path.join(root, "Saved", "ReactiveLightQA")
backups = os.path.join(qa, "Backups")
os.makedirs(backups, exist_ok=True)


def backup(relative, name):
    destination = os.path.join(backups, name)
    if not os.path.exists(destination):
        shutil.copy2(os.path.join(root, "Content", *relative.split("/")), destination)


def configure(actor, floor_z=None):
    # The requested reference is the CORRIDOR point light (85 cd at 3.30m),
    # not the dimmer reactive showroom light. Unitless spotlights use a legacy x16 scale;
    # candelas use x10000 in the renderer. Keep fixture units and convert the reference.
    light = actor.get_editor_property("reactive_spot_light")
    height = light.get_world_location().z - floor_z if floor_z is not None else 414.0
    reference_height, reference_radius = 330.0, 2000.0
    if (height <= 0 or not light.get_editor_property("use_inverse_squared_falloff")
            or light.get_editor_property("intensity_units") != unreal.LightUnits.UNITLESS):
        raise RuntimeError("Cannot calibrate fixture: " + actor.get_actor_label())
    radius = math.sqrt(reference_radius ** 2 + height ** 2 - reference_height ** 2)
    cutoff = lambda h, r: (1.0 - (h / r) ** 4) ** 2
    intensity = (85.0 * 10000.0 / 16.0 * (height ** 2 + 1.0) / (reference_height ** 2 + 1.0)
                 * cutoff(reference_height, reference_radius) / cutoff(height, radius))
    light.set_attenuation_radius(radius)
    actor.set_editor_property("light_on_intensity", intensity)
    actor.set_editor_property("emissive_on_value", 7.0)
    actor.set_editor_property("minimum_brightness_fraction", 0.0)
    actor.set_editor_property("emissive_off_value", 0.0)
    actor.set_editor_property("electrical_sound_volume", 0.45)


surface_only = "-MatchReactiveSurfaces" in unreal.SystemLibrary.get_command_line()
if not surface_only:
    bp_path = "/Game/MyProject/MannequinReactiveLight"
    backup("MyProject/MannequinReactiveLight.uasset", "MannequinReactiveLight_BeforeStrongerProfile.uasset")
    bp = unreal.load_asset(bp_path)
    configure(unreal.get_default_object(unreal.EditorAssetLibrary.load_blueprint_class(bp_path)))
    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    if not unreal.EditorAssetLibrary.save_loaded_asset(bp, only_if_is_dirty=False):
        raise RuntimeError("Cannot save fixture defaults")

result = []
for name, expected in ([("LevelTest01", 9)] if surface_only else [("LevelTest01", 9), ("MannequinLightingTest", 12)]):
    path = "/Game/MyProject/Level/" + name
    backup("MyProject/Level/" + name + ".umap", name + "_BeforeStrongerProfile.umap")
    if name == "LevelTest01":
        backup("MyProject/Level/" + name + ".umap", name + "_BeforeSurfaceBrightness.umap")
    backup("MyProject/Level/" + name + ".umap", name + "_BeforeCorridorBrightness.umap")
    backup("MyProject/Level/" + name + ".umap", name + "_BeforeFullOutage.umap")
    world = unreal.EditorLoadingAndSavingUtils.load_map(path)
    if not world:
        raise RuntimeError("Cannot load " + path)
    found = []
    for actor in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors():
        if name == "MannequinLightingTest" and isinstance(actor, unreal.PointLight) and actor.get_actor_label().endswith("_EmergencyFill"):
            # The old steady fill was a second source of residual light during every outage.
            # Keep corridor reference lights intact; only disable the artificial showroom fill.
            actor.light_component.set_intensity(0.0)
        if not isinstance(actor, unreal.MannequinReactiveLight):
            continue
        before = actor.get_actor_transform()
        housing_before = actor.get_editor_property("fixture_mesh").get_world_scale()
        configure(actor, floor_z=0.0)
        light = actor.get_editor_property("reactive_spot_light")
        # OnConstruction/BeginPlay will use light_on_intensity, but keep the editor preview in sync too.
        light.set_intensity(actor.get_editor_property("light_on_intensity"))
        if not actor.get_actor_transform().equals(before):
            raise RuntimeError("Fixture transform changed: " + actor.get_actor_label())
        housing_after = actor.get_editor_property("fixture_mesh").get_world_scale()
        if any(abs(getattr(housing_before, axis)-getattr(housing_after, axis))>0.01 for axis in ("x", "y", "z")):
            raise RuntimeError("Fixture dimensions changed: " + actor.get_actor_label())
        found.append({"name": actor.get_actor_label(), "intensity": actor.get_editor_property("light_on_intensity"),
                      "radius": light.get_editor_property("attenuation_radius")})
    if len(found) != expected:
        raise RuntimeError("Unexpected fixture count in " + name + ": " + str(len(found)))
    if not unreal.EditorLoadingAndSavingUtils.save_map(world, path):
        raise RuntimeError("Cannot save " + path)
    result.append({"map": path, "fixtures": found, "emissive": 7, "electrical_volume": 0.45})

with open(os.path.join(qa, "StrongerProfileApplied.json"), "w", encoding="utf-8") as output:
    json.dump(result, output, ensure_ascii=False, indent=2)
unreal.log("[StrongerLighting] Applied surface-calibrated profile; transforms, thresholds and event frequency unchanged")
