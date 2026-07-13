"""Authors the streamed-world detail meshes and exports GLBs.

Run headless:  blender --background --python scripts/build_detail_assets.py

Conifers use a procedural branching generator: a wandering tapered trunk,
golden-angle spiral branches with droop, and jagged geometric needle sprays
(no alpha cards needed). Every asset keeps its origin at the ground plane.
"""
import math
import os
import random

import bpy
from mathutils import Quaternion, Vector

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TEXTURES = os.path.join(ROOT, "assets", "textures")
MODELS = os.path.join(ROOT, "assets", "models")
GOLDEN_ANGLE = 2.39996


def reset_scene():
    bpy.ops.wm.read_factory_settings(use_empty=True)


def textured_material(name, png, roughness=0.85):
    material = bpy.data.materials.new(name)
    material.use_nodes = True
    bsdf = material.node_tree.nodes["Principled BSDF"]
    bsdf.inputs["Roughness"].default_value = roughness
    image_node = material.node_tree.nodes.new("ShaderNodeTexImage")
    image_node.image = bpy.data.images.load(os.path.join(TEXTURES, png))
    material.node_tree.links.new(image_node.outputs["Color"], bsdf.inputs["Base Color"])
    return material


def solid_material(name, rgba, roughness=0.9):
    material = bpy.data.materials.new(name)
    material.use_nodes = True
    bsdf = material.node_tree.nodes["Principled BSDF"]
    bsdf.inputs["Base Color"].default_value = rgba
    bsdf.inputs["Roughness"].default_value = roughness
    return material


def export_glb(filename):
    asset_filter = os.environ.get("AETHERWAKE_ASSET_FILTER", "").strip()
    if asset_filter and not any(token.strip() in filename for token in asset_filter.split(",")):
        return
    bpy.ops.export_scene.gltf(
        filepath=os.path.join(MODELS, filename),
        export_format="GLB",
        export_apply=True,
        export_yup=True,
    )


def ensure_birch_bark_texture():
    """Create a reusable 512px procedural bark plate with fine lenticels."""
    path = os.path.join(TEXTURES, "silver_birch_bark_albedo.png")
    if os.path.exists(path):
        return
    size = 512
    image = bpy.data.images.new("SilverBirchBarkGenerated", width=size, height=size, alpha=True)
    pixels = [0.0] * (size * size * 4)
    rng = random.Random(1907)
    scars = [(rng.randrange(0, size), rng.randrange(0, size), rng.randrange(8, 42), rng.randrange(1, 4)) for _ in range(145)]
    for y in range(size):
        for x in range(size):
            grain = math.sin(x * 0.17 + math.sin(y * 0.031) * 2.4) * 0.035 + math.sin(x * 0.047 + y * 0.013) * 0.025
            fleck = math.sin(x * 1.71 + y * 2.37) * math.sin(x * 0.29 - y * 0.83) * 0.018
            value = 0.60 + grain + fleck
            for sx, sy, length, thickness in scars:
                dy = abs(y - sy)
                dx = abs(x - sx)
                if dy <= thickness and dx < length:
                    value *= 0.25 + 0.45 * dx / max(length, 1)
            index = (y * size + x) * 4
            pixels[index] = value * 0.88; pixels[index + 1] = value * 0.91; pixels[index + 2] = value * 0.86; pixels[index + 3] = 1.0
    image.pixels.foreach_set(pixels); image.filepath_raw = path; image.file_format = "PNG"; image.save()
    bpy.data.images.remove(image)


def add_cone_between(p0, p1, r0, r1, sides=7):
    """Tapered cylinder from p0 to p1 (mathutils Vectors)."""
    axis = p1 - p0
    length = axis.length
    bpy.ops.mesh.primitive_cone_add(vertices=sides, radius1=r0, radius2=r1, depth=length)
    segment = bpy.context.active_object
    segment.rotation_mode = "QUATERNION"
    segment.rotation_quaternion = axis.to_track_quat("Z", "Y")
    segment.location = (p0 + p1) * 0.5
    return segment


def add_profiled_form(name, location, profile, segments, material, scale=(1.0, 1.0, 1.0),
                      radial_noise=0.0, phase=0.0, smooth_profile=True):
    """Build a purpose-shaped radial mesh instead of stretching a stock sphere."""
    if smooth_profile and len(profile) >= 4:
        controls = profile; dense_profile = []
        for index in range(len(controls) - 1):
            p0 = controls[max(0, index - 1)]; p1 = controls[index]
            p2 = controls[index + 1]; p3 = controls[min(len(controls) - 1, index + 2)]
            for subdivision in range(4):
                t = subdivision / 4.0; t2 = t * t; t3 = t2 * t
                height = p1[0] + (p2[0] - p1[0]) * t
                radius = 0.5 * ((2.0 * p1[1]) + (-p0[1] + p2[1]) * t
                    + (2.0 * p0[1] - 5.0 * p1[1] + 4.0 * p2[1] - p3[1]) * t2
                    + (-p0[1] + 3.0 * p1[1] - 3.0 * p2[1] + p3[1]) * t3)
                dense_profile.append((height, max(0.001, radius)))
        dense_profile.append(controls[-1]); profile = dense_profile
    verts, faces = [], []
    for ring_index, (height, radius) in enumerate(profile):
        height_t = ring_index / max(1, len(profile) - 1)
        for segment in range(segments):
            angle = segment / segments * math.tau
            relief = 1.0 + radial_noise * (
                math.sin(angle * 3.0 + phase + height_t * 1.45) * 0.55
                + math.sin(angle * 7.0 - phase * 0.7 - height_t * 2.10) * 0.28
                + math.sin(angle * 13.0 + height_t * 3.20) * 0.17)
            verts.append((location[0] + math.cos(angle) * radius * relief * scale[0],
                          location[1] + math.sin(angle) * radius * relief * scale[1],
                          location[2] + height * scale[2]))
    for ring in range(len(profile) - 1):
        for segment in range(segments):
            nxt = (segment + 1) % segments
            a = ring * segments + segment; b = ring * segments + nxt
            c = (ring + 1) * segments + nxt; d = (ring + 1) * segments + segment
            faces.append((a, b, c, d))
    mesh = bpy.data.meshes.new(name + "Mesh"); mesh.from_pydata(verts, [], faces); mesh.materials.append(material)
    uv_layer = mesh.uv_layers.new(name="AuthoredRadialUV")
    for polygon in mesh.polygons:
        for loop_index in polygon.loop_indices:
            vertex_index = mesh.loops[loop_index].vertex_index
            ring_index = vertex_index // segments; segment_index = vertex_index % segments
            uv_layer.data[loop_index].uv = (segment_index / segments, ring_index / max(1, len(profile) - 1))
    for polygon in mesh.polygons: polygon.use_smooth = True
    obj = bpy.data.objects.new(name, mesh); bpy.context.collection.objects.link(obj)
    return obj


def add_eroded_stone(name, location, radius, material, rng, scale=(1.0, 1.0, 1.0)):
    """Dense hand-shaped stone with chipped planes and multi-frequency relief."""
    # A broad fractured shoulder and off-axis crown produce a geological mass,
    # not an inflated ball or a stack of concentric pancakes.
    rings = [(radius * z, radius * r) for z, r in (
        (-0.46, 0.24), (-0.41, 0.66), (-0.29, 0.91), (-0.10, 1.02),
        (0.12, 0.98), (0.31, 0.84), (0.45, 0.61), (0.53, 0.38), (0.53, 0.025))]
    obj = add_profiled_form(name, location, rings, 40, material, scale,
                            radial_noise=rng.uniform(0.07, 0.13), phase=rng.random() * math.tau)
    min_z = min(vertex.co.z for vertex in obj.data.vertices)
    max_z = max(vertex.co.z for vertex in obj.data.vertices)
    crown_x = rng.uniform(-0.22, 0.22) * radius * scale[0]
    crown_y = rng.uniform(-0.18, 0.18) * radius * scale[1]
    for vertex in obj.data.vertices:
        t = max(0.0, min(1.0, (vertex.co.z - min_z) / max(0.001, max_z - min_z)))
        crown_weight = t * t * (3.0 - 2.0 * t)
        vertex.co.x += crown_x * crown_weight
        vertex.co.y += crown_y * crown_weight
    # Repeat the source plate so close-up grain remains granular instead of
    # stretching into broad horizontal streaks around the circumference.
    for uv in obj.data.uv_layers[0].data:
        uv.uv.x *= 2.35; uv.uv.y *= 1.65
    return obj


def add_seed_grain(name, center, radius, material, direction=None):
    profile = [(-radius, 0.010 * radius), (-0.72 * radius, 0.45 * radius),
               (0.0, radius), (0.68 * radius, 0.52 * radius), (radius, 0.015 * radius)]
    grain = add_profiled_form(name, center, profile, 12, material, radial_noise=0.05)
    if direction is not None:
        grain.rotation_mode = "QUATERNION"; grain.rotation_quaternion = direction.to_track_quat("Z", "Y")
    return grain


def add_bell_bloom(name, center, radius, height, material, lobes=5):
    profile = [(0.0, radius * 0.22), (height * 0.18, radius * 0.76),
               (height * 0.68, radius), (height, radius * 0.72)]
    return add_profiled_form(name, center, profile, max(20, lobes * 5), material,
                             radial_noise=0.11, phase=math.pi / lobes)


def add_crystal(name, center, radius, height, material, facets=9):
    profile = [(-height * 0.50, radius * 0.16), (-height * 0.34, radius * 0.88),
               (height * 0.27, radius), (height * 0.50, radius * 0.04)]
    obj = add_profiled_form(name, center, profile, facets, material, radial_noise=0.045, phase=0.31, smooth_profile=False)
    for polygon in obj.data.polygons: polygon.use_smooth = False
    return obj


def add_mushroom_cap(name, center, radius, material, gill_material, rng):
    """Umbonate cap with a rolled rim, concave underside, and radial gills."""
    profile = [(-radius * 0.22, radius * 0.06), (-radius * 0.18, radius * 0.64),
               (-radius * 0.10, radius), (radius * 0.03, radius * 0.93),
               (radius * 0.28, radius * 0.64), (radius * 0.43, radius * 0.10)]
    cap = add_profiled_form(name, center, profile, 40, material, radial_noise=0.045, phase=rng.random() * math.tau)
    for gill in range(28):
        angle = gill / 28 * math.tau
        inner = Vector(center) + Vector((math.cos(angle) * radius * 0.12, math.sin(angle) * radius * 0.12, -radius * 0.17))
        outer = Vector(center) + Vector((math.cos(angle) * radius * 0.88, math.sin(angle) * radius * 0.88, -radius * 0.105))
        rib = add_cone_between(inner, outer, radius * 0.012, radius * 0.004, sides=5)
        rib.name = name + "Gill"; rib.data.materials.append(gill_material)
    return cap


def spray_cluster(rng, verts, faces, face_sides, origin, direction, count, blade_length, spread, width_min=0.025, width_max=0.065):
    """Jagged needle blades fanning around a branch direction (built as raw geometry)."""
    direction = direction.normalized()
    arbitrary = Vector((0, 0, 1)) if abs(direction.z) < 0.9 else Vector((1, 0, 0))
    side = direction.cross(arbitrary).normalized()
    up = direction.cross(side).normalized()
    for _ in range(count):
        angle = rng.uniform(0.0, 6.2831853)
        tilt = rng.uniform(-spread, spread)
        blade_dir = (direction + side * math.cos(angle) * spread + up * math.sin(angle) * spread + Vector((0, 0, tilt * 0.5 - 0.22))).normalized()
        length = blade_length * rng.uniform(0.6, 1.25)
        width = length * rng.uniform(width_min, width_max)
        blade_side = blade_dir.cross(up if abs(blade_dir.dot(up)) < 0.9 else side).normalized() * width
        root = origin + direction * rng.uniform(-blade_length * 0.2, blade_length * 0.4)
        tip = root + blade_dir * length
        tip_side = blade_side * 0.055
        base_index = len(verts)
        verts.extend([root - blade_side * 0.5, root + blade_side * 0.5, tip + tip_side, tip - tip_side])
        faces.append((base_index, base_index + 1, base_index + 2, base_index + 3))
        face_sides.append(rng.random() < 0.5)


