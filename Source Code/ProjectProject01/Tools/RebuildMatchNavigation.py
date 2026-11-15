"""Full navigation build of the match floors (3F/2F/1F) and the rescue basement, from the command line.

Run with the editor closed:
  UnrealEditor-Cmd ProjectDFSD.uproject -run=pythonscript -script="<repo>/ProjectProject01/Tools/RebuildMatchNavigation.py"
The maps use Dynamic Modifiers Only navigation (fire shutters cut it at runtime). That mode needs a full build after every
change to the map, or the tiles around a shutter come back empty in play and bots stand still. The usual commandlet route
(ResavePackages -BuildNavigationData) is refused by the editor's load lock; this goes through
UProjectMapPlacementLibrary::RebuildNavigation, which lifts the lock first. Maps are copied to
Saved/MapBackups/<time>-nav/ before saving.
"""
import datetime
import os
import shutil

import unreal

MAPS = ["/Game/MyProject/Level/MultiplayTest", "/Game/MyProject/Level/MultiplayTest1", "/Game/MyProject/Level/MultiplayTest2",
        "/Game/MyProject/Level/MultiplayerRescueTest"]
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
content = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_content_dir())
backup = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir()) + "MapBackups/" + datetime.datetime.now().strftime("%Y%m%d-%H%M%S") + "-nav/"
os.makedirs(backup, exist_ok=True)
for path in MAPS:
    shutil.copy2(content + path[len("/Game/"):] + ".umap", backup)

for path in MAPS:
    unreal.EditorLoadingAndSavingUtils.load_map(path)
    world = editor.get_editor_world()
    tiles, ok, checked = (int(v) for v in unreal.ProjectMapPlacementLibrary.rebuild_navigation(world).split(","))
    if tiles == 0:
        unreal.log_error("[NavRebuild] %s: no navmesh tiles after the build; not saved" % path)
        continue
    unreal.EditorLoadingAndSavingUtils.save_map(world, path)
    unreal.log("[NavRebuild] %s: %d tiles, start->exit paths complete %d/%d" % (path, tiles, ok, checked))
unreal.log("[NavRebuild] done, backups in " + backup)
