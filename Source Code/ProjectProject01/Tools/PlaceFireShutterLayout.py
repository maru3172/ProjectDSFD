"""Place the fire shutter layout actors on the three match maps (Docs/FireShutterLayout.md).

Run with the editor closed:
  UnrealEditor-Cmd ProjectDFSD.uproject -run=pythonscript -script="<repo>/ProjectProject01/Tools/PlaceFireShutterLayout.py"
    -> dry run: writes previews to Saved/ShutterLayoutQA/<Map>_Placement.bmp and a report, saves no map.
  Set the environment variable SHUTTER_LAYOUT_APPLY=1 to also save the maps.
    -> the three .umap files are copied to Saved/MapBackups/<time>/ first.

What it does per floor (3F MultiplayTest, 2F MultiplayTest1, 1F MultiplayTest2):
  1. Reads the built navmesh on a 50 cm grid (walkable or not).
  2. Holes: closed non-walkable areas of escalator size. A hole holding an elevator (on any floor, same spot) is not an escalator.
  3. At the boarding end of each escalator hole (one end of its long axis; away from the elevator where there is one):
     a short U of BP_FireShutter (role Escalator, one id per hole) 2 m out from the edge, closed at both ends, and
     floor-change triggers on the floor strip inside it. The rest of the hole's edge gets no shutters. The floor's existing trigger is
     moved to the first of them (kept, not deleted) so there is no way down that skips the shutters.
  4. Route nodes on a 6 m lattice over walkable floor, linked along straight walkable lines (largest connected part only).
  5. Shutter rows across passages (at most 16 m wide), classified by the floor shape around them: corridor (stays
     narrow), chokepoint (narrow between wider areas), store front (a chokepoint into a small dead end). Roles Corridor,
     Chokepoint, Store. The runtime picks which corridors and chokepoints close each match; store fronts are normal shutters.
  6. One ProjectShutterLayout actor, 1F exit node, navmesh set to Dynamic Modifiers Only (rebuild paths afterwards).
Everything it adds is tagged ShutterLayoutAuto and is removed and rebuilt on the next run. Nothing else is deleted.
"""
import datetime
import json
import math
import os
import shutil
import struct

import unreal

APPLY = os.environ.get("SHUTTER_LAYOUT_APPLY") == "1"
MAPS = [("3F", "/Game/MyProject/Level/MultiplayTest", "/Game/MyProject/Level/MultiplayTest1"),
        ("2F", "/Game/MyProject/Level/MultiplayTest1", "/Game/MyProject/Level/MultiplayTest2"),
        ("1F", "/Game/MyProject/Level/MultiplayTest2", None)]
TAG = "ShutterLayoutAuto"
CELL = 50.0
LATTICE = 400.0
RING_OFFSET = 200.0          # shutter line, outward from the hole's walkable edge
TRIGGER_OFFSET = 100.0       # trigger strip, between the hole edge and the shutter line
GATE_DEPTH = 700.0           # how far the boarding gate reaches back from the end of the opening
CEILING_HEIGHT = 600.0       # department store ceiling (the frame meshes have none); fixtures sit flush under it
SHUTTER_SCALE_Z = (CEILING_HEIGHT + 10.0) / 400.0  # layout shutters reach the ceiling (BP_FireShutter is 4 m, pivot at the floor)
SHUTTER_OPEN_HEIGHT = CEILING_HEIGHT + 20.0        # fully raised = tucked above the ceiling
SHUTTER_SPEED = 260.0                              # about 2.4 s to open
LIGHT_SPACING_ROUTE = 2200.0 # pools of light along the way, darkness between
LIGHT_SPACING_SIDE = 3400.0
# Per floor (user, 2026-11-15): 2F and 1F want a few more lights, and no pitch-black pockets anywhere.
FLOOR_LIGHT_SPACING = {"2F": (1700.0, 2600.0), "1F": (1700.0, 2600.0)}   # (route, side)
FLOOR_DARK_GAP = {"3F": 2400.0, "2F": 1600.0, "1F": 1600.0}              # fill any spot farther than this from a lit fixture
LIGHT_CAP = 70
DEAD_FIXTURE_CHANCE = 0.0    # side-area fixtures that are simply dead (always); the way, starts and exits are never dead
LIGHT_SHUTTER_CLEARANCE = 200.0  # fixtures keep this far from any shutter line
LIGHT_WALL_CLEARANCE = 150.0     # and have open floor this far around them (about 1.9 m from a wall)
SKY_INTENSITY = 0.16         # dark, but shapes still read between the pools of light
FLOOR_EXPOSURE = {"1F": -1.5}  # 1F placeholder material is bright green; retune all three when real textures arrive
EXPOSURE_BIAS = 0.5          # fixed exposure; checked with Saved/ShutterLayoutQA/Look_*.png
SHUTTER_WIDTH = 300.0        # BP_FireShutter spans 300 cm along its local Y at scale 1
MAX_CORRIDOR_WIDTH = 4500.0   # main mall corridors get a row of many shutters wall to wall (or to the opening's edge)
MAIN_CORRIDOR_WIDTH = 1600.0  # wider than this: a main corridor row
BARRIER_SPACING = 1200.0
MAX_BARRIERS = 30
AGENT_MARGIN = 40.0          # navmesh is shrunk by the agent radius; shutters reach the real wall
EXIT_BOX_CLEARANCE = 100.0   # the exit row stays this far in front of every corner of the escape volume
MAX_LINES = 6                # compartment line candidates per floor (2-3 stand in a match)
LINE_MIN_GAP = 1500.0        # candidate lines at least 15 m apart
LINE_EVEN_SPACING = 2500.0   # evenly spread candidates about every 25 m
LINE_EDGE_MARGIN = 1200.0    # not right at the building's ends
OUT = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir()) + "ShutterLayoutQA/"
os.makedirs(OUT, exist_ok=True)

actors_sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
shutter_class = unreal.EditorAssetLibrary.load_blueprint_class("/Game/MyProject/BP_FireShutter")
report = {"apply": APPLY, "floors": {}}


# ---------------------------------------------------------------- scanning
def walkable_at(world, x, y, z_hint):
    p = unreal.NavigationSystemV1.project_point_to_navigation(world, unreal.Vector(x, y, z_hint), None, None, unreal.Vector(10, 10, 200))
    if p is None or abs(p.x - x) > 40 or abs(p.y - y) > 40:
        return None
    return p.z