def build_conifer(filename, seed, height, branch_rows, branch_length, needle_dark, needle_lit):
    reset_scene()
    rng = random.Random(seed)
    bark = textured_material("ConiferBark", "pine_bark_diff.jpg")
    dark = solid_material("NeedlesDark", needle_dark)
    lit = solid_material("NeedlesLit", needle_lit)

    wood = []
    # Wandering tapered trunk built from stacked segments, with a broader
    # buttress and fine upper taper instead of a constant utility pole.
    high_detail = branch_rows > 20
    far_detail = "_far" in filename
    is_spruce = "spruce" in filename
    architecture = "windswept" if "_wind" in filename else "veteran" if "_old" in filename else "natural"
    segments = 12 if high_detail else 5 if far_detail else 7
    base = Vector((0, 0, 0))
    drift = Vector((0, 0, 0))
    points = [base.copy()]
    for i in range(1, segments + 1):
        wind_bias = Vector((0.075, -0.025, 0)) if architecture == "windswept" else Vector((0, 0, 0))
        veteran_kink = Vector((rng.uniform(-0.09, 0.09), rng.uniform(-0.09, 0.09), 0)) if architecture == "veteran" else Vector((0, 0, 0))
        drift += Vector((rng.uniform(-0.10, 0.10), rng.uniform(-0.10, 0.10), 0)) + wind_bias + veteran_kink
        points.append(Vector((drift.x, drift.y, height * i / segments)))
    for i in range(segments):
        t0, t1 = i / segments, (i + 1) / segments
        base_radius = 0.34 if is_spruce else 0.30
        r0 = base_radius * (1.0 - t0) ** 1.18 + 0.025
        r1 = base_radius * (1.0 - t1) ** 1.18 + 0.025
        if i == 0: r0 *= 1.34
        wood.append(add_cone_between(points[i], points[i + 1], r0, r1, sides=20 if high_detail else 7 if far_detail else 10))

    # Exposed root flares physically anchor the near asset into the soil.
    if high_detail:
        for root_index in range(7):
            angle = root_index / 7.0 * math.tau + rng.uniform(-0.18, 0.18)
            p0 = Vector((math.cos(angle) * 0.16, math.sin(angle) * 0.16, 0.14))
            p1 = Vector((math.cos(angle) * rng.uniform(0.52, 0.90), math.sin(angle) * rng.uniform(0.52, 0.90), 0.015))
            wood.append(add_cone_between(p0, p1, rng.uniform(0.10, 0.16), 0.015, sides=10))

    spray_verts, spray_faces, spray_side = [], [], []
    whorls = ((17 if is_spruce else 18) if high_detail else
              (9 if is_spruce else 10) if far_detail else (13 if is_spruce else 14))
    for row in range(whorls):
        row_jitter = rng.uniform(-0.018, 0.018) if 0 < row < whorls - 1 else 0.0
        t = max(0.11, min(0.95, 0.14 + 0.79 * row / (whorls - 1) + row_jitter))
        trunk_point = points[min(int(t * segments), segments - 1)].lerp(points[min(int(t * segments) + 1, segments)], (t * segments) % 1.0)
        whorl_rotation = row * GOLDEN_ANGLE * rng.uniform(0.34, 0.53) + rng.uniform(-0.42, 0.42)
        # Real conifers do not repeat a three-arm radial scaffold. Juvenile
        # growth may form whorls, but crowding, damage and light competition
        # leave two to five surviving primaries with unequal spacing.
        if architecture == "veteran":
            arm_count = rng.choice((2, 2, 3, 4, 5))
        elif architecture == "windswept":
            arm_count = rng.choice((2, 3, 3, 4))
        else:
            arm_count = rng.choice((2, 3, 3, 4, 4))
        for arm in range(arm_count):
            gap_chance = 0.18 if architecture == "veteran" else 0.12 if architecture == "windswept" else 0.08
            if row > 1 and rng.random() < gap_chance:
                continue  # storm gap; breaks perfect radial repetition
            azimuth = whorl_rotation + arm * math.tau / arm_count + rng.uniform(-0.30, 0.30)
            crown_taper = (1.0 - t) ** (0.62 if is_spruce else 0.78)
            side_light = math.cos(azimuth - 0.12)
            exposure = 1.0
            if architecture == "windswept": exposure = 0.48 + 0.62 * max(0.0, side_light)
            elif architecture == "veteran": exposure = rng.uniform(0.68, 1.18)
            length = (branch_length * crown_taper * rng.uniform(0.68, 1.24) + 0.20) * exposure
            base_droop = -0.58 if is_spruce else -0.34
            droop = base_droop + t * (0.46 if is_spruce else 0.34) + rng.uniform(-0.22, 0.18)
            if architecture == "veteran" and row < whorls * 0.45: droop -= rng.uniform(0.10, 0.32)
            if architecture == "windswept": droop += side_light * 0.08
            direction = Vector((math.cos(azimuth), math.sin(azimuth), droop)).normalized()
            # Three-stage primary limb. A single down/up elbow produces the
            # repeated wide-V silhouette; independent shoulder, sag and tip
            # points instead approximate the long irregular curve of a limb
            # responding to its own weight and local light.
            shoulder_fraction = rng.uniform(0.22, 0.34)
            middle_fraction = rng.uniform(0.55, 0.72)
            limb_side_curve = Vector((-math.sin(azimuth), math.cos(azimuth), 0)) * rng.uniform(-0.10, 0.10) * length
            shoulder = trunk_point + direction * length * shoulder_fraction + Vector((0, 0, rng.uniform(-0.045, 0.025) * length))
            middle = trunk_point + direction * length * middle_fraction + limb_side_curve * 0.55 + Vector((0, 0, -rng.uniform(0.055, 0.17) * length))
            tip_lift = rng.uniform(0.02, 0.20) if is_spruce else rng.uniform(0.07, 0.29)
            if architecture == "veteran" and rng.random() < 0.28:
                tip_lift = rng.uniform(-0.10, 0.08)  # weighted or storm-damaged tip
            tip = trunk_point + direction * length + limb_side_curve + Vector((0, 0, tip_lift * length))
            branch_radius = (0.060 if is_spruce else 0.052) * (1.0 - t * 0.52)
            primary_sides = 10 if high_detail else 5 if far_detail else 6
            tip_sides = 8 if high_detail else 4 if far_detail else 5
            wood.append(add_cone_between(trunk_point, shoulder, branch_radius, branch_radius * 0.76, sides=primary_sides))
            wood.append(add_cone_between(shoulder, middle, branch_radius * 0.78, branch_radius * 0.39, sides=primary_sides))
            wood.append(add_cone_between(middle, tip, branch_radius * 0.42, 0.006, sides=tip_sides))

            limb_direction = (tip - trunk_point).normalized()
            limb_side = limb_direction.cross(Vector((0, 0, 1))).normalized()
            # Lowest whorls retain a minority of dead, broken branches—the
            # foliage crown starts gradually instead of at a hard cutoff.
            dead_limb = (row < 3 and rng.random() < 0.42) or (architecture == "veteran" and row < whorls * 0.58 and rng.random() < 0.16)
            stations = (0.30, 0.62, 0.90) if far_detail else (0.24, 0.46, 0.68, 0.88)
            # Needle-bearing short shoots continue along the primary limb;
            # secondary twig clusters alone leave an artificial bare rail.
            if not dead_limb:
                primary_stations = ((0.22, 0.38, 0.54, 0.70, 0.86, 0.97) if high_detail else
                                    (0.30, 0.60, 0.88) if far_detail else (0.28, 0.50, 0.72, 0.92))
                for primary_index, primary_station in enumerate(primary_stations):
                    primary_center = trunk_point.lerp(tip, primary_station)
                    primary_center += limb_side * (0.08 if primary_index % 2 else -0.08) * (1.0 - t)
                    shoot_direction = (limb_direction + limb_side * (0.24 if primary_index % 2 else -0.24)
                                       + Vector((0, 0, 0.12 if not is_spruce else -0.03))).normalized()
                    spray_cluster(rng, spray_verts, spray_faces, spray_side, primary_center, shoot_direction,
                                  count=66 if high_detail else 10 if far_detail else 14,
                                  blade_length=(0.275 if high_detail else 0.64 if far_detail else 0.43) * (1.0 - t * 0.16), spread=0.82,
                                  width_min=0.042 if high_detail else 0.24 if far_detail else 0.14,
                                  width_max=0.112 if high_detail else 0.46 if far_detail else 0.27)
            for station in stations:
                origin = trunk_point.lerp(tip, station)
                fork_signs = (-1.0, 1.0) if high_detail else ((-1.0,) if (arm + row) % 2 else (1.0,))
                for fork_sign in fork_signs:
                    fork = (limb_direction * 0.48 + limb_side * fork_sign * rng.uniform(0.56, 0.96)
                            + Vector((0, 0, rng.uniform(0.16, 0.46) if not is_spruce else rng.uniform(-0.02, 0.24)))).normalized()
                    twig_length = (0.30 + (1.0 - t) * 0.34) * rng.uniform(0.82, 1.18)
                    twig_tip = origin + fork * twig_length
                    twig = add_cone_between(origin, twig_tip, 0.017 * (1.0 - t * 0.40), 0.0035, sides=7 if high_detail else 4)
                    wood.append(twig)
                    if dead_limb: continue
                    boughs = 3 if high_detail else 1 if far_detail else 2
                    for bough_index in range(boughs):
                        bough_center = origin.lerp(twig_tip, 0.27 + bough_index * (0.62 / max(1, boughs - 1)))
                        bough_center += limb_side * rng.uniform(-0.08, 0.08)
                        spray_cluster(rng, spray_verts, spray_faces, spray_side, bough_center, fork,
                                      count=58 if high_detail else 10 if far_detail else 15,
                                      blade_length=(0.245 if high_detail else 0.62 if far_detail else 0.42) * (1.0 - t * 0.18), spread=0.70,
                                      width_min=0.040 if high_detail else 0.23 if far_detail else 0.14,
                                      width_max=0.108 if high_detail else 0.44 if far_detail else 0.27)
    # Crown tuft at the very top.
    spray_cluster(rng, spray_verts, spray_faces, spray_side, points[-1], Vector((0, 0, 1)), count=156 if high_detail else 24 if far_detail else 40,
                  blade_length=0.30 if high_detail else 0.68 if far_detail else 0.48, spread=0.62,
                  width_min=0.040 if high_detail else 0.24 if far_detail else 0.13,
                  width_max=0.105 if high_detail else 0.44 if far_detail else 0.25)

    spray_mesh = bpy.data.meshes.new("NeedleSprays")
    spray_mesh.from_pydata(spray_verts, [], spray_faces)
    spray_mesh.update()
    sprays = bpy.data.objects.new("NeedleSprays", spray_mesh)
    bpy.context.collection.objects.link(sprays)
    sprays.data.materials.append(dark)
    sprays.data.materials.append(lit)
    for polygon, use_lit in zip(sprays.data.polygons, spray_side):
        polygon.material_index = 1 if use_lit else 0

    for obj in wood:
        obj.data.materials.append(bark)
    bpy.ops.object.select_all(action="SELECT")
    bpy.context.view_layer.objects.active = wood[0]
    bpy.ops.object.join()
    export_glb(filename)


