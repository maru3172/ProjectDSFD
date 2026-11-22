"""Extra role spawn spots on the match floors (user, 2026-11-22): 20 mannequin + 10 survivor spots per floor.

Run with the editor closed:
  UnrealEditor-Cmd ProjectDFSD.uproject -run=pythonscript -script="<repo>/ProjectProject01/Tools/AddRoleSpawnSpots.py"
The hand-placed spots are kept; each is trap-checked (CheckRoleSpawnSpots) and a failing one is moved to the nearest passing
spot within 5 m. Then the missing spots are added, chosen by
UProjectMapPlacementLibrary::PickRoleSpawnSpots (reachable navmesh, no sealed rooms, no outskirts, clear of exits, shutters
and other starts, farthest-apart first). Everything added is tagged RoleSpawnAuto and is removed and placed again on the
next run; nothing else is deleted. Maps are copied to Saved/MapBackups/<time>-spawns/ before saving.
"""
import datetime
import os
import shutil

import unreal

TAG = "RoleSpawnAuto"
MAPS = ["/Game/MyProject/Level/MultiplayTest", "/Game/MyProject/Level/MultiplayTest1", "/Game/MyProject/Level/MultiplayTest2"]
MANNEQUINS, SURVIVORS = 20, 10
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
content = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_content_dir())
backup = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir()) + "MapBackups/" + datetime.datetime.now().strftime("%Y%m%d-%H%M%S") + "-spawns/"
os.makedirs(backup, exist_ok=True)
for path in MAPS:
    shutil.copy2(content + path[len("/Game/"):] + ".umap", backup)

for seed, path in enumerate(MAPS, start=1):
    unreal.EditorLoadingAndSavingUtils.load_map(path)
    world = editor.get_editor_world()
    for actor in actors.get_all_level_actors():
        if TAG in [str(t) for t in actor.tags]:
            actors.destroy_actor(actor)
    tiles = int(unreal.ProjectMapPlacementLibrary.rebuild_navigation(world).split(",")[0])  # live navmesh for the queries
    if tiles == 0:
        unreal.log_error("[RoleSpawn] %s: no navmesh tiles; not saved" % path)
        continue
    # The hand-placed spots get the same trap check; a failing one moves to the nearest passing spot within 5 m
    # (it is never deleted), and one with none is reported and left as it is.
    by_name = {a.get_name(): a for a in actors.get_all_level_actors() if isinstance(a, unreal.ProjectProject01RoleSpawnPoint)}
    for entry in filter(None, unreal.ProjectMapPlacementLibrary.check_role_spawn_spots(world).split(";")):
        parts = entry.split()
        if parts[1] == "move" and parts[0] in by_name:
            old = by_name[parts[0]].get_actor_location()
            by_name[parts[0]].set_actor_location(unreal.Vector(float(parts[2]), float(parts[3]), float(parts[4])), False, False)
            unreal.log_warning("[RoleSpawn] %s: moved %s from %s" % (path, parts[0], old))
        elif parts[1] == "fail":
            unreal.log_error("[RoleSpawn] %s: %s fails the trap check (%s), left as it is" % (path, parts[0], parts[2]))
    picked = unreal.ProjectMapPlacementLibrary.pick_role_spawn_spots(world, MANNEQUINS, SURVIVORS, seed)
    if picked.startswith("error:"):
        unreal.log_error("[RoleSpawn] %s: %s; not saved" % (path, picked))
        continue
    counts = {"M": 0, "S": 0}
    for entry in filter(None, picked.split(";")):
        role, x, y, z, yaw = entry.split()
        counts[role] += 1
        point = actors.spawn_actor_from_class(unreal.ProjectProject01RoleSpawnPoint, unreal.Vector(float(x), float(y), float(z)), unreal.Rotator(0, 0, float(yaw)))
        if not point:
            unreal.log_error("[RoleSpawn] %s: could not spawn %s" % (path, entry))
            continue
        name = "Mannequin" if role == "M" else "Survivor"
        point.set_editor_property("spawn_role", unreal.ProjectProject01SpawnRole.MANNEQUIN if role == "M" else unreal.ProjectProject01SpawnRole.SURVIVOR)
        point.set_actor_label("%sSpawnAuto_%02d" % (name, counts[role]))
        point.set_folder_path("RoleSpawnAuto")
        point.tags = [TAG]
    unreal.EditorLoadingAndSavingUtils.save_map(world, path)
    unreal.log("[RoleSpawn] %s: +%d mannequin, +%d survivor spots" % (path, counts["M"], counts["S"]))
unreal.log("[RoleSpawn] done, backups in " + backup)
