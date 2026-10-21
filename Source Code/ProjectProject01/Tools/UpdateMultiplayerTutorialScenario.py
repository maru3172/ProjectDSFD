"""Refresh only the role scenarios in the existing multiplayer tutorial map.

Geometry, spawn points, triggers and navigation are deliberately left intact.
Run with UnrealEditor-Cmd -run=pythonscript -script=<this file>.
"""
import unreal

path = "/Game/MyProject/Level/MultiplayerTutorial"
world = unreal.EditorLoadingAndSavingUtils.load_map(path)
if not world:
    raise RuntimeError("Unable to load the existing tutorial map")
plans = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.ProjectProject01TutorialPlan)
if len(plans) != 1:
    raise RuntimeError("Expected exactly one TutorialPlan; nothing was saved")
plan = plans[0]
defaults = unreal.get_default_object(unreal.ProjectProject01TutorialPlan)
changed = False
for name in ("survivor_steps", "mannequin_steps", "survivor_closing", "mannequin_closing"):
    value = defaults.get_editor_property(name)
    if plan.get_editor_property(name) != value:
        plan.set_editor_property(name, value)
        changed = True
for name in ("survivor_steps", "mannequin_steps"):
    steps = plan.get_editor_property(name)
    unreal.log("[TutorialScenario] {}: {} steps".format(name, len(steps)))
    for step in steps:
        unreal.log("[TutorialScenario] {} | {} | {}".format(
            step.get_editor_property("id"), step.get_editor_property("title"),
            step.get_editor_property("instructions")))
if changed and not unreal.EditorLoadingAndSavingUtils.save_map(world, path):
    raise RuntimeError("Unable to save the updated tutorial scenarios")
unreal.log("[TutorialScenario] " + ("Updated role scenarios only" if changed else "Already current; no save needed"))