def build_conifer_impostor(filename, seed, height, crown_width, is_spruce=False):
    """Ultra-far layered crown: species silhouette in only a few dozen faces."""
    reset_scene(); rng = random.Random(seed)
    bark = solid_material("FarTrunk", (0.18, 0.095, 0.045, 1.0), roughness=0.96)
    dark = solid_material("FarCrownDark", (0.018, 0.050, 0.026, 1.0), roughness=0.92)
    lit = solid_material("FarCrownLit", (0.032, 0.083, 0.039, 1.0), roughness=0.90)
    trunk = add_cone_between(Vector((0, 0, 0)), Vector((0.08, -0.04, height)), 0.30, 0.028, sides=8)
    trunk.name = "FarSilhouetteTrunk"; trunk.data.materials.append(bark)
    verts, faces, materials = [], [], []
    tiers = 15 if is_spruce else 14
    for tier in range(tiers):
        t = 0.14 + tier / max(1, tiers - 1) * 0.80
        z = height * t
        radius = crown_width * (1.0 - t) ** (0.58 if is_spruce else 0.72) + 0.18
        rotation = tier * GOLDEN_ANGLE * 0.37
        for arm in range(4):
            angle = rotation + arm * math.tau / 4.0
            direction = Vector((math.cos(angle), math.sin(angle), 0))
            side = Vector((-math.sin(angle), math.cos(angle), 0))
            root = Vector((0, 0, z + (0.03 if tier % 2 else -0.03) * height / tiers))
            mid = root + direction * radius * 0.56 + Vector((0, 0, -radius * (0.09 if is_spruce else 0.035)))
            tip = root + direction * radius + Vector((0, 0, -radius * (0.18 if is_spruce else 0.08)))
            half = radius * (0.18 if is_spruce else 0.14)
            vertical = radius * (0.20 if is_spruce else 0.16)
            index = len(verts)
            verts.extend([root + Vector((0, 0, vertical)), mid - side * half,
                          tip - Vector((0, 0, vertical * 0.32)), mid + side * half,
                          root - Vector((0, 0, vertical * 0.72))])
            faces.append((index, index + 1, index + 2, index + 3, index + 4))
            materials.append((tier + arm) % 2)
    # A narrow leader closes the crown without a cone-shaped canopy shell.
    for arm in range(3):
        angle = arm * math.tau / 3.0 + 0.4
        side = Vector((-math.sin(angle), math.cos(angle), 0)) * crown_width * 0.10
        root = Vector((0, 0, height * 0.86)); tip = Vector((0.08, -0.04, height * 1.02))
        index = len(verts); verts.extend([root - side, tip, root + side]); faces.append((index, index + 1, index + 2)); materials.append(arm % 2)
    mesh = bpy.data.meshes.new("LayeredFarCrown"); mesh.from_pydata(verts, [], faces); mesh.materials.append(dark); mesh.materials.append(lit)
    for polygon, material_index in zip(mesh.polygons, materials): polygon.material_index = material_index
    crown = bpy.data.objects.new("LayeredFarCrown", mesh); bpy.context.collection.objects.link(crown)
    export_glb(filename)


def build_snag():
    reset_scene()
    rng = random.Random(11)
    bark = textured_material("SnagBark", "pine_bark_diff.jpg", roughness=0.95)
    parts = [add_cone_between(Vector((0, 0, 0)), Vector((rng.uniform(-0.3, 0.3), rng.uniform(-0.3, 0.3), 6.4)), 0.24, 0.03, sides=18)]
    for _ in range(4):
        z = rng.uniform(2.2, 5.2)
        azimuth = rng.uniform(0, 6.283)
        direction = Vector((math.cos(azimuth), math.sin(azimuth), rng.uniform(-0.15, 0.45))).normalized()
        parts.append(add_cone_between(Vector((0, 0, z)), Vector((0, 0, z)) + direction * rng.uniform(0.9, 1.8), 0.07, 0.012, sides=10))
    for obj in parts:
        obj.data.materials.append(bark)
    bpy.ops.object.select_all(action="SELECT")
    bpy.context.view_layer.objects.active = parts[0]
    bpy.ops.object.join()
    export_glb("detail_snag.glb")


def build_boulder():
    reset_scene()
    basalt = textured_material("BoulderBasalt", "mossy_rock_diff.jpg", roughness=0.92)
    boulder = add_eroded_stone("FracturedBasaltBoulder", (0, 0, 0.42), 1.25, basalt,
                               random.Random(1703), scale=(1.30, 1.05, 0.72))
    export_glb("detail_boulder.glb")


def build_fern():
    reset_scene()
    rng = random.Random(23)
    frond_dark = solid_material("FernDark", (0.020, 0.055, 0.018, 1.0))
    frond_lit = solid_material("FernLit", (0.034, 0.088, 0.026, 1.0))
    verts, faces, sides = [], [], []
    for frond in range(17):
        azimuth = frond * GOLDEN_ANGLE + rng.uniform(-0.2, 0.2)
        direction = Vector((math.cos(azimuth), math.sin(azimuth), 0))
        length = rng.uniform(0.55, 0.95)
        width = length * 0.16
        steps = 10
        previous = [Vector((0, 0, 0.03)), Vector((0, 0, 0.03))]
        for step in range(1, steps + 1):
            t = step / steps
            arch = math.sin(t * 2.4) * 0.42 * length          # rises then droops
            center = direction * (length * t) + Vector((0, 0, arch))
            half = direction.cross(Vector((0, 0, 1))).normalized() * width * (1.0 - t * 0.85) * 0.5
            current = [center - half, center + half]
            base_index = len(verts)
            verts.extend([previous[0], previous[1], current[1], current[0]])
            faces.append((base_index, base_index + 1, base_index + 2, base_index + 3))
            sides.append(frond % 2 == 0)
            if step < steps:
                side_axis = direction.cross(Vector((0, 0, 1))).normalized()
                leaflet_length = length * 0.24 * (1.0 - t * 0.72)
                for sign in (-1.0, 1.0):
                    leaflet_direction = (direction * 0.22 + side_axis * sign + Vector((0, 0, -0.08 * t))).normalized()
                    leaflet_tip = center + leaflet_direction * leaflet_length
                    leaflet_half = direction * leaflet_length * 0.09
                    leaf_index = len(verts)
                    verts.extend([center - leaflet_half, center + leaflet_half, leaflet_tip])
                    faces.append((leaf_index, leaf_index + 1, leaf_index + 2)); sides.append((frond + step) % 2 == 0)
            previous = current
    mesh = bpy.data.meshes.new("Fern")
    mesh.from_pydata(verts, [], faces)
    mesh.update()
    fern = bpy.data.objects.new("Fern", mesh)
    bpy.context.collection.objects.link(fern)
    fern.data.materials.append(frond_dark)
    fern.data.materials.append(frond_lit)
    for polygon, use_lit in zip(fern.data.polygons, sides):
        polygon.material_index = 1 if use_lit else 0
    export_glb("detail_fern.glb")


def build_log():
    reset_scene()
    bark = textured_material("LogBark", "pine_bark_diff.jpg", roughness=0.95)
    p0, p1 = Vector((-2.1, 0, 0.24)), Vector((2.1, 0.3, 0.30))
    log = add_cone_between(p0, p1, 0.30, 0.22, sides=32)
    log.data.materials.append(bark)
    texture = bpy.data.textures.new("log-noise", type="CLOUDS")
    texture.noise_scale = 0.5
    modifier = log.modifiers.new("Displace", "DISPLACE")
    modifier.texture = texture
    modifier.strength = 0.08
    cut_wood = solid_material("CutHeartwood", (0.30, 0.17, 0.075, 1.0), roughness=0.90)
    axis = (p1 - p0).normalized()
    for point, radius, sign in ((p0, 0.303, -1.0), (p1, 0.223, 1.0)):
        cut = add_cone_between(point, point + axis * 0.018 * sign, radius, radius * 0.98, sides=32)
        cut.name = "ExposedTreeRings"; cut.data.materials.append(cut_wood)
    export_glb("detail_log.glb")


def build_rock_variant(filename, seed, outcrop=False):
    reset_scene(); rng = random.Random(seed)
    stone = textured_material("WeatheredRockPhotoscan", "mossy_rock_diff.jpg", roughness=0.95)
    count = 3 if outcrop else 1
    for index in range(count):
        radius = rng.uniform(0.78, 1.28) if outcrop else 1.45
        angle = index / max(count, 1) * math.tau + rng.uniform(-0.35, 0.35)
        radial = rng.uniform(0.15, 0.72) if outcrop else 0.0
        vertical_scale = rng.uniform(0.78, 1.18) if outcrop else 0.48
        location = (math.cos(angle) * radial, math.sin(angle) * radial * 0.72,
                    radius * vertical_scale * 0.43) if outcrop else (0, 0, radius * vertical_scale * 0.43)
        rock = add_eroded_stone(f"WeatheredOutcrop{index}" if outcrop else "WeatheredStoneSlab",
            location, radius, stone, rng,
            scale=(rng.uniform(0.82, 1.28) if outcrop else 1.42,
                   rng.uniform(0.74, 1.18) if outcrop else 1.08,
                   vertical_scale))
        # Vertices already contain their authored cluster offset. Pitching the
        # object around the GLB origin lifts one side visibly above terrain;
        # grounded yaw retains variation without violating contact.
        rock.rotation_euler = (0.0, 0.0, rng.random() * math.tau)
    export_glb(filename)


def build_stump():
    reset_scene(); rng = random.Random(373)
    bark = textured_material("StumpBark", "pine_bark_diff.jpg", roughness=0.96)
    heartwood = solid_material("StumpHeartwood", (0.28, 0.15, 0.055, 1.0), roughness=0.93)
    base, top = Vector((0, 0, 0.02)), Vector((0.05, -0.03, 0.88))
    trunk = add_cone_between(base, top, 0.47, 0.35, sides=40); trunk.name = "HighResolutionStump"; trunk.data.materials.append(bark)
    # Thin cap carries a distinct cut material and catches raking light.
    cap = add_cone_between(top, top + Vector((0, 0, 0.025)), 0.355, 0.34, sides=40); cap.name = "VisibleTreeRings"; cap.data.materials.append(heartwood)
    for root_index in range(11):
        angle = root_index / 11 * math.tau + rng.uniform(-0.13, 0.13)
        p0 = Vector((math.cos(angle) * 0.28, math.sin(angle) * 0.28, 0.17))
        p1 = Vector((math.cos(angle) * rng.uniform(0.72, 1.15), math.sin(angle) * rng.uniform(0.72, 1.15), 0.035))
        root = add_cone_between(p0, p1, rng.uniform(0.11, 0.17), rng.uniform(0.018, 0.045), sides=14)
        root.name = f"ExposedRoot{root_index}"; root.data.materials.append(bark)
    export_glb("detail_stump.glb")


