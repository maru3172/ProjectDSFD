"""Give every light fixture the single intro's recessed round downlight (trim ring + glowing lens, wide soft cone).

Run with UnrealEditor-Cmd -run=pythonscript -script=".../Tools/ApplyIntroDownlights.py".
- Mannequin reactive lights (C++ class and the /Game/MyProject/MannequinReactiveLight Blueprint) keep their fault
  behaviour; only the fixture meshes and the spot light's cone/softness change.
- Plain point lights named in FIXTURE_LIGHTS become a downlight (spot light pointing down) with the same intensity.
Sizes follow the ceiling height: the intro fixture is 24 cm for a 3.7 m ceiling.
"""
import unreal

TRIM = unreal.load_asset("/Game/MyProject/Testing/SingleIntro/SM_IntroDownlightTrim")
LENS = unreal.load_asset("/Game/MyProject/Testing/SingleIntro/MI_IntroDownlightLens")
WALL = unreal.load_asset("/Game/MyProject/Testing/ReactiveLighting/M_TestWall")
CYLINDER = unreal.load_asset("/Engine/BasicShapes/Cylinder")
if not (TRIM and LENS and WALL and CYLINDER):
    raise RuntimeError("Run Tools/CreateSinglePlayerIntro.py first: the intro downlight assets are missing")

# map -> (fixture size, {label prefix of point lights to turn into downlights: ceiling height or None = at the light})
MAPS = {
    "/Game/MyProject/Level/MannequinLightingTest": (1.0, {"_CorridorLight": 440.0}),
    "/Game/MyProject/Level/MultiplayerRescueTest": (1.0, {"Arena light": None, "Basement ": None}),
}
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
report = []


def look(spot, size):
    spot.set_editor_property("inner_cone_angle", 55.0)
    spot.set_editor_property("outer_cone_angle", 81.0)
    spot.set_editor_property("source_radius", 5.0 * size)
    spot.set_editor_property("soft_source_radius", 14.0 * size)
    spot.set_editor_property("specular_scale", .28)


def place(component, location, scale):
    component.set_world_location(location, False, False)
    component.set_world_rotation(unreal.Rotator(), False, False)
    component.set_world_scale3d(scale)


def reactive(actor, size):
    """Same parts as the intro downlight, placed at the actor (the ceiling)."""
    at = actor.get_actor_location()
    housing = actor.get_editor_property("fixture_mesh")
    housing.set_static_mesh(TRIM); housing.set_material(0, WALL); housing.set_editor_property("cast_shadow", False)
    place(housing, at, unreal.Vector(size, size, size))
    lens = actor.get_editor_property("bulb_mesh")
    lens.set_static_mesh(CYLINDER); lens.set_material(0, LENS); lens.set_editor_property("cast_shadow", False)
    place(lens, at + unreal.Vector(0, 0, -.62 * size), unreal.Vector(.151 * size, .151 * size, .0028 * size))
    spot = actor.get_editor_property("reactive_spot_light")
    spot.set_world_location(at + unreal.Vector(0, 0, -3.25 * size), False, False)
    spot.set_world_rotation(unreal.Rotator(pitch=-90), False, False)
    look(spot, size)


def downlight(point, ceiling, size):
    """Replace a bare point light with the intro downlight at the same place and brightness."""
    label = point.get_actor_label()
    source = point.light_component
    units, intensity, radius = source.intensity_units, source.intensity, source.attenuation_radius
    loc = point.get_actor_location()
    z = ceiling if ceiling is not None else loc.z
    actors.destroy_actor(point)
    spot = actors.spawn_actor_from_class(unreal.SpotLight, unreal.Vector(loc.x, loc.y, z - 3.25 * size), unreal.Rotator(pitch=-90))
    spot.set_actor_label(label)
    c = spot.light_component
    c.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
    c.set_editor_property("intensity_units", units)
    c.set_editor_property("intensity", intensity)
    c.set_editor_property("attenuation_radius", radius)
    look(c, size)
    for suffix, mesh, material, offset, scale in (
        ("trim", TRIM, WALL, 0, (size, size, size)),
        ("lit lens", CYLINDER, LENS, -.62 * size, (.151 * size, .151 * size, .0028 * size)),
    ):
        part = actors.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(loc.x, loc.y, z + offset), unreal.Rotator())
        part.set_actor_label(label + " " + suffix)
        part.set_actor_scale3d(unreal.Vector(*scale))
        piece = part.static_mesh_component
        piece.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
        piece.set_static_mesh(mesh); piece.set_material(0, material)
        piece.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
        piece.set_editor_property("cast_shadow", False)
        part.attach_to_actor(spot, "", unreal.AttachmentRule.KEEP_WORLD, unreal.AttachmentRule.KEEP_WORLD,
                             unreal.AttachmentRule.KEEP_WORLD, False)


for path, (size, plain) in MAPS.items():
    world = unreal.EditorLoadingAndSavingUtils.load_map(path)
    lamps = changed = 0
    for actor in list(actors.get_all_level_actors()):
        if isinstance(actor, unreal.MannequinReactiveLight):
            reactive(actor, size); lamps += 1
        elif isinstance(actor, unreal.PointLight):
            for prefix, ceiling in plain.items():
                if prefix in actor.get_actor_label() and not actor.get_actor_label().endswith("ceiling bounce"):
                    downlight(actor, ceiling, size); changed += 1
                    break
    if not unreal.EditorLoadingAndSavingUtils.save_map(world, path):
        raise RuntimeError("Unable to save " + path)
    report.append("%s: %d reactive, %d downlights" % (path, lamps, changed))
unreal.log("[IntroDownlights] " + " | ".join(report))
