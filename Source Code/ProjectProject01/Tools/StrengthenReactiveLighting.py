"""Apply the stronger requested lighting presentation to the fixture Blueprint and existing/test maps."""
import json
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


def configure(actor):
    actor.set_editor_property("light_on_intensity", 6500.0)
    actor.set_editor_property("emissive_on_value", 7.0)
    actor.set_editor_property("electrical_sound_volume", 0.45)


bp_path = "/Game/MyProject/MannequinReactiveLight"
backup("MyProject/MannequinReactiveLight.uasset", "MannequinReactiveLight_BeforeStrongerProfile.uasset")
bp = unreal.load_asset(bp_path)
configure(unreal.get_default_object(unreal.EditorAssetLibrary.load_blueprint_class(bp_path)))
unreal.BlueprintEditorLibrary.compile_blueprint(bp)
if not unreal.EditorAssetLibrary.save_loaded_asset(bp, only_if_is_dirty=False):
    raise RuntimeError("Cannot save fixture defaults")

result = []
for name, expected in [("LevelTest01", 9), ("MannequinLightingTest", 12)]:
    path = "/Game/MyProject/Level/" + name
    backup("MyProject/Level/" + name + ".umap", name + "_BeforeStrongerProfile.umap")
    world = unreal.EditorLoadingAndSavingUtils.load_map(path)
    if not world:
        raise RuntimeError("Cannot load " + path)
    found = []
    for actor in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors():
        if not isinstance(actor, unreal.MannequinReactiveLight):
            continue
        before = actor.get_actor_transform()
        housing_before = actor.get_editor_property("fixture_mesh").get_world_scale()
        configure(actor)
        if not actor.get_actor_transform().equals(before):
            raise RuntimeError("Fixture transform changed: " + actor.get_actor_label())
        housing_after = actor.get_editor_property("fixture_mesh").get_world_scale()
        if any(abs(getattr(housing_before, axis)-getattr(housing_after, axis))>0.01 for axis in ("x", "y", "z")):
            raise RuntimeError("Fixture dimensions changed: " + actor.get_actor_label())
        found.append(actor.get_actor_label())
    if len(found) != expected:
        raise RuntimeError("Unexpected fixture count in " + name + ": " + str(len(found)))
    if not unreal.EditorLoadingAndSavingUtils.save_map(world, path):
        raise RuntimeError("Cannot save " + path)
    result.append({"map": path, "fixtures": len(found), "intensity": 6500, "emissive": 7, "electrical_volume": 0.45})

with open(os.path.join(qa, "StrongerProfileApplied.json"), "w", encoding="utf-8") as output:
    json.dump(result, output, ensure_ascii=False, indent=2)
unreal.log("[StrongerLighting] Updated defaults and 21 fixtures; transforms, thresholds and event frequency unchanged")