def build_branch_pile():
    reset_scene(); rng = random.Random(397)
    bark = textured_material("BranchPileBark", "pine_bark_diff.jpg", roughness=0.97)
    for branch_index in range(15):
        angle = rng.random() * math.tau
        center = Vector((rng.uniform(-0.55, 0.55), rng.uniform(-0.45, 0.45), rng.uniform(0.08, 0.40)))
        direction = Vector((math.cos(angle), math.sin(angle), rng.uniform(-0.16, 0.22))).normalized()
        half = rng.uniform(0.45, 1.25)
        branch = add_cone_between(center - direction * half, center + direction * half, rng.uniform(0.035, 0.085), rng.uniform(0.012, 0.04), sides=12)
        branch.name = f"FallenBranch{branch_index}"; branch.data.materials.append(bark)
        for fork_index in range(2):
            origin = center + direction * rng.uniform(-half * 0.5, half * 0.65)
            side = direction.cross(Vector((0, 0, 1))).normalized() * (-1 if fork_index else 1)
            tip = origin + (direction * 0.25 + side + Vector((0, 0, rng.uniform(0.1, 0.4)))).normalized() * rng.uniform(0.22, 0.62)
            twig = add_cone_between(origin, tip, rng.uniform(0.018, 0.035), 0.004, sides=8); twig.data.materials.append(bark)
    export_glb("detail_branch_pile.glb")


def add_leaf_blade(verts, faces, center, direction, length, width):
    """Serrated, cupped leaf with a raised midrib and nine silhouette stations."""
    direction = direction.normalized()
    side_axis = direction.cross(Vector((0, 0, 1)))
    if side_axis.length < 0.01: side_axis = direction.cross(Vector((0, 1, 0)))
    side_axis.normalize()
    i = len(verts)
    stations = ((0.0, 0.10), (0.13, 0.48), (0.25, 0.74), (0.38, 0.91),
                (0.52, 1.0), (0.65, 0.84), (0.77, 0.66), (0.89, 0.37), (1.0, 0.015))
    for station_index, (station, profile) in enumerate(stations):
        serration = 1.0 - (0.10 if station_index % 2 else 0.0)
        middle = center + direction * (length * station) + Vector((0, 0, math.sin(station * math.pi) * length * 0.16))
        side = side_axis * width * profile * serration
        ridge = middle + Vector((0, 0, width * 0.10 * math.sin(station * math.pi)))
        verts.extend([middle - side, ridge, middle + side])
    for station in range(len(stations) - 1):
        a = i + station * 3; b = a + 3
        faces.extend([(a, a + 1, b + 1, b), (a + 1, a + 2, b + 2, b + 1)])


def build_wildflower():
    """A varied meadow clump: stems, leaves, and modest moonlit flower heads."""
    reset_scene()
    rng = random.Random(47)
    stem = solid_material("FlowerStems", (0.028, 0.105, 0.026, 1.0))
    leaf = solid_material("FlowerLeaves", (0.036, 0.135, 0.038, 1.0))
    petal = solid_material("MoonflowerPetals", (0.48, 0.29, 0.58, 1.0), roughness=0.72)
    pollen = solid_material("MoonflowerPollen", (0.78, 0.50, 0.12, 1.0), roughness=0.58)
    stems, leaves = [], []
    leaf_verts, leaf_faces, petal_verts, petal_faces = [], [], [], []
    for n in range(15):
        a, r = rng.random() * math.tau, rng.uniform(0.04, 0.72)
        root = Vector((math.cos(a) * r, math.sin(a) * r, 0))
        h = rng.uniform(0.30, 0.82)
        top = root + Vector((rng.uniform(-0.10, 0.10), rng.uniform(-0.10, 0.10), h))
        stalk = add_cone_between(root, top, 0.012, 0.006, sides=8); stalk.data.materials.append(stem); stems.append(stalk)
        for t in (0.26, 0.52):
            add_leaf_blade(leaf_verts, leaf_faces, root.lerp(top, t), Vector((math.cos(a + math.pi * t * 3), math.sin(a + math.pi * t * 3), 0)), rng.uniform(0.10, 0.20), 0.035)
        if n % 2 == 0:
            for p in range(5):
                angle = p * math.tau / 5.0
                add_leaf_blade(petal_verts, petal_faces, top, Vector((math.cos(angle), math.sin(angle), 0.10)).normalized(), 0.13, 0.052)
            pollen_center = top + Vector((0, 0, 0.014))
            for grain in range(13):
                ga = grain * GOLDEN_ANGLE; gr = 0.010 * math.sqrt(grain)
                add_seed_grain(f"PollenGrain{n}_{grain}", pollen_center + Vector((math.cos(ga) * gr, math.sin(ga) * gr, 0.004 * (grain % 3))), 0.011, pollen)
    mesh = bpy.data.meshes.new("WildflowerLeaves"); mesh.from_pydata(leaf_verts, [], leaf_faces); mesh.materials.append(leaf)
    obj = bpy.data.objects.new("WildflowerLeaves", mesh); bpy.context.collection.objects.link(obj)
    petal_mesh = bpy.data.meshes.new("IndividualFlowerPetals"); petal_mesh.from_pydata(petal_verts, [], petal_faces); petal_mesh.materials.append(petal)
    for polygon in petal_mesh.polygons: polygon.use_smooth = True
    petal_object = bpy.data.objects.new("IndividualFlowerPetals", petal_mesh); bpy.context.collection.objects.link(petal_object)
    export_glb("detail_wildflower.glb")


def build_heather():
    reset_scene()
    rng = random.Random(61)
    twig = solid_material("HeatherTwig", (0.10, 0.045, 0.026, 1.0))
    foliage = solid_material("HeatherFoliage", (0.055, 0.105, 0.047, 1.0))
    bloom = solid_material("HeatherBloom", (0.38, 0.11, 0.38, 1.0), roughness=0.8)
    leaf_verts, leaf_faces = [], []
    for n in range(28):
        a, r = rng.random() * math.tau, rng.uniform(0.02, 0.72)
        root = Vector((math.cos(a) * r, math.sin(a) * r, 0))
        top = root + Vector((math.cos(a) * rng.uniform(0.08, 0.25), math.sin(a) * rng.uniform(0.08, 0.25), rng.uniform(0.18, 0.50)))
        branch = add_cone_between(root, top, 0.018, 0.005, sides=8); branch.data.materials.append(twig)
        for b in range(3):
            q = root.lerp(top, 0.42 + b * 0.20)
            for leaf_index in range(4):
                leaf_angle = a + leaf_index * GOLDEN_ANGLE
                add_leaf_blade(leaf_verts, leaf_faces, q, Vector((math.cos(leaf_angle), math.sin(leaf_angle), rng.uniform(0.0, 0.25))).normalized(),
                               rng.uniform(0.055, 0.105), rng.uniform(0.014, 0.028))
        add_bell_bloom(f"HeatherBell{n}", top - Vector((0, 0, 0.025)), 0.050, 0.105, bloom, lobes=4)
    leaf_mesh = bpy.data.meshes.new("IndividualHeatherLeaves"); leaf_mesh.from_pydata(leaf_verts, [], leaf_faces); leaf_mesh.materials.append(foliage)
    for polygon in leaf_mesh.polygons: polygon.use_smooth = True
    leaf_object = bpy.data.objects.new("IndividualHeatherLeaves", leaf_mesh); bpy.context.collection.objects.link(leaf_object)
    export_glb("detail_heather.glb")


def build_mushrooms():
    reset_scene()
    rng = random.Random(79)
    stem = solid_material("MushroomStem", (0.43, 0.34, 0.22, 1.0))
    cap = solid_material("MushroomCap", (0.26, 0.065, 0.026, 1.0), roughness=0.64)
    gills = solid_material("MushroomGills", (0.54, 0.39, 0.24, 1.0), roughness=0.88)
    for n in range(11):
        a, r = rng.random() * math.tau, rng.uniform(0.05, 0.62)
        h, rad = rng.uniform(0.08, 0.26), rng.uniform(0.07, 0.16)
        loc = (math.cos(a) * r, math.sin(a) * r, h * 0.5)
        stalk = add_profiled_form(f"TaperedMushroomStem{n}", (loc[0], loc[1], 0),
            [(0.0, rad * 0.28), (h * 0.12, rad * 0.36), (h * 0.58, rad * 0.25), (h, rad * 0.32)],
            20, stem, radial_noise=0.04, phase=a)
        add_mushroom_cap(f"GilledMushroomCap{n}", (loc[0], loc[1], h), rad, cap, gills, rng)
    export_glb("detail_mushrooms.glb")


def build_reeds():
    reset_scene()
    rng = random.Random(97)
    reed = solid_material("ReedStem", (0.20, 0.25, 0.09, 1.0))
    seed = solid_material("ReedSeedhead", (0.16, 0.075, 0.025, 1.0))
    for n in range(22):
        a, r = rng.random() * math.tau, rng.uniform(0.05, 0.85)
        root = Vector((math.cos(a) * r, math.sin(a) * r, 0))
        h = rng.uniform(0.75, 1.55)
        top = root + Vector((rng.uniform(-0.14, 0.14), rng.uniform(-0.14, 0.14), h))
        stalk = add_cone_between(root, top, 0.018, 0.010, sides=8); stalk.data.materials.append(reed)
        if n % 3 != 0:
            seed_center = top - Vector((0, 0, 0.13))
            add_profiled_form(f"DenseReedSeedHead{n}", seed_center,
                [(-0.18, 0.010), (-0.15, 0.044), (-0.08, 0.055), (0.08, 0.052), (0.15, 0.038), (0.18, 0.006)],
                32, seed, radial_noise=0.075, phase=a)
            # Fine awns break up the silhouette without turning the head into floating beads.
            for awn in range(18):
                ga = awn * GOLDEN_ANGLE; z = (awn / 17.0 - 0.5) * 0.27
                p0 = seed_center + Vector((math.cos(ga) * 0.048, math.sin(ga) * 0.048, z))
                p1 = p0 + Vector((math.cos(ga) * 0.035, math.sin(ga) * 0.035, rng.uniform(-0.006, 0.012)))
                bristle = add_cone_between(p0, p1, 0.0022, 0.0003, sides=5); bristle.data.materials.append(seed)
    export_glb("detail_reeds.glb")


def build_shrub():
    reset_scene()
    rng = random.Random(113)
    wood = solid_material("ShrubWood", (0.075, 0.038, 0.020, 1.0))
    leaf = solid_material("ShrubLeaves", (0.028, 0.098, 0.025, 1.0))
    berry = solid_material("ShrubBerries", (0.16, 0.018, 0.025, 1.0), roughness=0.45)
    leaf_verts, leaf_faces = [], []
    for n in range(21):
        a, r = rng.random() * math.tau, rng.uniform(0.02, 0.65)
        root = Vector((math.cos(a) * r, math.sin(a) * r, 0))
        top = root + Vector((math.cos(a) * rng.uniform(0.22, 0.62), math.sin(a) * rng.uniform(0.22, 0.62), rng.uniform(0.35, 0.82)))
        branch = add_cone_between(root, top, 0.025, 0.006, sides=8); branch.data.materials.append(wood)
        for b in range(3):
            q = root.lerp(top, 0.38 + b * 0.22)
            for leaf_index in range(5):
                leaf_angle = a + leaf_index * GOLDEN_ANGLE + rng.uniform(-0.2, 0.2)
                leaf_direction = Vector((math.cos(leaf_angle), math.sin(leaf_angle), rng.uniform(-0.08, 0.28))).normalized()
                add_leaf_blade(leaf_verts, leaf_faces, q + Vector((rng.uniform(-0.08, 0.08), rng.uniform(-0.08, 0.08), 0.02)),
                               leaf_direction, rng.uniform(0.12, 0.22), rng.uniform(0.035, 0.065))
        if n % 4 == 0:
            fruit = add_profiled_form(f"DimpledBerry{n}", top,
                [(-0.042, 0.008), (-0.031, 0.031), (0.0, 0.043), (0.029, 0.034), (0.040, 0.009)],
                24, berry, radial_noise=0.025, phase=a)
            for sepal in range(5):
                sa = sepal / 5 * math.tau
                calyx = add_cone_between(top + Vector((0, 0, 0.034)), top + Vector((math.cos(sa) * 0.028, math.sin(sa) * 0.028, 0.052)), 0.006, 0.001, sides=5)
                calyx.name = f"BerryCalyx{n}_{sepal}"; calyx.data.materials.append(leaf)
    leaf_mesh = bpy.data.meshes.new("IndividualShrubLeaves"); leaf_mesh.from_pydata(leaf_verts, [], leaf_faces); leaf_mesh.materials.append(leaf)
    for polygon in leaf_mesh.polygons: polygon.use_smooth = True
    leaf_object = bpy.data.objects.new("IndividualShrubLeaves", leaf_mesh); bpy.context.collection.objects.link(leaf_object)
    export_glb("detail_shrub.glb")


