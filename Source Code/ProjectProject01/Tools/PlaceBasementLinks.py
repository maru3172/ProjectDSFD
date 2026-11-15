"""Two-way stairs between the match 1F (MultiplayTest2) and the temporary rescue basement (MultiplayerRescueTest).

Run with the editor closed:
  UnrealEditor-Cmd ProjectDFSD.uproject -run=pythonscript -script="<repo>/ProjectProject01/Tools/PlaceBasementLinks.py"
Saves both maps (copies them to Saved/MapBackups/<time>-basement/ first). Everything it adds is tagged BasementLinkAuto and is
removed and placed again on the next run. Nothing else is deleted.

1F: a "B1" stairs trigger near the elevator (to the basement) and three arrival spots a few metres away from it (tag F1_FromB1),
so survivors coming back up do not land on the trigger again.
Basement map: arrival spots at the foot of its ramp (tag B1_From1F) and a trigger on the upper ramp back to 1F.
Arrival spots are plain PlayerStarts tagged with the arrival id (the elevator arrival lookup accepts them).
"""
import datetime
import math
import os
import shutil

import unreal

TAG = "BasementLinkAuto"
FIRST = "/Game/MyProject/Level/MultiplayTest2"
BASEMENT = "/Game/MyProject/Level/MultiplayerRescueTest"
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)

stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
backup = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir()) + "MapBackups/" + stamp + "-basement/"
os.makedirs(backup, exist_ok=True)
for path in (FIRST, BASEMENT):
    shutil.copy2(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_content_dir()) + path[len("/Game/"):] + ".umap", backup)
unreal.log("[BasementLink] maps backed up to " + backup)


def clear():
    for actor in actors.get_all_level_actors():
        if TAG in [str(t) for t in actor.tags]:
            actors.destroy_actor(actor)


def place(cls, label, xyz, yaw=0.0):
    actor = actors.spawn_actor_from_class(cls, unreal.Vector(*xyz), unreal.Rotator(0, 0, yaw))
    if not actor:
        raise RuntimeError("could not spawn " + label)
    actor.set_actor_label(label)
    actor.set_folder_path("BasementLink")
    actor.tags = [TAG]
    return actor


def way(label, xyz, extent, yaw, destination, arrival_id):
    trigger = place(unreal.ProjectProject01LevelTransitionTrigger, label, xyz, yaw)
    trigger.set_editor_property("destination_level", unreal.load_asset(destination))
    trigger.set_editor_property("destination_arrival_id", arrival_id)
    trigger.get_editor_property("transition_volume").set_box_extent(unreal.Vector(*extent))
    trigger.set_editor_property("bot_objective", False)  # a side trip: bots keep heading for the real exit
    return trigger


def arrival(label, xyz, yaw, arrival_id):
    start = place(unreal.PlayerStart, label, xyz, yaw)
    start.set_editor_property("player_start_tag", arrival_id)
    return start


def sign(label, text, xyz, yaw):
    s = place(unreal.TextRenderActor, label, xyz, yaw)
    s.text_render.set_text(text)
    s.text_render.set_world_size(55)
    s.text_render.set_horizontal_alignment(unreal.HorizTextAligment.EHTA_CENTER)
    s.text_render.set_text_render_color(unreal.Color(r=255, g=70, b=55, a=255))


def walkable(world, x, y, z):
    p = unreal.NavigationSystemV1.project_point_to_navigation(world, unreal.Vector(x, y, z), None, None, unreal.Vector(10, 10, 200))
    return p is not None and abs(p.x - x) < 40 and abs(p.y - y) < 40


def open_spot(world, x, y, z, radius):
    return walkable(world, x, y, z) and all(walkable(world, x + math.cos(a) * radius, y + math.sin(a) * radius, z) for a in [i * math.pi / 4 for i in range(8)])


# ---------------------------------------------------------------- 1F
unreal.EditorLoadingAndSavingUtils.load_map(FIRST)
world = editor.get_editor_world()
clear()
elevator = next(a for a in actors.get_all_level_actors() if a.get_class().get_name() == "ProjectProject01Elevator")
e = elevator.get_actor_location()
# The stairs down: an open spot 7-14 m from the elevator, with room (3 m) for the trigger and a landing beside it.
best = None
for r in (700, 900, 1100, 1300):
    for k in range(16):
        a = k * math.pi / 8
        x, y = e.x + math.cos(a) * r, e.y + math.sin(a) * r
        if not open_spot(world, x, y, 50, 300):
            continue
        # A landing 5 m further out, also open (arrivals stand there, off the trigger).
        lx, ly = x + math.cos(a) * 500, y + math.sin(a) * 500
        if open_spot(world, lx, ly, 50, 200):
            best = (x, y, a, lx, ly)
            break
    if best:
        break
if not best:
    raise RuntimeError("[BasementLink] no open spot near the 1F elevator for the basement stairs; nothing saved")
x, y, a, lx, ly = best
yaw = math.degrees(a)
way("Stairs_1F_to_B1", (x, y, 150), (140, 140, 150), yaw, BASEMENT, "B1_From1F")
sign("Sign_1F_B1", "B1  지하", (x, y, 330), yaw + 180)
for i, side in enumerate((-160, 0, 160)):
    px, py = lx + math.cos(a + math.pi / 2) * side, ly + math.sin(a + math.pi / 2) * side
    arrival("Arrival_1F_FromB1_%d" % i, (px, py, 100), yaw, "F1_FromB1")  # facing away from the stairs
unreal.EditorLoadingAndSavingUtils.save_map(world, FIRST)
unreal.log("[BasementLink] 1F stairs at (%d, %d), landing at (%d, %d)" % (x, y, lx, ly))

# ---------------------------------------------------------------- basement (rescue map)
unreal.EditorLoadingAndSavingUtils.load_map(BASEMENT)
world = editor.get_editor_world()
clear()
# The ramp runs from the arena's east opening (x ~2080, floor 0) down to the basement (x ~3250, floor -440).
way("Stairs_B1_to_1F", (2250, 550, 40), (110, 210, 160), 0.0, FIRST, "F1_FromB1")
sign("Sign_B1_1F", "1F  위층", (2250, 550, 300), 180.0)
for i, yy in enumerate((430, 550, 670)):
    arrival("Arrival_B1_From1F_%d" % i, (3420, yy, -340), 0.0, "B1_From1F")  # past the ramp foot, facing into the basement
unreal.EditorLoadingAndSavingUtils.save_map(world, BASEMENT)
unreal.log("[BasementLink] done")
