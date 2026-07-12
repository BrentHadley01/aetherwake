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
    # Wandering tapered trunk built from stacked segments.
    high_detail = branch_rows > 20
    segments = 10 if high_detail else 6
    base = Vector((0, 0, 0))
    drift = Vector((0, 0, 0))
    points = [base.copy()]
    for i in range(1, segments + 1):
        drift += Vector((rng.uniform(-0.14, 0.14), rng.uniform(-0.14, 0.14), 0))
        points.append(Vector((drift.x, drift.y, height * i / segments)))
    for i in range(segments):
        t0, t1 = i / segments, (i + 1) / segments
        wood.append(add_cone_between(points[i], points[i + 1], 0.26 * (1 - t0) + 0.03, 0.26 * (1 - t1) + 0.03, sides=16 if high_detail else 8))

    spray_verts, spray_faces, spray_side = [], [], []
    branch_count = 0
    for row in range(branch_rows):
        t = 0.22 + 0.74 * row / (branch_rows - 1)
        trunk_point = points[min(int(t * segments), segments - 1)].lerp(points[min(int(t * segments) + 1, segments)], (t * segments) % 1.0)
        azimuth = row * GOLDEN_ANGLE + rng.uniform(-0.25, 0.25)
        length = branch_length * (1.0 - t) ** 0.85 + 0.25
        droop = -0.30 + t * 0.12
        direction = Vector((math.cos(azimuth), math.sin(azimuth), droop)).normalized()
        tip = trunk_point + direction * length + Vector((0, 0, 0.10 * length))
        wood.append(add_cone_between(trunk_point, tip, 0.045 * (1.0 - t * 0.5), 0.008, sides=9 if high_detail else 5))
        branch_count += 1
        # Secondary twigs create a readable hierarchy: trunk -> limb -> twig
        # -> needle cluster. Sky gaps are retained instead of hiding the crown
        # in a simplified cone-shaped canopy shell.
        limb_side = direction.cross(Vector((0, 0, 1))).normalized()
        for station in (0.28, 0.52, 0.76, 0.96):
            origin = trunk_point.lerp(tip, station)
            for fork_sign in ((-1.0, 1.0) if high_detail else (1.0,)):
                fork = (direction * 0.55 + limb_side * fork_sign * rng.uniform(0.45, 0.95) + Vector((0, 0, rng.uniform(0.12, 0.42)))).normalized()
                twig_tip = origin + fork * (0.34 + (1.0 - t) * 0.38)
                twig = add_cone_between(origin, twig_tip, 0.020 * (1.0 - t * 0.45), 0.004, sides=7 if high_detail else 4)
                wood.append(twig)
                # Dense individual needles are distributed along paired twigs
                # on both sides of each limb, creating a continuous bough.
                for bough_index in range(3):
                    bough_center = origin.lerp(twig_tip, 0.30 + bough_index * 0.30) + limb_side * rng.uniform(-0.10, 0.10)
                    spray_cluster(rng, spray_verts, spray_faces, spray_side, bough_center, fork, count=64 if high_detail else 24,
                                  blade_length=(0.20 if high_detail else 0.48) * (1.0 - t * 0.22), spread=0.72,
                                  width_min=0.025 if high_detail else 0.16, width_max=0.065 if high_detail else 0.30)
    # Crown tuft at the very top.
    spray_cluster(rng, spray_verts, spray_faces, spray_side, points[-1], Vector((0, 0, 1)), count=90 if high_detail else 30,
                  blade_length=0.25 if high_detail else 0.48, spread=0.62,
                  width_min=0.025 if high_detail else 0.16, width_max=0.065 if high_detail else 0.30)

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
    bpy.ops.mesh.primitive_ico_sphere_add(subdivisions=5, radius=1.25, location=(0, 0, 0.55))
    boulder = bpy.context.active_object
    boulder.scale = (1.3, 1.05, 0.75)
    texture = bpy.data.textures.new("boulder-noise", type="CLOUDS")
    texture.noise_scale = 1.1
    modifier = boulder.modifiers.new("Displace", "DISPLACE")
    modifier.texture = texture
    modifier.strength = 0.55
    fine_texture = bpy.data.textures.new("boulder-fine-noise", type="CLOUDS"); fine_texture.noise_scale = 0.18
    fine_modifier = boulder.modifiers.new("Fine stone relief", "DISPLACE"); fine_modifier.texture = fine_texture; fine_modifier.strength = 0.075
    bpy.ops.object.shade_smooth()
    boulder.data.materials.append(basalt)
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


def add_leaf_blade(verts, faces, center, direction, length, width):
    """A gently cupped, two-sided leaf; small plants stay volumetric at close range."""
    side = direction.cross(Vector((0, 0, 1))).normalized() * width
    i = len(verts)
    for station, profile in ((0.0, 0.15), (0.32, 0.82), (0.68, 1.0), (1.0, 0.05)):
        middle = center + direction * (length * station) + Vector((0, 0, math.sin(station * math.pi) * length * 0.16))
        verts.extend([middle - side * profile, middle + side * profile])
    faces.extend([(i, i + 1, i + 3, i + 2), (i + 2, i + 3, i + 5, i + 4), (i + 4, i + 5, i + 7, i + 6)])


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
            bpy.ops.mesh.primitive_ico_sphere_add(subdivisions=2, radius=0.045, location=top + Vector((0, 0, 0.008)))
            bpy.context.active_object.data.materials.append(pollen)
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
        bpy.ops.mesh.primitive_uv_sphere_add(segments=14, ring_count=8, radius=0.045, location=top)
        bud = bpy.context.active_object; bud.scale = (1.0, 1.0, 1.55); bud.data.materials.append(bloom)
    leaf_mesh = bpy.data.meshes.new("IndividualHeatherLeaves"); leaf_mesh.from_pydata(leaf_verts, [], leaf_faces); leaf_mesh.materials.append(foliage)
    for polygon in leaf_mesh.polygons: polygon.use_smooth = True
    leaf_object = bpy.data.objects.new("IndividualHeatherLeaves", leaf_mesh); bpy.context.collection.objects.link(leaf_object)
    export_glb("detail_heather.glb")