def build_meadow_grass():
    """Broad-leafed grass tussock, distinct from the fine wind-swayed ground cover."""
    reset_scene()
    rng = random.Random(131)
    dark = solid_material("MeadowGrassDark", (0.035, 0.090, 0.026, 1.0))
    lit = solid_material("MeadowGrassLit", (0.075, 0.155, 0.042, 1.0))
    verts, faces = [], []
    for blade in range(45):
        a = blade * GOLDEN_ANGLE + rng.uniform(-0.16, 0.16)
        root = Vector((math.cos(a) * rng.uniform(0.00, 0.20), math.sin(a) * rng.uniform(0.00, 0.20), 0))
        direction = Vector((math.cos(a), math.sin(a), 0))
        length, width = rng.uniform(0.45, 0.92), rng.uniform(0.015, 0.032)
        side = direction.cross(Vector((0, 0, 1))).normalized() * width
        i = len(verts)
        lean = rng.uniform(0.44, 0.72)
        for station, profile in ((0.0, 1.0), (0.34, 0.82), (0.70, 0.48), (1.0, 0.06)):
            middle = root + direction * (length * lean * station) + Vector((0, 0, length * station))
            verts.extend([middle - side * profile, middle + side * profile])
        faces.extend([(i, i + 1, i + 3, i + 2), (i + 2, i + 3, i + 5, i + 4), (i + 4, i + 5, i + 7, i + 6)])
    mesh = bpy.data.meshes.new("MeadowGrass"); mesh.from_pydata(verts, [], faces); mesh.materials.append(dark); mesh.materials.append(lit)
    for i, poly in enumerate(mesh.polygons): poly.material_index = i % 3 == 0
    obj = bpy.data.objects.new("MeadowGrass", mesh); bpy.context.collection.objects.link(obj)
    export_glb("detail_meadow_grass.glb")


def build_birch(filename, seed, branch_rows, leaves_per_twig):
    """Open-crowned silver birch with forked limbs and individual leaves."""
    reset_scene()
    ensure_birch_bark_texture()
    rng = random.Random(seed)
    high_detail = leaves_per_twig > 10
    bark = textured_material("SilverBirchBark", "silver_birch_bark_albedo.png", roughness=0.92)
    twig_material = solid_material("BirchTwigs", (0.075, 0.045, 0.025, 1.0), roughness=0.95)
    leaf_dark = solid_material("BirchLeavesDark", (0.012, 0.050, 0.014, 1.0), roughness=0.84)
    leaf_lit = solid_material("BirchLeavesLit", (0.032, 0.105, 0.026, 1.0), roughness=0.80)
    leaf_old = solid_material("BirchLeavesOld", (0.12, 0.070, 0.018, 1.0), roughness=0.86)

    height = 9.6
    trunk_points = [Vector((0, 0, 0))]
    drift = Vector((0, 0, 0))
    for i in range(1, 9):
        drift += Vector((rng.uniform(-0.07, 0.07), rng.uniform(-0.07, 0.07), 0))
        trunk_points.append(Vector((drift.x, drift.y, height * i / 8)))
    for i in range(8):
        t = i / 8
        trunk = add_cone_between(trunk_points[i], trunk_points[i + 1], 0.25 * (1.0 - t) + 0.035, 0.25 * (1.0 - (i + 1) / 8) + 0.035, sides=24 if high_detail else 10)
        trunk.name = f"BirchTrunk{i}"; trunk.data.materials.append(bark)

    leaf_verts, leaf_faces, leaf_materials = [], [], []

    def add_leaf(center, branch_direction, material_index):
        arbitrary = Vector((0, 0, 1)) if abs(branch_direction.z) < 0.88 else Vector((1, 0, 0))
        side = branch_direction.cross(arbitrary).normalized()
        up = branch_direction.cross(side).normalized()
        side = (side * rng.uniform(0.5, 1.0) + up * rng.uniform(-0.6, 0.6)).normalized()
        length = rng.uniform(0.18, 0.31); width = length * rng.uniform(0.48, 0.70)
        tip_direction = (branch_direction * 0.45 + up * rng.uniform(-0.35, 0.35) + Vector((0, 0, 0.12))).normalized()
        root = center - tip_direction * length * 0.45; tip = center + tip_direction * length * 0.55
        i = len(leaf_verts)
        if high_detail:
            # Five cross-sections make a cupped, tapered lamina rather than a
            # flat diamond card. The center vein bows subtly toward the light.
            stations = ((0.0, 0.0), (0.24, 0.72), (0.52, 1.0), (0.78, 0.62), (1.0, 0.0))
            vein = (tip - root).normalized()
            cup = vein.cross(side).normalized()
            for station, profile in stations:
                middle = root.lerp(tip, station) + cup * math.sin(station * math.pi) * length * 0.08
                leaf_verts.extend([middle - side * width * profile, middle + side * width * profile])
            for station in range(len(stations) - 1):
                a = i + station * 2; b = a + 1; c = a + 3; d = a + 2
                leaf_faces.append((a, b, c, d)); leaf_materials.append(material_index)
        else:
            leaf_verts.extend([root, center + side * width, tip, center - side * width])
            leaf_faces.extend([(i, i + 1, i + 2), (i, i + 2, i + 3)])
            leaf_materials.extend([material_index, material_index])

    for row in range(branch_rows):
        t = 0.30 + 0.64 * row / max(branch_rows - 1, 1)
        segment = min(int(t * 8), 7); local = t * 8 - segment
        origin = trunk_points[segment].lerp(trunk_points[segment + 1], local)
        azimuth = row * GOLDEN_ANGLE + rng.uniform(-0.28, 0.28)
        primary_length = (1.0 - t) * 2.1 + rng.uniform(0.95, 1.55)
        primary_direction = Vector((math.cos(azimuth), math.sin(azimuth), rng.uniform(0.10, 0.42))).normalized()
        primary_tip = origin + primary_direction * primary_length
        limb = add_cone_between(origin, primary_tip, 0.055 * (1.0 - t * 0.45), 0.010, sides=14 if high_detail else 6)
        limb.name = f"BirchLimb{row}"; limb.data.materials.append(twig_material)
        limb_side = primary_direction.cross(Vector((0, 0, 1))).normalized()
        for fork_index, station in enumerate((0.48, 0.76, 0.96)):
            fork_root = origin.lerp(primary_tip, station)
            fork_direction = (primary_direction * 0.55 + limb_side * (-0.75 if fork_index % 2 else 0.75) + Vector((0, 0, rng.uniform(0.22, 0.65)))).normalized()
            fork_tip = fork_root + fork_direction * rng.uniform(0.55, 0.95)
            twig = add_cone_between(fork_root, fork_tip, 0.022, 0.004, sides=10 if high_detail else 5)
            twig.name = f"BirchTwig{row}_{fork_index}"; twig.data.materials.append(twig_material)
            for leaf_index in range(leaves_per_twig):
                q = fork_root.lerp(fork_tip, rng.uniform(0.28, 1.0))
                q += limb_side * rng.uniform(-0.16, 0.16) + Vector((0, 0, rng.uniform(-0.08, 0.12)))
                color_pick = rng.random(); material_index = 2 if color_pick < 0.055 else 1 if color_pick < 0.48 else 0
                add_leaf(q, fork_direction, material_index)

    mesh = bpy.data.meshes.new("IndividualBirchLeaves")
    mesh.from_pydata(leaf_verts, [], leaf_faces); mesh.materials.append(leaf_dark); mesh.materials.append(leaf_lit); mesh.materials.append(leaf_old)
    for polygon, material_index in zip(mesh.polygons, leaf_materials): polygon.material_index = material_index; polygon.use_smooth = True
    leaves = bpy.data.objects.new("IndividualBirchLeaves", mesh); bpy.context.collection.objects.link(leaves)
    export_glb(filename)


def build_clover():
    reset_scene(); rng = random.Random(211)
    stem_material = solid_material("CloverStems", (0.018, 0.085, 0.020, 1.0), roughness=0.90)
    leaf_material = solid_material("CloverLeaves", (0.020, 0.125, 0.025, 1.0), roughness=0.86)
    bloom_material = solid_material("CloverBloom", (0.68, 0.64, 0.58, 1.0), roughness=0.82)
    verts, faces = [], []
    for plant in range(34):
        angle, radius = rng.random() * math.tau, rng.uniform(0.02, 0.72)
        root = Vector((math.cos(angle) * radius, math.sin(angle) * radius, 0))
        top = root + Vector((rng.uniform(-0.08, 0.08), rng.uniform(-0.08, 0.08), rng.uniform(0.10, 0.28)))
        stem = add_cone_between(root, top, 0.007, 0.003, sides=8); stem.data.materials.append(stem_material)
        for leaflet in range(3):
            a = angle + leaflet * math.tau / 3.0 + rng.uniform(-0.12, 0.12)
            add_leaf_blade(verts, faces, top, Vector((math.cos(a), math.sin(a), 0.05)).normalized(), rng.uniform(0.075, 0.125), rng.uniform(0.035, 0.055))
        if plant % 7 == 0:
            bloom_center = top + Vector((0, 0, rng.uniform(0.04, 0.10)))
            for petal in range(26):
                a = petal * GOLDEN_ANGLE; t = petal / 25.0
                radial = 0.050 * math.sin(t * math.pi) + 0.008
                center = bloom_center + Vector((math.cos(a) * radial, math.sin(a) * radial, (t - 0.42) * 0.085))
                add_bell_bloom(f"CloverFloret{plant}_{petal}", center, 0.017, 0.041, bloom_material, lobes=5)
    mesh = bpy.data.meshes.new("CurvedCloverLeaves"); mesh.from_pydata(verts, [], faces); mesh.materials.append(leaf_material)
    for polygon in mesh.polygons: polygon.use_smooth = True
    obj = bpy.data.objects.new("CurvedCloverLeaves", mesh); bpy.context.collection.objects.link(obj)
    export_glb("detail_clover.glb")