class Grid:
    def __init__(self, world, x0, y0, w, h, z_hint):
        self.x0, self.y0, self.w, self.h = x0, y0, w, h
        self.walk = bytearray(w * h)
        self.z = 0.0
        zs = []
        for j in range(h):
            for i in range(w):
                z = walkable_at(world, x0 + (i + .5) * CELL, y0 + (j + .5) * CELL, z_hint)
                if z is not None:
                    self.walk[j * w + i] = 1
                    if len(zs) < 2000:
                        zs.append(z)
        if zs:
            zs.sort()
            self.z = zs[len(zs) // 2]

    def cell(self, x, y):
        return int((x - self.x0) / CELL), int((y - self.y0) / CELL)

    def ok(self, x, y):
        i, j = self.cell(x, y)
        return 0 <= i < self.w and 0 <= j < self.h and self.walk[j * self.w + i] == 1

    def line_ok(self, a, b, step=25.0):
        n = max(1, int(math.dist(a, b) / step))
        return all(self.ok(a[0] + (b[0] - a[0]) * k / n, a[1] + (b[1] - a[1]) * k / n) for k in range(n + 1))


ERODE = 2  # cells: walls thinner than about 2 m vanish, so a hole touching a wall line is still its own area


def core_mask(grid):
    """Non-walkable cells with no walkable cell within ERODE cells (thin walls drop out, holes stay)."""
    core = bytearray(grid.w * grid.h)
    for j in range(ERODE, grid.h - ERODE):
        for i in range(ERODE, grid.w - ERODE):
            if all(not grid.walk[(j + dj) * grid.w + i + di] for dj in range(-ERODE, ERODE + 1) for di in range(-ERODE, ERODE + 1)):
                core[j * grid.w + i] = 1
    return core


def components(grid, mask):
    """Connected areas of mask cells: (cells, touches_border)."""
    seen = bytearray(grid.w * grid.h)
    out = []
    for start in range(grid.w * grid.h):
        if not mask[start] or seen[start]:
            continue
        stack, cells, border = [start], [], False
        seen[start] = 1
        while stack:
            c = stack.pop()
            cells.append(c)
            i, j = c % grid.w, c // grid.w
            if i <= ERODE or j <= ERODE or i >= grid.w - 1 - ERODE or j >= grid.h - 1 - ERODE:
                border = True
            for di, dj in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                ni, nj = i + di, j + dj
                if 0 <= ni < grid.w and 0 <= nj < grid.h:
                    n = nj * grid.w + ni
                    if mask[n] and not seen[n]:
                        seen[n] = 1
                        stack.append(n)
        out.append((cells, border))
    return out


def hull(points):
    pts = sorted(set(points))
    if len(pts) < 3:
        return pts
    def cross(o, a, b):
        return (a[0] - o[0]) * (b[1] - o[1]) - (a[1] - o[1]) * (b[0] - o[0])
    lower, upper = [], []
    for p in pts:
        while len(lower) >= 2 and cross(lower[-2], lower[-1], p) <= 0:
            lower.pop()
        lower.append(p)
    for p in reversed(pts):
        while len(upper) >= 2 and cross(upper[-2], upper[-1], p) <= 0:
            upper.pop()
        upper.append(p)
    return lower[:-1] + upper[:-1]


def simplify(poly, min_side=250.0):
    """Merge short hull sides so each ring side is a real wall line."""
    changed = True
    while changed and len(poly) > 4:
        changed = False
        for k in range(len(poly)):
            a, b = poly[k], poly[(k + 1) % len(poly)]
            if math.dist(a, b) < min_side:
                poly = poly[:k] + poly[k + 1:]
                changed = True
                break
    return poly


def offset(poly, d):
    """Convex counter-clockwise polygon moved outward by d."""
    lines = []
    n = len(poly)
    for k in range(n):
        a, b = poly[k], poly[(k + 1) % n]
        dx, dy = b[0] - a[0], b[1] - a[1]
        l = math.hypot(dx, dy) or 1
        nx, ny = dy / l, -dx / l  # outward for counter-clockwise order
        lines.append(((a[0] + nx * d, a[1] + ny * d), (dx / l, dy / l)))
    out = []
    for k in range(n):
        (p1, d1), (p2, d2) = lines[k - 1], lines[k]
        den = d1[0] * d2[1] - d1[1] * d2[0]
        if abs(den) < 1e-6:
            out.append(p2)
            continue
        t = ((p2[0] - p1[0]) * d2[1] - (p2[1] - p1[1]) * d2[0]) / den
        out.append((p1[0] + d1[0] * t, p1[1] + d1[1] * t))
    return out


def inside(poly, p):
    c = False
    n = len(poly)
    for k in range(n):
        a, b = poly[k], poly[(k + 1) % n]
        if (a[1] > p[1]) != (b[1] > p[1]) and p[0] < (b[0] - a[0]) * (p[1] - a[1]) / (b[1] - a[1]) + a[0]:
            c = not c
    return c


def segments_cross(a, b, c, d):
    def o(p, q, r):
        return (q[0] - p[0]) * (r[1] - p[1]) - (q[1] - p[1]) * (r[0] - p[0])
    return (o(a, b, c) > 0) != (o(a, b, d) > 0) and (o(c, d, a) > 0) != (o(c, d, b) > 0)


def ccw(poly):
    area = sum(poly[k][0] * poly[(k + 1) % len(poly)][1] - poly[(k + 1) % len(poly)][0] * poly[k][1] for k in range(len(poly)))
    return poly if area > 0 else list(reversed(poly))


def dist_to_poly(p, poly):
    best = 1e18
    for k in range(len(poly)):
        a, b = poly[k], poly[(k + 1) % len(poly)]
        ab = (b[0] - a[0], b[1] - a[1])
        l2 = ab[0] ** 2 + ab[1] ** 2 or 1
        t = max(0, min(1, ((p[0] - a[0]) * ab[0] + (p[1] - a[1]) * ab[1]) / l2))
        best = min(best, math.dist(p, (a[0] + ab[0] * t, a[1] + ab[1] * t)))
    return best


# ---------------------------------------------------------------- pass 1: scan every floor, find holes
floors = {}
for floor, path, dest in MAPS:
    unreal.EditorLoadingAndSavingUtils.load_map(path)
    world = editor.get_editor_world()
    frame, elevators, spawns, mannequins, triggers, exits = None, [], [], [], [], []
    exit_boxes = {}  # exit centre -> its four corners on the ground (the whole escape volume must sit behind the exit row)
    for actor in actors_sub.get_all_level_actors():
        cls = actor.get_class().get_name()
        loc = actor.get_actor_location()
        if cls == "StaticMeshActor" and actor.static_mesh_component.static_mesh and TAG not in [str(t) for t in actor.tags]:
            # The floor's own mesh: the biggest untagged static mesh (never one of our ceiling slabs).
            if frame is None or actor.get_actor_bounds(False)[1].x > frame.get_actor_bounds(False)[1].x:
                frame = actor
        elif cls == "ProjectProject01Elevator":
            elevators.append((loc.x, loc.y))
        elif cls == "ProjectProject01RoleSpawnPoint":
            role = str(actor.get_editor_property("spawn_role")).upper()
            (spawns if "SURVIVOR" in role else mannequins).append((loc.x, loc.y))
        elif cls == "TriggerBox":
            exits.append((loc.x, loc.y))
            _o, _e = actor.get_actor_bounds(False)
            exit_boxes[(loc.x, loc.y)] = [(_o.x + sx * _e.x, _o.y + sy * _e.y) for sx in (-1, 1) for sy in (-1, 1)]
    origin, extent = frame.get_actor_bounds(False)
    x0, y0 = origin.x - extent.x, origin.y - extent.y
    grid = Grid(world, x0, y0, int(extent.x * 2 / CELL) + 1, int(extent.y * 2 / CELL) + 1, 50.0)
    holes = []
    comps = components(grid, core_mask(grid))
    for cells, border in comps:
        if border:
            continue  # outside the building
        area = len(cells) * CELL * CELL / 1e4
        xs = [c % grid.w for c in cells]
        ys = [c // grid.w for c in cells]
        bw, bh = (max(xs) - min(xs) + 1 + 2 * ERODE) * CELL, (max(ys) - min(ys) + 1 + 2 * ERODE) * CELL
        if area < 80 or min(bw, bh) < 800 or len(cells) / ((bw / CELL - 2 * ERODE) * (bh / CELL - 2 * ERODE)) < .3:
            continue
        pts = [(grid.x0 + (c % grid.w) * CELL + (dx * CELL), grid.y0 + (c // grid.w) * CELL + (dy * CELL)) for c in cells for dx in (0, 1) for dy in (0, 1)]
        # The eroded core is ERODE cells inside the real edge: grow it back.
        poly = ccw(simplify(offset(ccw(simplify(hull(pts), 100.0)), ERODE * CELL)))
        cx, cy = sum(p[0] for p in poly) / len(poly), sum(p[1] for p in poly) / len(poly)
        holes.append({"poly": poly, "center": (cx, cy), "area": area, "size": (bw, bh)})
    floors[floor] = {"path": path, "dest": dest, "grid": grid, "holes": holes, "elevators": elevators, "spawns": spawns,
                     "mannequins": mannequins, "exits": exits, "exit_boxes": exit_boxes}
    unreal.log("[ShutterLayout] %s scanned: %d holes, %d survivor spawns, %d mannequin spawns" % (floor, len(holes), len(spawns), len(mannequins)))

# Safety: an unreadable navmesh (it happened once, transiently) would make this script wipe the layout and place
# nothing. Stop before touching any map.
for name, fl in floors.items():
    walkable = sum(fl["grid"].walk)
    if walkable < 5000:
        raise RuntimeError("[ShutterLayout] %s: navmesh not readable (%d walkable cells); nothing was changed. Run again." % (name, walkable))

# A hole with an elevator at it: the elevator end stays as it is, the far end is an escalator (the user's rule).
# The same hole on another floor (same spot) is split the same way, toward the same elevator spot.
shafts = [(h["center"], e) for f in floors.values() for h in f["holes"] for e in f["elevators"] if inside(offset(h["poly"], 300), e)]
for f in floors.values():
    for h in f["holes"]:
        h["escalator"] = True
        h["elevator"] = next((e for c, e in shafts if math.dist(h["center"], c) < 800), None)


# ---------------------------------------------------------------- pass 2: place
def spawn(cls, label, loc, yaw=0.0, scale=None):
    actor = actors_sub.spawn_actor_from_class(cls, unreal.Vector(loc[0], loc[1], loc[2]), unreal.Rotator(0, 0, yaw))
    if not actor:
        raise RuntimeError("could not spawn " + label)
    actor.set_actor_label(label)
    actor.set_folder_path("ShutterLayout")
    actor.tags = [TAG]
    if scale:
        actor.set_actor_scale3d(unreal.Vector(*scale))
    return actor


def write_bmp(path, w, h, px):
    row = (w * 3 + 3) & ~3
    data = bytearray()
    for y in range(h - 1, -1, -1):
        line = bytearray()
        for x in range(w):
            r, g, b = px[y * w + x]
            line += bytes((b, g, r))
        data += line + b"\0" * (row - len(line))
    with open(path, "wb") as f:
        f.write(b"BM" + struct.pack("<IHHI", 54 + len(data), 0, 0, 54) + struct.pack("<IiiHHIIiiII", 40, w, h, 1, 24, 0, len(data), 2835, 2835, 0, 0) + data)


stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
if APPLY:
    backup = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir()) + "MapBackups/" + stamp + "/"
    os.makedirs(backup, exist_ok=True)
    for _, path, _ in MAPS:
        src = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_content_dir()) + path[len("/Game/"):] + ".umap"
        shutil.copy2(src, backup)
    report["backup"] = backup
    unreal.log("[ShutterLayout] maps backed up to " + backup)

for floor, path, dest in MAPS:
    f = floors[floor]
    grid = f["grid"]
    unreal.EditorLoadingAndSavingUtils.load_map(path)
    world = editor.get_editor_world()
    for actor in actors_sub.get_all_level_actors():
        if TAG in [str(t) for t in actor.tags]:
            actors_sub.destroy_actor(actor)
    old_triggers = [a for a in actors_sub.get_all_level_actors() if a.get_class().get_name() == "ProjectProject01LevelTransitionTrigger"]
    destination = old_triggers[0].get_editor_property("destination_level") if old_triggers else None
    z = grid.z
    floor_report = {"holes": [], "escalators": 0, "nodes": 0, "links": 0, "barriers": 0, "warnings": []}
    paint = []  # (kind, points)

    # Escalator rings.
    rings = []
    escalator_index = 0
    for h in sorted(f["holes"], key=lambda h: h["center"]):
        floor_report["holes"].append({"center": [round(v) for v in h["center"]], "area_m2": round(h["area"]), "with_elevator": h["elevator"] is not None})
        if not h["escalator"]:
            continue
        escalator_index += 1
        esc_id = "ES_%s_%d" % (floor, escalator_index)
        ring = offset(h["poly"], RING_OFFSET)
        strip = offset(h["poly"], TRIGGER_OFFSET)
        blocks = []  # shutter lines the route graph may not cross
        poly = h["poly"]
        # The escalator boards at one end of the opening's long axis: only that end gets a short U of shutters
        # (the rest of the edge stays as the map has it). With an elevator at the opening, the end away from it.
        cx, cy = h["center"]
        sxx = sum((q[0] - cx) ** 2 for q in poly); syy = sum((q[1] - cy) ** 2 for q in poly); sxy = sum((q[0] - cx) * (q[1] - cy) for q in poly)
        angle = .5 * math.atan2(2 * sxy, sxx - syy)
        axis = (math.cos(angle), math.sin(angle))
        proj = [(q[0] - cx) * axis[0] + (q[1] - cy) * axis[1] for q in poly]
        if h["elevator"] is not None:
            sign = 1 if (cx - h["elevator"][0]) * axis[0] + (cy - h["elevator"][1]) * axis[1] > 0 else -1
        else:
            def openness(sg):
                tip = max(proj) if sg > 0 else min(proj)
                px, py = cx + axis[0] * (tip + sg * 500), cy + axis[1] * (tip + sg * 500)
                return sum(1 for dx in range(-600, 601, 100) for dy in range(-600, 601, 100) if dx * dx + dy * dy <= 360000 and grid.ok(px + dx, py + dy))
            sign = 1 if openness(1) >= openness(-1) else -1
        reach_tip = max(sign * t for t in proj)
        def in_gate(q):
            return sign * ((q[0] - cx) * axis[0] + (q[1] - cy) * axis[1]) >= reach_tip - GATE_DEPTH
        rings.append({"ring": ring, "hole": poly, "in_gate": in_gate, "blocks": blocks})
        placed_triggers = 0
        def escalator_side(k):
            m = ((poly[k][0] + poly[(k + 1) % len(poly)][0]) / 2, (poly[k][1] + poly[(k + 1) % len(poly)][1]) / 2)
            return in_gate(m)
        sides = [escalator_side(k) for k in range(len(ring))]
        # Where the gate ends, a short shutter closes the floor strip between the edge and the gate line.
        for k in range(len(ring)):
            if sides[k] != sides[k - 1]:
                a, b = poly[k], ring[k]
                length = math.dist(a, b)
                t = ((b[0] - a[0]) / length, (b[1] - a[1]) / length)
                c = ((a[0] + b[0]) / 2, (a[1] + b[1]) / 2)
                blocks.append((a, b))
                cap = spawn(shutter_class, "%s_Cap_%d" % (esc_id, k), (c[0], c[1], z + 10), math.degrees(math.atan2(-t[0], t[1])), (1, (length + 60) / SHUTTER_WIDTH, SHUTTER_SCALE_Z))
                cap.set_editor_property("open_height", SHUTTER_OPEN_HEIGHT)
                cap.set_editor_property("move_speed", SHUTTER_SPEED)
                cap.set_editor_property("layout_role", unreal.ProjectShutterLayoutRole.ESCALATOR)
                cap.set_editor_property("escalator_id", esc_id)
                paint.append(("ring", [c]))
        for k in range(len(ring)):
            if not sides[k]:
                continue
            a, b = ring[k], ring[(k + 1) % len(ring)]
            blocks.append((a, b))
            length = math.dist(a, b)
            t = ((b[0] - a[0]) / length, (b[1] - a[1]) / length)
            n = max(1, math.ceil(length / SHUTTER_WIDTH))
            yaw = math.degrees(math.atan2(-t[0], t[1]))
            for s in range(n):
                c = (a[0] + t[0] * length * (s + .5) / n, a[1] + t[1] * length * (s + .5) / n)
                if not grid.ok(*c):
                    continue  # a wall or another obstacle closes this part already
                shutter = spawn(shutter_class, "%s_Shutter_%d_%d" % (esc_id, k, s), (c[0], c[1], z + 10), yaw, (1, length / n / SHUTTER_WIDTH, SHUTTER_SCALE_Z))
                shutter.set_editor_property("open_height", SHUTTER_OPEN_HEIGHT)
                shutter.set_editor_property("move_speed", SHUTTER_SPEED)
                shutter.set_editor_property("layout_role", unreal.ProjectShutterLayoutRole.ESCALATOR)
                shutter.set_editor_property("escalator_id", esc_id)
                paint.append(("ring", [c]))
            # The floor strip inside this side: a way down, reachable only through this ring.
            sa, sb = strip[k], strip[(k + 1) % len(strip)]
            mid = ((sa[0] + sb[0]) / 2, (sa[1] + sb[1]) / 2)
            if destination is None or not grid.ok(*mid) or math.dist(sa, sb) < 200:
                continue
            if old_triggers:
                trigger = old_triggers.pop(0)  # the floor's original way down, moved inside the ring (not deleted)
                trigger.set_actor_location(unreal.Vector(mid[0], mid[1], z + 150), False, False)
                trigger.set_actor_rotation(unreal.Rotator(0, 0, math.degrees(math.atan2(t[1], t[0]))), False)
                floor_report["warnings"].append("moved existing trigger %s into %s" % (trigger.get_actor_label(), esc_id))
            else:
                trigger = spawn(unreal.ProjectProject01LevelTransitionTrigger, "%s_Way_%d" % (esc_id, k), (mid[0], mid[1], z + 150), math.degrees(math.atan2(t[1], t[0])))
                trigger.set_editor_property("destination_level", destination)
            trigger.get_editor_property("transition_volume").set_box_extent(unreal.Vector(max(80, math.dist(sa, sb) / 2 - 60), 70, 150))
            placed_triggers += 1
            paint.append(("trigger", [mid]))
        if placed_triggers == 0:
            floor_report["warnings"].append("%s: no floor strip for a trigger inside the ring" % esc_id)
        floor_report["escalators"] += 1
    if old_triggers and floor != "1F":
        floor_report["warnings"].append("existing trigger(s) left where they were: " + ", ".join(t.get_actor_label() for t in old_triggers))

    def shut_in(p):
        """Inside a hole, or on the floor strip an escalator gate closes off."""
        for r in rings:
            if inside(r["hole"], p) or (inside(r["ring"], p) and r["in_gate"](p)):
                return True
        return False

    # Route lattice.
    gx0, gy0 = grid.x0 + LATTICE / 2, grid.y0 + LATTICE / 2
    cols, rows = int(grid.w * CELL / LATTICE), int(grid.h * CELL / LATTICE)
    node_at = {}
    for r in range(rows):
        for c in range(cols):
            p = (gx0 + c * LATTICE, gy0 + r * LATTICE)
            if all(grid.ok(p[0] + dx, p[1] + dy) for dx, dy in ((0, 0), (50, 0), (-50, 0), (0, 50), (0, -50))) and not shut_in(p):
                node_at[(c, r)] = p
    links = set()
    for (c, r), p in node_at.items():
        for dc, dr in ((1, 0), (0, 1)):
            q = node_at.get((c + dc, r + dr))
            if q and grid.line_ok(p, q) and not any(segments_cross(p, q, a, b) for r in rings for a, b in r["blocks"]):
                links.add(((c, r), (c + dc, r + dr)))
    def split_parts():
        adj = {k: [] for k in node_at}
        for a, b in links:
            adj[a].append(b)
            adj[b].append(a)
        parts, seen = [], set()
        for k in node_at:
            if k in seen:
                continue
            stack, part = [k], []
            seen.add(k)
            while stack:
                u = stack.pop()
                part.append(u)
                for v in adj[u]:
                    if v not in seen:
                        seen.add(v)
                        stack.append(v)
            parts.append(part)
        return sorted(parts, key=len, reverse=True)

    # Doorways and bent passages that miss the lattice lines split the floor into parts. Join parts the navmesh
    # really joins: the nearest node pairs whose walking path is not much longer than the straight line.
    portals = set()

    def nearest_node(x, y, reach=260.0):
        c0, r0 = round((x - gx0) / LATTICE), round((y - gy0) / LATTICE)
        best, best_d = None, reach
        for c in range(c0 - 1, c0 + 2):
            for r in range(r0 - 1, r0 + 2):
                q = node_at.get((c, r))
                if q and math.dist(q, (x, y)) < best_d:
                    best, best_d = (c, r), math.dist(q, (x, y))
        return best

    for _ in range(30):
        parts = [part for part in split_parts() if len(part) >= 12]
        if len(parts) < 2:
            break
        main = parts[0]
        progressed = False
        for part in parts[1:]:
            a = part[len(part) // 2]
            b = min(main, key=lambda k: math.dist(node_at[k], node_at[a]))
            pa, pb = node_at[a], node_at[b]
            nav_path = unreal.NavigationSystemV1.find_path_to_location_synchronously(world, unreal.Vector(pa[0], pa[1], z + 50), unreal.Vector(pb[0], pb[1], z + 50))
            if not nav_path or not nav_path.is_valid() or nav_path.is_partial():
                continue
            corners = [(v.x, v.y) for v in nav_path.get_editor_property("path_points")]
            samples = []
            for k in range(len(corners) - 1):
                n = max(1, int(math.dist(corners[k], corners[k + 1]) / 100))
                samples += [(corners[k][0] + (corners[k + 1][0] - corners[k][0]) * t / n, corners[k][1] + (corners[k + 1][1] - corners[k][1]) * t / n) for t in range(n)]
            prev = None
            for x, y in samples:
                k = nearest_node(x, y)
                if k is None:
                    continue
                if prev is not None and k != prev:
                    qa, qb = node_at[prev], node_at[k]
                    mid = ((qa[0] + qb[0]) / 2, (qa[1] + qb[1]) / 2)
                    if not shut_in(mid) and not any(segments_cross(qa, qb, sa, sb) for r in rings for sa, sb in r["blocks"]):
                        if (prev, k) not in links and (k, prev) not in links:
                            links.add((prev, k))
                            portals.add((prev, k))
                            progressed = True
                prev = k
        if not progressed:
            break
    parts = split_parts()
    keep = set(parts[0]) if parts else set()
    links = {(a, b) for a, b in links if a in keep and b in keep}
    if len(keep) < 200:
        raise RuntimeError("[ShutterLayout] %s: route graph too small (%d nodes); stopping before saving anything more. Restore from %s if a floor was already saved." % (floor, len(keep), report.get("backup", "the backups")))
    floor_report["portals"] = len(portals)
    floor_report["dropped_parts"] = [len(part) for part in parts[1:] if len(part) >= 12]

    # Shutter rows across passages. Each candidate is a lattice link; the floor shape around it says what it is:
    #   corridor   - stays narrow for a while (width at the link and 4 m before/after alike)
    #   chokepoint - a narrow passage between wider areas (narrower than what lies on at least one side)
    #   store      - a chokepoint that only leads into a small dead-end area with no spawn, escalator or exit
    avoid = f["spawns"] + f["elevators"] + f["exits"]
    protected_nodes = set()
    for s in f["spawns"] + f["exits"]:
        k = min(keep, key=lambda k: math.dist(node_at[k], s)) if keep else None
        if k:
            protected_nodes.add(k)
    for k in keep:
        if any(dist_to_poly(node_at[k], r["ring"]) < 600 for r in rings):
            protected_nodes.add(k)
    adjacency = {k: set() for k in keep}
    for a, b in links:
        adjacency[a].add(b)
        adjacency[b].add(a)

    def width_at(c, p):
        def reach(sign):
            for s in range(1, 101):
                if not grid.ok(c[0] + p[0] * sign * s * 25, c[1] + p[1] * sign * s * 25):
                    return s * 25
            return None
        if not grid.ok(*c):
            return None, None, None
        left, right = reach(-1), reach(1)
        if left is None or right is None:
            return left, right, 99999
        return left, right, left + right

    def cut_side_is_small(a, b, line):
        """Cutting the links the row stands across: is one side a small closed-off area without anything needed?"""
        cut = {(u, v) for u in keep for v in adjacency[u] if segments_cross(node_at[u], node_at[v], line[0], line[1])}
        for start in (a, b):
            seen, stack = {start}, [start]
            while stack and len(seen) <= 90:
                u = stack.pop()
                for v in adjacency[u]:
                    if v not in seen and (u, v) not in cut and (v, u) not in cut:
                        seen.add(v)
                        stack.append(v)
            if len(seen) <= 90 and not (seen & protected_nodes):
                return True
        return False

    candidates = {"main": [], "corridor": [], "chokepoint": [], "store": []}

    def near_gate(q, limit):
        return any(dist_to_poly(q, [sa, sb]) < limit for r in rings for sa, sb in r["blocks"])
    for a, b in sorted(links):
        if (a, b) in portals:
            continue
        pa, pb = node_at[a], node_at[b]
        m = ((pa[0] + pb[0]) / 2, (pa[1] + pb[1]) / 2)
        d = ((pb[0] - pa[0]) / LATTICE, (pb[1] - pa[1]) / LATTICE)
        p = (-d[1], d[0])
        left, right, w = width_at(m, p)
        if w is None or w > MAX_CORRIDOR_WIDTH:
            continue
        if near_gate(m, 600) or shut_in(m) or any(math.dist(m, s) < 800 for s in avoid):
            continue
        before = width_at((m[0] - d[0] * 400, m[1] - d[1] * 400), p)[2]
        after = width_at((m[0] + d[0] * 400, m[1] + d[1] * 400), p)[2]
        span = left + right + 2 * AGENT_MARGIN
        line = ((m[0] - p[0] * (left + AGENT_MARGIN), m[1] - p[1] * (left + AGENT_MARGIN)),
                (m[0] + p[0] * (right + AGENT_MARGIN), m[1] + p[1] * (right + AGENT_MARGIN)))
        wider = max(before or 0, after or 0)
        if w <= 450 and wider >= 1.6 * w:
            kind = "store" if cut_side_is_small(a, b, line) else "chokepoint"
        elif before is not None and after is not None and before <= 1.4 * w + 50 and after <= 1.4 * w + 50:
            kind = "main" if w > MAIN_CORRIDOR_WIDTH else "corridor"
        else:
            continue
        candidates[kind].append((m, d, p, left, right, span, a, b))

    # How much "the way" passes each node: shortest ways from every survivor start to every escalator gate / exit.
    from collections import deque
    def bfs(src):
        dist = {src: 0}
        q = deque([src])
        while q:
            u = q.popleft()
            for v in adjacency[u]:
                if v not in dist:
                    dist[v] = dist[u] + 1
                    q.append(v)
        return dist
    def nearest_kept(pt):
        return min(keep, key=lambda k: math.dist(node_at[k], pt)) if keep else None
    starts = {nearest_kept(sp) for sp in f["spawns"]} - {None}
    goals = set()
    for r in rings:
        for sa, sb in r["blocks"]:
            mid = ((sa[0] + sb[0]) / 2, (sa[1] + sb[1]) / 2)
            k = nearest_kept(mid)
            if k:
                goals.add(k)
    goals |= {nearest_kept(e) for e in f["exits"]} - {None}
    traffic = {k: 0 for k in keep}
    goal_dist = {g: bfs(g) for g in goals}
    for st in starts:
        ds = bfs(st)
        for g, dg in goal_dist.items():
            if g not in ds:
                continue
            for k in keep:
                if k in ds and k in dg and ds[k] + dg[k] == ds[g]:
                    traffic[k] += 1

    # Fire compartments (user, 2026-11-15): like a real department store, the floor is split into zones and every passage
    # on a zone boundary has a shutter row. Each match closes them; most can be opened, some are broken through (go around).
    # Boundaries run north-south between the atrium columns (3F/2F) or split the floor in three (1F), each nudged within
    # 10 m to where the passages are narrowest (more wall, fewer and smaller openings).
    xs_walk = [grid.x0 + (c % grid.w + .5) * CELL for c in range(grid.w * grid.h) if grid.walk[c]]
    building_min, building_max = min(xs_walk), max(xs_walk)
    columns = []
    for h in sorted(f["holes"], key=lambda h: h["center"][0]):
        lo, hi = min(v[0] for v in h["poly"]), max(v[0] for v in h["poly"])
        if columns and lo - columns[-1][1] < 300:
            columns[-1] = (min(columns[-1][0], lo), max(columns[-1][1], hi))
        else:
            columns.append((lo, hi))
    # Candidates (user, 2026-11-15): more boundary lines than stand in a match. Each match the runtime picks 2-3 of them
    # (MinActiveLines/MaxActiveLines), so where shutters appear changes too and cannot be learned. Atrium gaps first (the
    # natural zone edges), then evenly across the building; lines keep LINE_MIN_GAP apart, at most MAX_LINES per floor.
    natural = [(columns[k][1] + columns[k + 1][0]) / 2 for k in range(len(columns) - 1) if columns[k + 1][0] - columns[k][1] > 1200]
    even_count = max(2, int((building_max - building_min - 2 * LINE_EDGE_MARGIN) / LINE_EVEN_SPACING))
    even = [building_min + LINE_EDGE_MARGIN + (building_max - building_min - 2 * LINE_EDGE_MARGIN) * (k + .5) / even_count for k in range(even_count)]
    line_xs = []
    for x in natural + even:
        if len(line_xs) < MAX_LINES and all(abs(x - o) >= LINE_MIN_GAP for o in line_xs):
            line_xs.append(x)
    line_xs.sort()
    # Arrival spots and the stairs to the basement stay clear of any line.
    keep_clear = [(a.get_actor_location().x, a.get_actor_location().y) for a in actors_sub.get_all_level_actors() if "BasementLinkAuto" in [str(t) for t in a.tags]]
    def openings_at(x):
        runs, start = [], None
        y = grid.y0
        while y <= grid.y0 + grid.h * CELL:
            ok = grid.ok(x, y) and not shut_in((x, y))
            if ok and start is None:
                start = y
            elif not ok and start is not None:
                runs.append((start, y - CELL))
                start = None
            y += CELL / 2
        return [r for r in runs if r[1] - r[0] >= 120]
    compartments = []
    placed_xs = []
    for line_index, line_x in enumerate(line_xs):
        best_x = min((line_x + dx for dx in range(-800, 801, 100)), key=lambda x: sum(b - a for a, b in openings_at(x)) + abs(x - line_x) * .05)
        if any(abs(best_x - o) < LINE_MIN_GAP * .6 for o in placed_xs):
            continue  # nudged onto a neighbour: one line there is enough
        placed_xs.append(best_x)
        for y0, y1 in openings_at(best_x):
            mid = (best_x, (y0 + y1) / 2)
            if near_gate(mid, 400) or any(abs(best_x - c[0]) < 400 and y0 - 400 < c[1] < y1 + 400 for c in keep_clear):
                continue
            half = (y1 - y0) / 2 - 20.0  # ends reach the wall line (navmesh edge + 20 cm), not into the wall
            k = nearest_kept(mid)
            compartments.append((mid, (1.0, 0.0), (0.0, 1.0), half, half, 2 * half + 2 * AGENT_MARGIN, k, ("line", line_index)))
    floor_report["compartments"] = len(compartments)
    floor_report["compartment_lines"] = len({c[7][1] for c in compartments})

    # The exit row (user, 2026-11-15): the 1F exit is always shut, working shutters mixed with broken ones (the runtime picks
    # which, ExitRow_<n>). The row goes wall to wall across the narrowest passage within 15 m in front of the exit that really
    # cuts the exit off from the rest of the floor (a flood fill from the exit side must stay small).
    def row_cells(a, b, pad=60.0):
        cells, n = set(), max(1, int(math.dist(a, b) / 20.0))
        for k in range(n + 1):
            x, y = a[0] + (b[0] - a[0]) * k / n, a[1] + (b[1] - a[1]) * k / n
            for dx in (-pad, -pad / 2, 0, pad / 2, pad):
                for dy in (-pad, -pad / 2, 0, pad / 2, pad):
                    cells.add(grid.cell(x + dx, y + dy))
        return cells

    def cut_off(start, blocked, limit=5000):
        seen, stack = {grid.cell(*start)}, [grid.cell(*start)]
        while stack:
            i, j = stack.pop()
            for ni, nj in ((i + 1, j), (i - 1, j), (i, j + 1), (i, j - 1)):
                if (ni, nj) in seen or (ni, nj) in blocked or not (0 <= ni < grid.w and 0 <= nj < grid.h) or not grid.walk[nj * grid.w + ni]:
                    continue
                seen.add((ni, nj))
                stack.append((ni, nj))
                if len(seen) > limit:
                    return False
        return True

    exit_rows = []
    for e in (f["exits"] if floor == "1F" else []):  # the way out of the building is on 1F only
        best = None
        for step in range(8):
            u = (math.cos(step * math.pi / 4), math.sin(step * math.pi / 4))
            pp = (-u[1], u[0])
            for t in range(150, 1501, 50):
                c = (e[0] + u[0] * t, e[1] + u[1] * t)
                if not grid.ok(*c) or shut_in(c):
                    continue
                left, right, w = width_at(c, pp)
                if w is None or w >= 99999 or w > 1800:
                    continue
                score = w + t * .5
                if best and score >= best[0]:
                    continue
                a = (c[0] - pp[0] * left, c[1] - pp[1] * left)
                b = (c[0] + pp[0] * right, c[1] + pp[1] * right)
                behind = (c[0] - u[0] * 120, c[1] - u[1] * 120)  # the exit side of the row
                if not grid.ok(*behind) or not cut_off(behind, row_cells(a, b)):
                    continue
                # The WHOLE escape volume stays behind the row, a body width clear of it (2026-11-16: a diagonal row once
                # crossed the middle of the box, so part of it lay outside and anyone could escape without opening a shutter).
                if any((q[0] - c[0]) * u[0] + (q[1] - c[1]) * u[1] > -EXIT_BOX_CLEARANCE for q in f["exit_boxes"].get(e, [e])):
                    continue
                # Centred on the run, ends at the wall line (navmesh edge + 20 cm).
                mid = (c[0] + pp[0] * (right - left) / 2, c[1] + pp[1] * (right - left) / 2)
                half = w / 2 - 20.0
                best = (score, (mid, u, pp, half, half, 2 * half + 2 * AGENT_MARGIN, nearest_kept(mid), ("exit", len(exit_rows))))
        if best:
            exit_rows.append(best[1])
        else:
            floor_report["warnings"].append("exit at (%d, %d): no passage in front of it to shut" % e)
    floor_report["exit_rows"] = len(exit_rows)
    # Compartment openings right at the exit row would double it.
    compartments = [c for c in compartments if all(math.dist(c[0], r[0]) > 600 for r in exit_rows)]

    # Spread out: corridors and chokepoints 12 m apart from each other, store fronts 5 m apart (6 m from the rest).
    import random
    shuffle = random.Random(floor)
    chosen = [("compartment", c) for c in compartments] + [("exit", r) for r in exit_rows]
    # Scattered corridor rows are no longer placed (they looked random); store fronts stay.
    for kind, cap in (("store", 24),):
        pool = candidates[kind][:]
        shuffle.shuffle(pool)
        if kind == "main":
            # Main corridor rows go where the way actually runs (not the car park or a side hall).
            pool = [c for c in pool if traffic.get(c[6], 0) + traffic.get(c[7], 0) > 0]
            pool.sort(key=lambda c: -(traffic.get(c[6], 0) + traffic.get(c[7], 0)))
        count = 0
        for cand in pool:
            if count >= cap:
                break
            ok = True
            for other_kind, other in chosen:
                need = (500 if other_kind == "store" else 600) if kind == "store" else (600 if other_kind == "store" else BARRIER_SPACING)
                if math.dist(cand[0], other[0]) < need:
                    ok = False
                    break
            if ok:
                chosen.append((kind, cand))
                count += 1
    roles = {"exit": unreal.ProjectShutterLayoutRole.EXIT, "compartment": unreal.ProjectShutterLayoutRole.CHOKEPOINT, "main": unreal.ProjectShutterLayoutRole.CORRIDOR, "corridor": unreal.ProjectShutterLayoutRole.CORRIDOR, "chokepoint": unreal.ProjectShutterLayoutRole.CHOKEPOINT, "store": unreal.ProjectShutterLayoutRole.STORE}
    for index, (kind, (m, d, p, left, right, span, _a, _b)) in enumerate(chosen):
        a = (m[0] - p[0] * (left + AGENT_MARGIN), m[1] - p[1] * (left + AGENT_MARGIN))
        n = max(1, math.ceil(span / SHUTTER_WIDTH))
        yaw = math.degrees(math.atan2(d[1], d[0]))
        for s in range(n):
            c = (a[0] + p[0] * span * (s + .5) / n, a[1] + p[1] * span * (s + .5) / n)
            shutter = spawn(shutter_class, "%s_%s_%02d_%d" % (kind.capitalize(), floor, index, s), (c[0], c[1], z + 10), yaw, (1, span / n / SHUTTER_WIDTH, SHUTTER_SCALE_Z))
            shutter.set_editor_property("open_height", SHUTTER_OPEN_HEIGHT)
            shutter.set_editor_property("move_speed", SHUTTER_SPEED)
            shutter.set_editor_property("layout_role", roles[kind])
            if kind == "compartment" and isinstance(_b, tuple):
                shutter.tags = [TAG, "CompLine_%d" % _b[1]]  # the runtime may leave a whole line open in a match
            if kind == "exit":
                shutter.tags = [TAG, "ExitRow_%d" % _b[1]]  # always shut; the runtime picks the working panels
        paint.append((kind, [a, (a[0] + p[0] * span, a[1] + p[1] * span)]))
    floor_report["barriers"] = {kind: sum(1 for k, _ in chosen if k == kind) for kind in candidates}
    floor_report["candidates"] = {kind: len(v) for kind, v in candidates.items()}

    # Real collision (C++, after the meshes build): narrow shutters that reach into a wall or column, lower those under a
    # lintel or beam, so no panel sticks into the modelled walls.
    narrowed, lowered, dropped = (int(v) for v in unreal.ProjectMapPlacementLibrary.fit_shutters_to_walls(world, CEILING_HEIGHT).split(","))
    floor_report["shutters_fitted"] = {"narrowed": narrowed, "lowered": lowered, "removed": dropped}

    # ------------------------------------------------------------ lights and darkness (a dark, ruined store; lights still there)
    # Pools of light along the way with darkness between them, dead fixtures here and there, a red emergency light over every
    # way out, a few weak lights off the way. No flicker is added here: the reactive fixtures only fail near mannequins.
    light_class = unreal.EditorAssetLibrary.load_blueprint_class("/Game/MyProject/MannequinReactiveLight")
    light_shuffle = random.Random(floor + "-lights")
    fixtures = []  # (point, kind)
    def far_from_fixtures(pt, need):
        return all(math.dist(pt, o[0]) >= need for o in fixtures)
    # Never over a shutter line: a fixture there would stick into the panel (rows reach the ceiling).
    shutter_lines = [tuple(pts) for kind_, pts in paint if kind_ in ("exit", "compartment", "main", "corridor", "chokepoint", "store")]
    shutter_lines += [(sa, sb) for r in rings for sa, sb in r["blocks"]]
    def clear_of_shutters(pt):
        # Also clear of the modelled walls: open floor all around (the navmesh edge is already ~40 cm off a wall).
        if not all(grid.ok(pt[0] + math.cos(a_) * r_, pt[1] + math.sin(a_) * r_) for r_ in (LIGHT_WALL_CLEARANCE * .5, LIGHT_WALL_CLEARANCE)
                   for a_ in [i_ * math.pi / 4 for i_ in range(8)]):
            return False
        return all(dist_to_poly(pt, [a_, b_]) >= LIGHT_SHUTTER_CLEARANCE for a_, b_ in shutter_lines)
    def clear_node_near(target, reach=1500.0):
        near = sorted((k for k in keep if math.dist(node_at[k], target) <= reach), key=lambda k: math.dist(node_at[k], target))
        return next((k for k in near if clear_of_shutters(node_at[k])), None)
    # Ways out first: over each escalator gate and the exit.
    for r in rings:
        mids = [((sa[0] + sb[0]) / 2, (sa[1] + sb[1]) / 2) for sa, sb in r["blocks"]]
        if mids:
            gate = (sum(m[0] for m in mids) / len(mids), sum(m[1] for m in mids) / len(mids))
            k = clear_node_near(gate)
            if k:
                fixtures.append((node_at[k], "exit"))
    for e in f["exits"]:
        k = clear_node_near(e)
        if k:
            fixtures.append((node_at[k], "exit"))
    # A lit fixture by the survivors' starts, so a match never begins in the dark.
    for sp in f["spawns"]:
        k = clear_node_near(sp)
        if k and far_from_fixtures(node_at[k], 1000):
            fixtures.append((node_at[k], "start"))
    # Then the way: busiest route nodes first, 14 m apart.
    route = sorted((k for k in keep if traffic.get(k, 0) > 0), key=lambda k: -traffic[k])
    for k in route:
        if len(fixtures) >= LIGHT_CAP:
            break
        if far_from_fixtures(node_at[k], FLOOR_LIGHT_SPACING.get(floor, (LIGHT_SPACING_ROUTE, 0))[0]) and clear_of_shutters(node_at[k]):
            fixtures.append((node_at[k], "route"))
    # A few weak lights off the way (side corridors, stores), 22 m apart.
    side = [k for k in keep if traffic.get(k, 0) == 0]
    light_shuffle.shuffle(side)
    for k in side:
        if len(fixtures) >= LIGHT_CAP:
            break
        if far_from_fixtures(node_at[k], FLOOR_LIGHT_SPACING.get(floor, (0, LIGHT_SPACING_SIDE))[1]) and clear_of_shutters(node_at[k]):
            fixtures.append((node_at[k], "side"))
    # Fill the pitch-black pockets: any floor spot still farther than the gap from every fixture gets a weak light.
    gap = FLOOR_DARK_GAP.get(floor, 2400.0)
    # Only where people go: within 25 m of the way (not car parks or far side halls), at most 15 per floor.
    way_points = [node_at[k] for k in keep if traffic.get(k, 0) > 0]
    near_way = [k for k in keep if any(math.dist(node_at[k], w) <= 2500 for w in way_points[::3])]
    filled = 0
    for k in sorted(near_way, key=lambda k: -min((math.dist(node_at[k], o[0]) for o in fixtures), default=1e9)):
        if filled >= 15:
            break
        if not far_from_fixtures(node_at[k], gap):
            continue
        # Stand a little off this spot if it is next to a wall or shutter: the nearest clear node within 6 m.
        spot = k if clear_of_shutters(node_at[k]) else clear_node_near(node_at[k], 600.0)
        if spot and far_from_fixtures(node_at[spot], gap * .6):
            fixtures.append((node_at[spot], "side"))
            filled += 1
    counts = {"exit": 0, "start": 0, "route": 0, "side": 0, "dead": 0}
    for index, (pt, kind) in enumerate(fixtures):
        # The Blueprint is authored level: housing flat against the ceiling, its spot light already aimed down. Only turn it
        # to run along the corridor (most links at this spot east-west: 0 deg, else 90 deg).
        k = nearest_kept(pt)
        along_x = sum(1 for v in adjacency.get(k, ()) if v[1] == k[1]) if k else 1
        along_y = sum(1 for v in adjacency.get(k, ()) if v[0] == k[0]) if k else 0
        light = spawn(light_class, "Light_%s_%03d_%s" % (floor, index, kind), (pt[0], pt[1], z + CEILING_HEIGHT - 6), 0.0 if along_x >= along_y else 90.0)
        # The Blueprint's parts are authored for an actor tipped -90 deg (old placements). Set them in world space, as the
        # intro downlights were: round housing and diffuser flat against the ceiling, the spot light aimed straight down.
        top = unreal.Vector(pt[0], pt[1], z + CEILING_HEIGHT - 6)
        for prop, drop in (("fixture_mesh", 0.0), ("bulb_mesh", 3.0)):
            part = light.get_editor_property(prop)
            part.set_world_location(unreal.Vector(top.x, top.y, top.z - drop), False, False)
            part.set_world_rotation(unreal.Rotator(), False, False)
        beam = light.get_editor_property("reactive_spot_light")
        beam.set_world_location(unreal.Vector(top.x, top.y, top.z - 10), False, False)
        beam.set_world_rotation(unreal.Rotator(pitch=-90.0), False, False)
        light.tags = [TAG, "LightKind_" + kind]  # the per-match dark zones never put out start or exit lights
        dead = kind == "side" and light_shuffle.random() < DEAD_FIXTURE_CHANCE  # the way stays lit; only side areas have dead fixtures
        if dead:
            light.set_editor_property("light_on_intensity", 0.0)
            light.set_editor_property("emissive_on_value", 0.0)
            counts["dead"] += 1
        else:
            light.set_editor_property("light_on_intensity", {"exit": 90000.0, "start": 70000.0, "route": 130000.0, "side": 30000.0}[kind])  # 6 m ceiling: about 2.1x the 4.14 m corridor value
        spot = light.get_editor_property("reactive_spot_light")
        if kind == "exit":
            spot.set_editor_property("light_color", unreal.Color(r=255, g=70, b=55, a=255))   # emergency red: the way out
        else:
            spot.set_editor_property("light_color", unreal.Color(r=205, g=222, b=255, a=255))  # cold fluorescent
        counts[kind] += 1
        paint.append(("light_dead" if dead else "light_" + kind, [pt]))
    floor_report["lights"] = counts
    # The floor scan cannot see walls up at the ceiling (a lintel over a store front): check with real collision
    # (C++, after the meshes build) and move any fixture that touches one to the nearest clear spot.
    moved, removed = (int(v) for v in unreal.ProjectMapPlacementLibrary.move_fixtures_out_of_walls(world, 80.0).split(","))
    floor_report["lights_moved_off_walls"] = moved
    floor_report["lights_removed_no_room"] = removed

    # Darkness: no sun, almost no sky light, dark fog, fixed exposure (automatic exposure would brighten the dark back up).
    for actor in actors_sub.get_all_level_actors():
        cls = actor.get_class().get_name()
        if cls == "DirectionalLight":
            actor.light_component.set_editor_property("intensity", 0.0)
        elif cls == "SkyLight":
            actor.light_component.set_editor_property("intensity", SKY_INTENSITY)
        elif cls == "ExponentialHeightFog":
            fog = actor.component
            fog.set_editor_property("fog_density", 0.035)
            fog.set_editor_property("fog_inscattering_luminance", unreal.LinearColor(0.006, 0.008, 0.011, 1))
            fog.set_editor_property("start_distance", 400.0)
    # Indoors: the frame meshes have no ceiling, so the night sky showed overhead. A dark ceiling slab over the whole
    # building (no collision, no navigation, no shadow) and no sky or clouds drawn. Raised shutters tuck away above it.
    for actor in actors_sub.get_all_level_actors():
        if actor.get_class().get_name() in ("SkyAtmosphere", "VolumetricCloud"):
            actor.set_actor_hidden_in_game(True)
            for comp in actor.get_components_by_class(unreal.SceneComponent):
                comp.set_visibility(False, True)
    # Ceiling slabs, open over every floor opening (the atriums: escalator and elevator voids). Strips of 5 m rows,
    # contiguous runs merged into one slab each, so looking up through a void shows the dark storey above.
    open_polys = [offset(h["poly"], 150) for h in f["holes"]]
    ceiling_mesh = unreal.load_asset("/Engine/BasicShapes/Cube")
    ceiling_material = unreal.load_asset("/Game/MyProject/Lighting/M_ReactiveFixture")
    strip = 500.0
    slabs = 0
    for row in range(int(grid.h * CELL / strip) + 1):
        y = grid.y0 + (row + .5) * strip
        run_start = None
        cols = int(grid.w * CELL / strip) + 1
        for col in range(cols + 1):
            x = grid.x0 + (col + .5) * strip
            covered = col < cols and not any(inside(poly, (x + dx, y + dy)) for poly in open_polys for dx in (-200, 200) for dy in (-200, 200))
            if covered and run_start is None:
                run_start = col
            elif not covered and run_start is not None:
                x0 = grid.x0 + run_start * strip
                x1 = grid.x0 + col * strip
                ceiling = spawn(unreal.StaticMeshActor, "Ceiling_%s_%02d_%02d" % (floor, row, run_start), ((x0 + x1) / 2, y, z + CEILING_HEIGHT + 10), 0.0,
                                ((x1 - x0) / 100.0, strip / 100.0, 0.2))
                slab = ceiling.static_mesh_component
                slab.set_static_mesh(ceiling_mesh)
                slab.set_material(0, ceiling_material)
                slab.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
                slab.set_editor_property("can_ever_affect_navigation", False)
                slab.set_editor_property("cast_shadow", False)
                slabs += 1
                run_start = None
    floor_report["ceiling_slabs"] = slabs
    post = spawn(unreal.PostProcessVolume, "Darkness_PostProcess_" + floor, (grid.x0, grid.y0, z + 200))
    post.set_editor_property("unbound", True)
    pp = post.get_editor_property("settings")
    for name, value in (("auto_exposure_method", unreal.AutoExposureMethod.AEM_MANUAL), ("auto_exposure_apply_physical_camera_exposure", False),
                        ("auto_exposure_bias", EXPOSURE_BIAS + FLOOR_EXPOSURE.get(floor, 0.0)), ("vignette_intensity", 0.55), ("color_saturation", unreal.Vector4(0.82, 0.84, 0.9, 1.0)),
                        ("color_gain", unreal.Vector4(0.92, 0.97, 1.05, 1.0)), ("motion_blur_amount", 0.0)):
        pp.set_editor_property("override_" + name, True)
        pp.set_editor_property(name, value)
    post.set_editor_property("settings", pp)

    # Route node actors (links on one side only).
    actor_of = {}
    for k in sorted(keep):
        p = node_at[k]
        actor_of[k] = spawn(unreal.ProjectRouteNode, "Route_%s_%03d_%03d" % (floor, k[0], k[1]), (p[0], p[1], z + 50))
    out_links = {}
    for a, b in links:
        out_links.setdefault(a, []).append(actor_of[b])
    for k, targets in out_links.items():
        actor_of[k].set_editor_property("links", targets)
    for e in f["exits"]:
        nearest = min(keep, key=lambda k: math.dist(node_at[k], e)) if keep else None
        if nearest:
            actor_of[nearest].set_editor_property("exit", True)
    floor_report["nodes"], floor_report["links"] = len(keep), len(links)
    for s in f["spawns"]:
        if shut_in(s):
            floor_report["warnings"].append("survivor spawn %s is inside an escalator ring" % (s,))

    layout = spawn(unreal.ProjectShutterLayout, "FireShutterLayout_" + floor, (grid.x0, grid.y0, z + 300))
    if floor == "1F":
        layout.set_editor_property("min_sealed_corridors", 1)
    for actor in actors_sub.get_all_level_actors():
        if actor.get_class().get_name() == "RecastNavMesh":
            actor.set_editor_property("runtime_generation", unreal.RuntimeGenerationType.DYNAMIC_MODIFIERS_ONLY)

    # Preview image.
    px = [(225, 225, 225) if grid.walk[i] else (25, 25, 25) for i in range(grid.w * grid.h)]
    def dot(x, y, color, r=2):
        i, j = grid.cell(x, y)
        for dj in range(-r, r + 1):
            for di in range(-r, r + 1):
                if 0 <= i + di < grid.w and 0 <= j + dj < grid.h:
                    px[(j + dj) * grid.w + i + di] = color
    for a, b in links:
        pa, pb = node_at[a], node_at[b]
        for s in range(13):
            dot(pa[0] + (pb[0] - pa[0]) * s / 12, pa[1] + (pb[1] - pa[1]) * s / 12, (150, 170, 200), 0)
    for kind, pts in paint:
        if kind in ("exit", "compartment", "main", "corridor", "chokepoint", "store"):
            a, b = pts
            color = {"exit": (255, 0, 120), "compartment": (0, 200, 220), "main": (255, 60, 0), "corridor": (240, 140, 0), "chokepoint": (0, 200, 220), "store": (230, 210, 40)}[kind]
            for s in range(21):
                dot(a[0] + (b[0] - a[0]) * s / 20, a[1] + (b[1] - a[1]) * s / 20, color, 1)
        elif kind.startswith("light_"):
            color = {"light_route": (255, 245, 170), "light_start": (120, 200, 255), "light_side": (170, 160, 110), "light_exit": (255, 40, 40), "light_dead": (90, 80, 60)}[kind]
            dot(pts[0][0], pts[0][1], color, 2)
        else:
            dot(pts[0][0], pts[0][1], (220, 40, 40) if kind == "ring" else (40, 190, 60), 2 if kind == "ring" else 3)
    for s in f["spawns"]:
        dot(s[0], s[1], (40, 110, 255), 4)
    for s in f["mannequins"]:
        dot(s[0], s[1], (150, 0, 150), 3)
    for e in f["elevators"]:
        dot(e[0], e[1], (255, 0, 220), 5)
    write_bmp(OUT + floor + "_Placement.bmp", grid.w, grid.h, px)

    if APPLY:
        unreal.EditorLoadingAndSavingUtils.save_map(world, path)
    report["floors"][floor] = floor_report
    unreal.log("[ShutterLayout] %s: %s" % (floor, json.dumps(floor_report, ensure_ascii=False)))

with open(OUT + "PlacementReport.json", "w", encoding="utf-8") as fp:
    json.dump(report, fp, ensure_ascii=False, indent=1)
unreal.log("[ShutterLayout] done (%s)" % ("maps saved" if APPLY else "dry run, nothing saved"))