def build_mushrooms():
    reset_scene()
    rng = random.Random(79)
    stem = solid_material("MushroomStem", (0.43, 0.34, 0.22, 1.0))
    cap = solid_material("MushroomCap", (0.26, 0.065, 0.026, 1.0), roughness=0.64)
    for n in range(11):
        a, r = rng.random() * math.tau, rng.uniform(0.05, 0.62)
        h, rad = rng.uniform(0.08, 0.26), rng.uniform(0.07, 0.16)
        loc = (math.cos(a) * r, math.sin(a) * r, h * 0.5)
        bpy.ops.mesh.primitive_cone_add(vertices=18, radius1=rad * 0.34, radius2=rad * 0.24, depth=h, location=loc)
        bpy.context.active_object.data.materials.append(stem)
        bpy.ops.mesh.primitive_uv_sphere_add(segments=28, ring_count=14, radius=rad, location=(loc[0], loc[1], h))
        head = bpy.context.active_object; head.scale = (1.0, 1.0, 0.44); head.data.materials.append(cap)
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
            bpy.ops.mesh.primitive_uv_sphere_add(segments=18, ring_count=10, radius=0.055, location=top - Vector((0, 0, 0.12)))
            head = bpy.context.active_object; head.scale = (0.72, 0.72, 2.8); head.data.materials.append(seed)
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
            bpy.ops.mesh.primitive_ico_sphere_add(subdivisions=2, radius=0.04, location=top)
            bpy.context.active_object.data.materials.append(berry)
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
    bpy.ops.mesh.primitive_uv_sphere_add(segments=28, ring_count=14, radius=0.48, location=(0, 0.0, 1.62))
    torso = bpy.context.active_object; torso.name = "WayfinderTorso"; torso.scale = (0.82, 0.55, 1.04); torso.data.materials.append(trim); bpy.ops.object.shade_smooth()
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
    bpy.ops.mesh.primitive_uv_sphere_add(segments=32, ring_count=12, radius=0.53, location=(0, 0.02, 1.95))
    mantle = bpy.context.active_object; mantle.name = "LayeredShoulderMantle"; mantle.scale = (1.08, 0.66, 0.43); mantle.data.materials.append(cloth); bpy.ops.object.shade_smooth()
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
        bpy.ops.mesh.primitive_uv_sphere_add(segments=16, ring_count=8, radius=0.105, location=wrist)
        hand = bpy.context.active_object; hand.name = f"Hand{index}"; hand.scale = (0.82, 0.72, 1.15); hand.data.materials.append(skin); bpy.ops.object.shade_smooth()

    # Hood shell, thick oval rim, and deeply recessed face cavity.
    bpy.ops.mesh.primitive_uv_sphere_add(segments=40, ring_count=20, radius=0.43, location=(0, 0.02, 2.34))
    hood = bpy.context.active_object; hood.name = "TailoredWayfinderHood"; hood.scale = (0.93, 0.86, 1.12); hood.data.materials.append(cloth); bpy.ops.object.shade_smooth()
    bpy.ops.mesh.primitive_torus_add(major_radius=0.215, minor_radius=0.035, major_segments=36, minor_segments=10,
                                    location=(0, -0.345, 2.33), rotation=(math.radians(90), 0, 0))
    hood_rim = bpy.context.active_object; hood_rim.name = "HoodOpeningRim"; hood_rim.scale = (0.86, 1.0, 1.18); hood_rim.data.materials.append(trim)
    bpy.ops.mesh.primitive_uv_sphere_add(segments=28, ring_count=14, radius=0.185, location=(0, -0.365, 2.31))
    hood_void = bpy.context.active_object; hood_void.name = "HoodShadow"; hood_void.scale = (0.82, 0.16, 1.06); hood_void.data.materials.append(face)
    bpy.ops.mesh.primitive_ico_sphere_add(subdivisions=2, radius=0.055, location=(0, -0.43, 1.98))
    brooch = bpy.context.active_object; brooch.name = "VeilBrooch"; brooch.data.materials.append(rune)

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
    bpy.ops.mesh.primitive_ico_sphere_add(subdivisions=3, radius=0.125, location=(0.70, -0.38, 2.80))
    focus = bpy.context.active_object; focus.data.materials.append(rune); bpy.ops.object.shade_smooth()
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
build_birch("detail_birch.glb", seed=151, branch_rows=24, leaves_per_twig=18)
build_birch("detail_birch_lod.glb", seed=151, branch_rows=14, leaves_per_twig=7)
build_snag()
build_boulder()
build_fern()
build_log()
build_wildflower()
build_heather()
build_mushrooms()
build_reeds()
build_shrub()
build_meadow_grass()
build_wayfinder()
print("DETAIL ASSETS EXPORTED")