def build_sedge(filename, dry=False):
    reset_scene(); rng = random.Random(241 if dry else 223)
    dark_color = (0.15, 0.095, 0.032, 1.0) if dry else (0.025, 0.095, 0.028, 1.0)
    lit_color = (0.28, 0.19, 0.065, 1.0) if dry else (0.055, 0.17, 0.045, 1.0)
    dark = solid_material("DryGrassDark" if dry else "WetSedgeDark", dark_color, roughness=0.92)
    lit = solid_material("DryGrassLit" if dry else "WetSedgeLit", lit_color, roughness=0.90)
    seed = solid_material("SeedPanicles", (0.23, 0.14, 0.045, 1.0) if dry else (0.10, 0.14, 0.045, 1.0), roughness=0.94)
    verts, faces, materials = [], [], []
    for blade in range(54):
        angle = blade * GOLDEN_ANGLE + rng.uniform(-0.18, 0.18); radial = rng.uniform(0.0, 0.48)
        root = Vector((math.cos(angle) * radial, math.sin(angle) * radial, 0)); direction = Vector((math.cos(angle), math.sin(angle), 0))
        length = rng.uniform(0.42, 1.05 if dry else 1.28); width = rng.uniform(0.009, 0.025)
        side = direction.cross(Vector((0, 0, 1))).normalized(); i = len(verts)
        stations = ((0.0, 1.0), (0.24, 0.92), (0.52, 0.68), (0.78, 0.34), (1.0, 0.04))
        for station, profile in stations:
            bend = direction * (length * station * station * rng.uniform(0.20, 0.52))
            middle = root + bend + Vector((0, 0, length * station))
            verts.extend([middle - side * width * profile, middle + side * width * profile])
        material_index = 1 if blade % 3 == 0 else 0
        for station in range(4):
            a = i + station * 2; faces.append((a, a + 1, a + 3, a + 2)); materials.append(material_index)
        if blade % (4 if dry else 7) == 0:
            tip = Vector(verts[i + 8] + verts[i + 9]) * 0.5
            for grain in range(7):
                ga = grain * GOLDEN_ANGLE
                grain_center = tip + Vector((math.cos(ga) * 0.045, math.sin(ga) * 0.045, (grain - 3) * 0.025))
                add_seed_grain(f"GrassSeed{blade}_{grain}", grain_center, 0.018, seed,
                               Vector((math.cos(ga) * 0.3, math.sin(ga) * 0.3, 1.0)).normalized())
    mesh = bpy.data.meshes.new("CurvedGrassBlades"); mesh.from_pydata(verts, [], faces); mesh.materials.append(dark); mesh.materials.append(lit)
    for polygon, material_index in zip(mesh.polygons, materials): polygon.material_index = material_index; polygon.use_smooth = True
    obj = bpy.data.objects.new("CurvedGrassBlades", mesh); bpy.context.collection.objects.link(obj)
    export_glb(filename)


def build_forest_litter():
    reset_scene(); rng = random.Random(263)
    leaf_material = solid_material("ForestLeafLitter", (0.16, 0.075, 0.020, 1.0), roughness=0.97)
    twig_material = solid_material("FineDeadTwigs", (0.075, 0.035, 0.012, 1.0), roughness=0.98)
    verts, faces = [], []
    for leaf in range(65):
        angle, radius = rng.random() * math.tau, rng.uniform(0.02, 0.85)
        center = Vector((math.cos(angle) * radius, math.sin(angle) * radius, rng.uniform(0.006, 0.025)))
        direction = Vector((math.cos(angle + rng.uniform(-1.2, 1.2)), math.sin(angle + rng.uniform(-1.2, 1.2)), 0)).normalized()
        add_leaf_blade(verts, faces, center, direction, rng.uniform(0.09, 0.22), rng.uniform(0.025, 0.065))
    mesh = bpy.data.meshes.new("CurledLeafLitter"); mesh.from_pydata(verts, [], faces); mesh.materials.append(leaf_material)
    for polygon in mesh.polygons: polygon.use_smooth = True
    obj = bpy.data.objects.new("CurledLeafLitter", mesh); bpy.context.collection.objects.link(obj)
    for twig in range(14):
        angle, radius = rng.random() * math.tau, rng.uniform(0.05, 0.75)
        p0 = Vector((math.cos(angle) * radius, math.sin(angle) * radius, 0.025))
        p1 = p0 + Vector((math.cos(angle + rng.uniform(-1.0, 1.0)), math.sin(angle + rng.uniform(-1.0, 1.0)), rng.uniform(-0.01, 0.04))) * rng.uniform(0.18, 0.55)
        branch = add_cone_between(p0, p1, rng.uniform(0.008, 0.018), rng.uniform(0.002, 0.006), sides=8); branch.data.materials.append(twig_material)
    export_glb("detail_forest_litter.glb")


def build_pebbles():
    reset_scene(); rng = random.Random(281)
    stone = textured_material("PebblePhotoscanStone", "mossy_rock_diff.jpg", roughness=0.94)
    for pebble in range(22):
        angle, radius = rng.random() * math.tau, rng.uniform(0.04, 0.90)
        size = rng.uniform(0.055, 0.19)
        obj = add_eroded_stone(f"WaterWornPebble{pebble}",
            (math.cos(angle) * radius, math.sin(angle) * radius, size * 0.34), size, stone, rng,
            scale=(rng.uniform(0.8, 1.5), rng.uniform(0.65, 1.25), rng.uniform(0.45, 0.78)))
        obj.rotation_euler = (rng.uniform(-0.35, 0.35), rng.uniform(-0.35, 0.35), rng.random() * math.tau)
    export_glb("detail_pebbles.glb")


def build_moss_mat():
    reset_scene(); rng = random.Random(307)
    dark = solid_material("MossMatDark", (0.012, 0.052, 0.012, 1.0), roughness=0.99)
    lit = solid_material("MossMatLit", (0.032, 0.105, 0.022, 1.0), roughness=0.98)
    filament = solid_material("MossSporophytes", (0.075, 0.14, 0.030, 1.0), roughness=0.96)
    for cushion in range(38):
        angle, radius = rng.random() * math.tau, rng.uniform(0.0, 0.78); size = rng.uniform(0.07, 0.22)
        center = Vector((math.cos(angle) * radius, math.sin(angle) * radius, 0.006))
        add_profiled_form(f"IrregularMossCushion{cushion}", center,
            [(0.0, size * 0.20), (size * 0.05, size * 0.84), (size * 0.15, size), (size * 0.28, size * 0.68), (size * 0.34, size * 0.06)],
            30, lit if cushion % 4 == 0 else dark,
            scale=(rng.uniform(1.2, 2.4), rng.uniform(1.2, 2.4), 1.0), radial_noise=0.12, phase=angle)
        if cushion % 3 == 0:
            for shoot in range(5):
                sa = shoot * GOLDEN_ANGLE
                p0 = center + Vector((math.cos(sa) * size * 0.42, math.sin(sa) * size * 0.42, size * 0.20))
                p1 = p0 + Vector((rng.uniform(-0.02, 0.02), rng.uniform(-0.02, 0.02), rng.uniform(0.08, 0.18)))
                sporophyte = add_cone_between(p0, p1, 0.004, 0.0015, sides=6); sporophyte.data.materials.append(filament)
                add_seed_grain(f"MossCapsule{cushion}_{shoot}", p1, 0.012, filament)
    export_glb("detail_moss_mat.glb")


def build_lupine():
    reset_scene(); rng = random.Random(331)
    stem = solid_material("LupineStems", (0.018, 0.082, 0.022, 1.0), roughness=0.90)
    leaf = solid_material("LupineLeaves", (0.025, 0.13, 0.032, 1.0), roughness=0.86)
    petal = solid_material("LupinePetals", (0.24, 0.12, 0.42, 1.0), roughness=0.76)
    verts, faces = [], []
    for plant in range(9):
        angle, radius = rng.random() * math.tau, rng.uniform(0.02, 0.58); root = Vector((math.cos(angle) * radius, math.sin(angle) * radius, 0))
        height = rng.uniform(0.52, 1.12); top = root + Vector((rng.uniform(-0.06, 0.06), rng.uniform(-0.06, 0.06), height))
        stalk = add_cone_between(root, top, 0.014, 0.005, sides=10); stalk.data.materials.append(stem)
        for whorl in (0.24, 0.43, 0.60):
            center = root.lerp(top, whorl)
            for leaflet in range(7):
                a = leaflet / 7 * math.tau + angle
                add_leaf_blade(verts, faces, center, Vector((math.cos(a), math.sin(a), 0.08)).normalized(), rng.uniform(0.10, 0.18), rng.uniform(0.022, 0.04))
        for blossom in range(20):
            t = 0.62 + blossom / 19 * 0.36; a = blossom * GOLDEN_ANGLE
            center = root.lerp(top, t) + Vector((math.cos(a) * (1.0 - t) * 0.22, math.sin(a) * (1.0 - t) * 0.22, 0))
            outward = Vector((math.cos(a), math.sin(a), 0.28)).normalized()
            floret = add_bell_bloom(f"LupineFloret{plant}_{blossom}", center, 0.046, 0.075, petal, lobes=5)
            floret.rotation_mode = "QUATERNION"; floret.rotation_quaternion = outward.to_track_quat("Z", "Y")
    mesh = bpy.data.meshes.new("LupinePalmateLeaves"); mesh.from_pydata(verts, [], faces); mesh.materials.append(leaf)
    for polygon in mesh.polygons: polygon.use_smooth = True
    obj = bpy.data.objects.new("LupinePalmateLeaves", mesh); bpy.context.collection.objects.link(obj)
    export_glb("detail_lupine.glb")


def build_spruce_sapling():
    """Young spruce with a readable leader, branch whorls and discrete needles."""
    reset_scene(); rng = random.Random(2081)
    bark = solid_material("SaplingBark", (0.075, 0.041, 0.022, 1.0), roughness=0.96)
    needles_dark = solid_material("SaplingNeedlesDark", (0.015, 0.060, 0.028, 1.0), roughness=0.90)
    needles_lit = solid_material("SaplingNeedlesLit", (0.030, 0.105, 0.045, 1.0), roughness=0.84)
    for plant in range(3):
        offset = Vector((rng.uniform(-0.58, 0.58), rng.uniform(-0.46, 0.46), 0.0))
        height = rng.uniform(1.20, 2.15)
        top = offset + Vector((rng.uniform(-0.08, 0.08), rng.uniform(-0.08, 0.08), height))
        trunk = add_cone_between(offset, top, 0.040, 0.007, sides=10)
        trunk.name = f"SaplingTrunk{plant}"; trunk.data.materials.append(bark)
        spray_verts, spray_faces, spray_sides = [], [], []
        for row in range(7):
            t = 0.18 + row * 0.105
            center = offset.lerp(top, t)
            radius = (1.0 - t) * rng.uniform(0.38, 0.60)
            arms = 3 + (row + plant) % 2
            rotation = row * GOLDEN_ANGLE * 0.45 + rng.uniform(-0.25, 0.25)
            for arm in range(arms):
                angle = rotation + arm * math.tau / arms + rng.uniform(-0.16, 0.16)
                direction = Vector((math.cos(angle), math.sin(angle), rng.uniform(-0.25, 0.04))).normalized()
                tip = center + direction * radius
                branch = add_cone_between(center, tip, 0.014, 0.0025, sides=6)
                branch.name = f"SaplingBranch{plant}_{row}_{arm}"; branch.data.materials.append(bark)
                for station in (0.32, 0.66, 0.92):
                    spray_cluster(rng, spray_verts, spray_faces, spray_sides, center.lerp(tip, station), direction,
                                  count=22, blade_length=0.10, spread=0.72, width_min=0.035, width_max=0.075)
        spray_cluster(rng, spray_verts, spray_faces, spray_sides, top, Vector((0, 0, 1)),
                      count=54, blade_length=0.11, spread=0.60, width_min=0.035, width_max=0.070)
        mesh = bpy.data.meshes.new(f"SaplingNeedleMesh{plant}"); mesh.from_pydata(spray_verts, [], spray_faces)
        mesh.materials.append(needles_dark); mesh.materials.append(needles_lit)
        for polygon, lit in zip(mesh.polygons, spray_sides): polygon.material_index = 1 if lit else 0
        obj = bpy.data.objects.new(f"IndividualSaplingNeedles{plant}", mesh); bpy.context.collection.objects.link(obj)
    export_glb("detail_spruce_sapling.glb")


def build_wood_sorrel():
    """Dense trifoliate woodland herb with occasional five-petal flowers."""
    reset_scene(); rng = random.Random(2099)
    stem = solid_material("WoodSorrelStems", (0.025, 0.095, 0.027, 1.0), roughness=0.92)
    leaf = solid_material("WoodSorrelLeaves", (0.030, 0.145, 0.042, 1.0), roughness=0.82)
    petal = solid_material("WoodSorrelPetals", (0.78, 0.75, 0.66, 1.0), roughness=0.76)
    leaf_verts, leaf_faces, petal_verts, petal_faces = [], [], [], []
    for plant in range(28):
        angle = plant * GOLDEN_ANGLE; radius = 0.08 * math.sqrt(plant)
        root = Vector((math.cos(angle) * radius, math.sin(angle) * radius, 0.0))
        top = root + Vector((rng.uniform(-0.05, 0.05), rng.uniform(-0.05, 0.05), rng.uniform(0.12, 0.30)))
        stalk = add_cone_between(root, top, 0.007, 0.0025, sides=6); stalk.name = f"SorrelStem{plant}"; stalk.data.materials.append(stem)
        for leaflet in range(3):
            a = angle + leaflet * math.tau / 3.0
            add_leaf_blade(leaf_verts, leaf_faces, top, Vector((math.cos(a), math.sin(a), 0.12)).normalized(),
                           rng.uniform(0.08, 0.14), rng.uniform(0.035, 0.058))
        if plant % 7 == 0:
            flower = top + Vector((0, 0, rng.uniform(0.05, 0.11)))
            for p in range(5):
                a = p * math.tau / 5.0
                add_leaf_blade(petal_verts, petal_faces, flower, Vector((math.cos(a), math.sin(a), 0.15)).normalized(), 0.055, 0.020)
    leaf_mesh = bpy.data.meshes.new("IndividualWoodSorrelLeaves"); leaf_mesh.from_pydata(leaf_verts, [], leaf_faces); leaf_mesh.materials.append(leaf)
    bpy.context.collection.objects.link(bpy.data.objects.new("IndividualWoodSorrelLeaves", leaf_mesh))
    petal_mesh = bpy.data.meshes.new("IndividualWoodSorrelPetals"); petal_mesh.from_pydata(petal_verts, [], petal_faces); petal_mesh.materials.append(petal)
    bpy.context.collection.objects.link(bpy.data.objects.new("IndividualWoodSorrelPetals", petal_mesh))
    export_glb("detail_wood_sorrel.glb")


def build_fireweed():
    """Tall meadow pioneer plant with lance leaves and irregular bloom spires."""
    reset_scene(); rng = random.Random(2111)
    stem = solid_material("FireweedStems", (0.055, 0.105, 0.035, 1.0), roughness=0.90)
    leaf = solid_material("FireweedLeaves", (0.035, 0.125, 0.036, 1.0), roughness=0.84)
    petal = solid_material("FireweedPetals", (0.50, 0.075, 0.23, 1.0), roughness=0.70)
    leaf_verts, leaf_faces = [], []
    for plant in range(12):
        a = plant * GOLDEN_ANGLE; r = rng.uniform(0.05, 0.62)
        root = Vector((math.cos(a) * r, math.sin(a) * r, 0.0)); h = rng.uniform(0.65, 1.38)
        top = root + Vector((rng.uniform(-0.09, 0.09), rng.uniform(-0.09, 0.09), h))
        stalk = add_cone_between(root, top, 0.013, 0.004, sides=8); stalk.name = f"FireweedStem{plant}"; stalk.data.materials.append(stem)
        for leaf_index in range(7):
            t = 0.16 + leaf_index * 0.09; la = a + leaf_index * GOLDEN_ANGLE
            add_leaf_blade(leaf_verts, leaf_faces, root.lerp(top, t), Vector((math.cos(la), math.sin(la), 0.04)).normalized(),
                           rng.uniform(0.15, 0.25), rng.uniform(0.025, 0.045))
        for bloom_index in range(7):
            t = 0.62 + bloom_index * 0.050
            if t > 0.97: break
            ba = a + bloom_index * GOLDEN_ANGLE
            center = root.lerp(top, t) + Vector((math.cos(ba) * 0.045, math.sin(ba) * 0.045, 0))
            add_bell_bloom(f"FireweedBloom{plant}_{bloom_index}", center, 0.040, 0.060, petal, lobes=4)
    mesh = bpy.data.meshes.new("IndividualFireweedLeaves"); mesh.from_pydata(leaf_verts, [], leaf_faces); mesh.materials.append(leaf)
    bpy.context.collection.objects.link(bpy.data.objects.new("IndividualFireweedLeaves", mesh))
    export_glb("detail_fireweed.glb")


def build_wood_anemone():
    """Low spring wildflower mats for bright pockets beneath deciduous trees."""
    reset_scene(); rng = random.Random(2129)
    stem = solid_material("AnemoneStems", (0.030, 0.100, 0.030, 1.0), roughness=0.91)
    leaf = solid_material("AnemoneLeaves", (0.025, 0.115, 0.030, 1.0), roughness=0.83)
    petal = solid_material("AnemonePetals", (0.88, 0.86, 0.79, 1.0), roughness=0.72)
    pollen = solid_material("AnemonePollen", (0.72, 0.50, 0.08, 1.0), roughness=0.60)
    leaf_verts, leaf_faces, petal_verts, petal_faces = [], [], [], []
    for plant in range(18):
        a = plant * GOLDEN_ANGLE; r = rng.uniform(0.04, 0.64)
        root = Vector((math.cos(a) * r, math.sin(a) * r, 0)); h = rng.uniform(0.18, 0.42)
        top = root + Vector((rng.uniform(-0.045, 0.045), rng.uniform(-0.045, 0.045), h))
        stalk = add_cone_between(root, top, 0.008, 0.003, sides=7); stalk.name = f"AnemoneStem{plant}"; stalk.data.materials.append(stem)
        for leaflet in range(3):
            la = a + leaflet * math.tau / 3.0
            add_leaf_blade(leaf_verts, leaf_faces, root.lerp(top, 0.44), Vector((math.cos(la), math.sin(la), 0.08)).normalized(), 0.11, 0.038)
        if plant % 2 == 0:
            for p in range(6):
                pa = p * math.tau / 6.0
                add_leaf_blade(petal_verts, petal_faces, top, Vector((math.cos(pa), math.sin(pa), 0.10)).normalized(), 0.075, 0.028)
            add_seed_grain(f"AnemoneCenter{plant}", top + Vector((0, 0, 0.012)), 0.018, pollen)
    leaf_mesh = bpy.data.meshes.new("IndividualAnemoneLeaves"); leaf_mesh.from_pydata(leaf_verts, [], leaf_faces); leaf_mesh.materials.append(leaf)
    bpy.context.collection.objects.link(bpy.data.objects.new("IndividualAnemoneLeaves", leaf_mesh))
    petal_mesh = bpy.data.meshes.new("IndividualAnemonePetals"); petal_mesh.from_pydata(petal_verts, [], petal_faces); petal_mesh.materials.append(petal)
    bpy.context.collection.objects.link(bpy.data.objects.new("IndividualAnemonePetals", petal_mesh))
    export_glb("detail_wood_anemone.glb")


def build_wayfinder():
    """Detailed grounded third-person mage with a readable human silhouette."""
    reset_scene()
    cloth = textured_material("WayfinderWool", "wayfinder_wool_albedo.png", roughness=0.82)
    leather = textured_material("WayfinderLeather", "wet_bark_albedo.png", roughness=0.68)
    face = solid_material("HoodShadow", (0.0003, 0.0005, 0.0008, 1.0), roughness=0.98)
    skin = solid_material("WeatheredHands", (0.31, 0.19, 0.13, 1.0), roughness=0.74)
    trim = solid_material("CloakTrim", (0.035, 0.055, 0.068, 1.0), roughness=0.78)
    metal = solid_material("StaffIron", (0.08, 0.10, 0.12, 1.0), roughness=0.38)
    rune = solid_material("StaffRune", (0.01, 0.68, 0.52, 1.0), roughness=0.22)

    def beveled_cube(name, location, scale, material, bevel=0.04):
        bpy.ops.mesh.primitive_cube_add(location=location)
        obj = bpy.context.active_object; obj.name = name; obj.scale = scale; obj.data.materials.append(material)
        modifier = obj.modifiers.new(f"{name} softened edges", "BEVEL"); modifier.width = bevel; modifier.segments = 3
        return obj

    # Separate boots and legs prevent the robe from reading as a single cone.
    for side in (-1, 1):
        beveled_cube(f"TravelBoot{side}", (side * 0.18, -0.10, 0.14), (0.13, 0.23, 0.14), leather, 0.045)
        leg = add_cone_between(Vector((side * 0.18, 0.02, 0.28)), Vector((side * 0.16, 0.02, 1.05)), 0.13, 0.12, sides=16)
        leg.name = f"TrouserLeg{side}"; leg.data.materials.append(trim)

    # Multi-ring skirt mesh with radial cloth folds and an irregular hem.
    segments = 48
    rings = [(0.22, 0.60), (0.55, 0.55), (0.95, 0.45), (1.28, 0.34), (1.56, 0.31)]
    verts, faces = [], []
    for ring_index, (z, radius) in enumerate(rings):
        for i in range(segments):
            angle = i / segments * math.tau
            fold = math.sin(angle * 7.0 + ring_index * 0.38) * (0.055 * (1.0 - ring_index / len(rings)))
            hem = math.sin(angle * 5.0 + 0.7) * 0.035 if ring_index == 0 else 0.0
            verts.append((math.cos(angle) * (radius + fold), math.sin(angle) * (radius * 0.72 + fold * 0.5), z + hem))
    for r in range(len(rings) - 1):
        for i in range(segments):
            n = (i + 1) % segments; a = r * segments + i; b = r * segments + n
            c = (r + 1) * segments + n; d = (r + 1) * segments + i
            faces.append((a, b, c, d))
    mesh = bpy.data.meshes.new("FoldedRobeMesh"); mesh.from_pydata(verts, [], faces); mesh.materials.append(cloth)
    robe_uv = mesh.uv_layers.new(name="RobeUV")
    for polygon in mesh.polygons:
        for loop_index in polygon.loop_indices:
            vertex = mesh.loops[loop_index].vertex_index
            robe_uv.data[loop_index].uv = ((vertex % segments) / segments, (vertex // segments) / (len(rings) - 1))
    robe = bpy.data.objects.new("FoldedTravelRobe", mesh); bpy.context.collection.objects.link(robe)
    for polygon in mesh.polygons: polygon.use_smooth = True

    # Anatomical torso beneath the garments, with belt and asymmetric gear.
    torso = add_profiled_form("AnatomicalWayfinderTorso", (0, 0.0, 1.62),
        [(-0.50, 0.20), (-0.38, 0.36), (-0.05, 0.42), (0.22, 0.49), (0.43, 0.35), (0.50, 0.12)],
        48, trim, scale=(0.82, 0.55, 1.04), radial_noise=0.018, phase=0.7)
    bpy.ops.mesh.primitive_torus_add(major_radius=0.34, minor_radius=0.045, major_segments=32, minor_segments=8, location=(0, 0, 1.38))
    belt = bpy.context.active_object; belt.name = "LeatherBelt"; belt.scale.y = 0.76; belt.data.materials.append(leather)
    beveled_cube("BeltBuckle", (0, -0.275, 1.38), (0.08, 0.035, 0.065), metal, 0.018)
    beveled_cube("PotionPouch", (-0.37, 0.02, 1.28), (0.13, 0.10, 0.18), leather, 0.045)

    # Draped back cloak: broad at the shoulders, split and folded at the hem.
    cloak_verts, cloak_faces = [], []
    across, down = 12, 9
    for row in range(down):
        t = row / (down - 1); z = 2.06 - t * 1.72
        half_width = 0.48 + t * 0.20
        for column in range(across):
            u = column / (across - 1) * 2.0 - 1.0
            x = u * half_width
            y = 0.20 + t * 0.12 + math.cos(u * math.pi * 3.0) * 0.045 + math.sin(t * math.pi) * 0.10
            z_fold = math.sin(u * math.pi * 4.0) * 0.035 * t
            cloak_verts.append((x, y, z + z_fold))
    for row in range(down - 1):
        for column in range(across - 1):
            a = row * across + column; b = a + 1; d = (row + 1) * across + column; c = d + 1
            cloak_faces.append((a, b, c, d))
    cloak_mesh = bpy.data.meshes.new("DrapedCloakMesh"); cloak_mesh.from_pydata(cloak_verts, [], cloak_faces); cloak_mesh.materials.append(cloth)
    cloak_uv = cloak_mesh.uv_layers.new(name="CloakUV")
    for polygon in cloak_mesh.polygons:
        for loop_index in polygon.loop_indices:
            vertex = cloak_mesh.loops[loop_index].vertex_index
            cloak_uv.data[loop_index].uv = ((vertex % across) / (across - 1), (vertex // across) / (down - 1))
    cloak = bpy.data.objects.new("DrapedWayfinderCloak", cloak_mesh); bpy.context.collection.objects.link(cloak)
    solidify = cloak.modifiers.new("Cloak thickness", "SOLIDIFY"); solidify.thickness = 0.025
    for polygon in cloak_mesh.polygons: polygon.use_smooth = True

    # Shoulder mantle and layered collar break the torso/hood transition.
    mantle = add_profiled_form("TailoredShoulderMantle", (0, 0.02, 1.95),
        [(-0.24, 0.30), (-0.15, 0.50), (0.0, 0.56), (0.13, 0.43), (0.23, 0.18)],
        56, cloth, scale=(1.08, 0.66, 0.43), radial_noise=0.035, phase=1.2)
    bpy.ops.mesh.primitive_torus_add(major_radius=0.29, minor_radius=0.055, major_segments=32, minor_segments=10, location=(0, -0.05, 2.03))
    collar = bpy.context.active_object; collar.name = "RaisedCollar"; collar.scale.y = 0.78; collar.data.materials.append(trim)

    # Articulated arms, forearms, leather bracers, and visible hands.
    arm_specs = [
        (Vector((-0.36, -0.01, 1.91)), Vector((-0.55, -0.20, 1.55)), Vector((-0.62, -0.31, 1.24))),
        (Vector((0.36, -0.01, 1.91)), Vector((0.55, -0.22, 1.67)), Vector((0.67, -0.34, 1.47))),
    ]
    for index, (shoulder, elbow, wrist) in enumerate(arm_specs):
        upper = add_cone_between(shoulder, elbow, 0.16, 0.13, sides=16); upper.name = f"UpperArm{index}"; upper.data.materials.append(cloth)
        fore = add_cone_between(elbow, wrist, 0.13, 0.09, sides=16); fore.name = f"Forearm{index}"; fore.data.materials.append(cloth)
        bracer = add_cone_between(elbow.lerp(wrist, 0.48), elbow.lerp(wrist, 0.88), 0.115, 0.095, sides=16); bracer.name = f"LeatherBracer{index}"; bracer.data.materials.append(leather)
        hand = add_profiled_form(f"SculptedHand{index}", wrist,
            [(-0.12, 0.035), (-0.09, 0.075), (0.0, 0.105), (0.07, 0.085), (0.12, 0.030)],
            28, skin, scale=(0.82, 0.72, 1.15), radial_noise=0.025, phase=index * 1.7)

    # Hood shell, thick oval rim, and deeply recessed face cavity.
    hood = add_profiled_form("DrapedWayfinderHood", (0, 0.02, 2.34),
        [(-0.48, 0.19), (-0.35, 0.36), (-0.05, 0.43), (0.25, 0.35), (0.43, 0.17), (0.50, 0.025)],
        56, cloth, scale=(0.93, 0.86, 1.12), radial_noise=0.028, phase=2.1)
    bpy.ops.mesh.primitive_torus_add(major_radius=0.215, minor_radius=0.035, major_segments=36, minor_segments=10,
                                    location=(0, -0.345, 2.33), rotation=(math.radians(90), 0, 0))
    hood_rim = bpy.context.active_object; hood_rim.name = "HoodOpeningRim"; hood_rim.scale = (0.86, 1.0, 1.18); hood_rim.data.materials.append(trim)
    hood_void = add_profiled_form("DeepHoodShadow", (0, -0.365, 2.31),
        [(-0.20, 0.035), (-0.14, 0.15), (0.0, 0.19), (0.14, 0.15), (0.20, 0.035)],
        40, face, scale=(0.82, 0.16, 1.06))
    add_crystal("CutVeilBrooch", (0, -0.43, 1.98), 0.052, 0.13, rune, facets=8)

    # Staff with leather grip, metal ferrule, cage, and magical focus.
    staff = add_cone_between(Vector((0.70, -0.38, 0.08)), Vector((0.70, -0.38, 2.92)), 0.055, 0.035, sides=14)
    staff.name = "RunewoodStaff"; staff.data.materials.append(leather)
    grip = add_cone_between(Vector((0.70, -0.38, 1.22)), Vector((0.70, -0.38, 1.72)), 0.073, 0.066, sides=16)
    grip.name = "StaffGrip"; grip.data.materials.append(trim)
    ferrule = add_cone_between(Vector((0.70, -0.38, 0.03)), Vector((0.70, -0.38, 0.28)), 0.072, 0.058, sides=14)
    ferrule.name = "IronFerrule"; ferrule.data.materials.append(metal)
    staff.data.materials.append(leather)
    bpy.ops.mesh.primitive_torus_add(major_radius=0.19, minor_radius=0.027, major_segments=32, minor_segments=10, location=(0.70, -0.38, 2.80), rotation=(math.radians(90), 0, 0))
    cage = bpy.context.active_object; cage.data.materials.append(metal)
    for angle in (0.0, math.pi / 2.0):
        bpy.ops.mesh.primitive_torus_add(major_radius=0.16, minor_radius=0.016, major_segments=24, minor_segments=8,
                                        location=(0.70, -0.38, 2.80), rotation=(math.radians(90), angle, 0))
        bpy.context.active_object.data.materials.append(metal)
    add_crystal("FracturedStaffFocus", (0.70, -0.38, 2.80), 0.13, 0.34, rune, facets=11)
    export_glb("detail_wayfinder.glb")


build_conifer("detail_pine.glb", seed=7, height=11.5, branch_rows=30, branch_length=2.6,
              needle_dark=(0.022, 0.060, 0.028, 1.0), needle_lit=(0.038, 0.095, 0.040, 1.0))
build_conifer("detail_spruce.glb", seed=19, height=8.0, branch_rows=26, branch_length=3.1,
              needle_dark=(0.020, 0.052, 0.032, 1.0), needle_lit=(0.033, 0.082, 0.048, 1.0))
# Far trees retain the authored silhouette but use fewer independently resolved
# branch clusters; they are selected only beyond readable needle distance.
build_conifer("detail_pine_lod.glb", seed=7, height=11.5, branch_rows=18, branch_length=2.6,
              needle_dark=(0.022, 0.060, 0.028, 1.0), needle_lit=(0.038, 0.095, 0.040, 1.0))
build_conifer("detail_spruce_lod.glb", seed=19, height=8.0, branch_rows=16, branch_length=3.1,
              needle_dark=(0.020, 0.052, 0.032, 1.0), needle_lit=(0.033, 0.082, 0.048, 1.0))
build_conifer("detail_pine_far.glb", seed=7, height=11.5, branch_rows=10, branch_length=2.6,
              needle_dark=(0.022, 0.060, 0.028, 1.0), needle_lit=(0.038, 0.095, 0.040, 1.0))
build_conifer("detail_spruce_far.glb", seed=19, height=8.0, branch_rows=9, branch_length=3.1,
              needle_dark=(0.020, 0.052, 0.032, 1.0), needle_lit=(0.033, 0.082, 0.048, 1.0))
build_conifer_impostor("detail_pine_impostor.glb", seed=707, height=11.5, crown_width=3.1, is_spruce=False)
build_conifer_impostor("detail_spruce_impostor.glb", seed=719, height=8.0, crown_width=3.5, is_spruce=True)
# Veteran variants have irregular surviving whorls, deeper lower-limb droop,
# crown holes and a more crooked trunk. Matching assets at every LOD preserve
# that individual tree's identity as the camera crosses distance bands.
build_conifer("detail_pine_old.glb", seed=1707, height=12.4, branch_rows=30, branch_length=2.9,
              needle_dark=(0.020, 0.052, 0.024, 1.0), needle_lit=(0.034, 0.079, 0.033, 1.0))
build_conifer("detail_spruce_old.glb", seed=1719, height=9.2, branch_rows=26, branch_length=3.35,
              needle_dark=(0.017, 0.046, 0.028, 1.0), needle_lit=(0.029, 0.073, 0.042, 1.0))
build_conifer("detail_pine_old_lod.glb", seed=1707, height=12.4, branch_rows=18, branch_length=2.9,
              needle_dark=(0.020, 0.052, 0.024, 1.0), needle_lit=(0.034, 0.079, 0.033, 1.0))
build_conifer("detail_spruce_old_lod.glb", seed=1719, height=9.2, branch_rows=16, branch_length=3.35,
              needle_dark=(0.017, 0.046, 0.028, 1.0), needle_lit=(0.029, 0.073, 0.042, 1.0))
build_conifer("detail_pine_old_far.glb", seed=1707, height=12.4, branch_rows=10, branch_length=2.9,
              needle_dark=(0.020, 0.052, 0.024, 1.0), needle_lit=(0.034, 0.079, 0.033, 1.0))
build_conifer("detail_spruce_old_far.glb", seed=1719, height=9.2, branch_rows=9, branch_length=3.35,
              needle_dark=(0.017, 0.046, 0.028, 1.0), needle_lit=(0.029, 0.073, 0.042, 1.0))
build_conifer_impostor("detail_pine_old_impostor.glb", seed=2707, height=12.4, crown_width=3.45, is_spruce=False)
build_conifer_impostor("detail_spruce_old_impostor.glb", seed=2719, height=9.2, crown_width=3.8, is_spruce=True)
build_birch("detail_birch.glb", seed=151, branch_rows=24, leaves_per_twig=18)
build_birch("detail_birch_lod.glb", seed=151, branch_rows=14, leaves_per_twig=7)
build_snag()
build_boulder()
build_fern()
build_log()
build_rock_variant("detail_rock_slab.glb", seed=359, outcrop=False)
build_rock_variant("detail_rock_outcrop.glb", seed=367, outcrop=True)
build_stump()
build_branch_pile()
build_wildflower()
build_heather()
build_mushrooms()
build_reeds()
build_shrub()
build_meadow_grass()
build_clover()
build_sedge("detail_sedge.glb", dry=False)
build_sedge("detail_dry_grass.glb", dry=True)
build_forest_litter()
build_pebbles()
build_moss_mat()
build_lupine()
build_spruce_sapling()
build_wood_sorrel()
build_fireweed()
build_wood_anemone()
build_wayfinder()
print("DETAIL ASSETS EXPORTED")

