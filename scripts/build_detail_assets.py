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


def spray_cluster(rng, verts, faces, face_sides, origin, direction, count, blade_length, spread):
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
        width = length * rng.uniform(0.13, 0.22)
        blade_side = blade_dir.cross(up if abs(blade_dir.dot(up)) < 0.9 else side).normalized() * width
        root = origin + direction * rng.uniform(-blade_length * 0.2, blade_length * 0.4)
        tip = root + blade_dir * length
        base_index = len(verts)
        verts.extend([root - blade_side * 0.5, root + blade_side * 0.5, tip])
        faces.append((base_index, base_index + 1, base_index + 2))
        face_sides.append(rng.random() < 0.5)


def build_conifer(filename, seed, height, branch_rows, branch_length, needle_dark, needle_lit):
    reset_scene()
    rng = random.Random(seed)
    bark = textured_material("ConiferBark", "pine_bark_diff.jpg")
    dark = solid_material("NeedlesDark", needle_dark)
    lit = solid_material("NeedlesLit", needle_lit)

    wood = []
    # Wandering tapered trunk built from stacked segments.
    segments = 6
    base = Vector((0, 0, 0))
    drift = Vector((0, 0, 0))
    points = [base.copy()]
    for i in range(1, segments + 1):
        drift += Vector((rng.uniform(-0.14, 0.14), rng.uniform(-0.14, 0.14), 0))
        points.append(Vector((drift.x, drift.y, height * i / segments)))
    for i in range(segments):
        t0, t1 = i / segments, (i + 1) / segments
        wood.append(add_cone_between(points[i], points[i + 1], 0.26 * (1 - t0) + 0.03, 0.26 * (1 - t1) + 0.03, sides=8))

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
        wood.append(add_cone_between(trunk_point, tip, 0.045 * (1.0 - t * 0.5), 0.008, sides=5))
        branch_count += 1
        # Secondary twigs create a readable hierarchy: trunk -> limb -> twig
        # -> needle cluster. Sky gaps are retained instead of hiding the crown
        # in a simplified cone-shaped canopy shell.
        limb_side = direction.cross(Vector((0, 0, 1))).normalized()
        for station in (0.28, 0.52, 0.76, 0.96):
            origin = trunk_point.lerp(tip, station)
            fork = (direction * 0.55 + limb_side * rng.uniform(-0.9, 0.9) + Vector((0, 0, rng.uniform(0.12, 0.42)))).normalized()
            twig_tip = origin + fork * (0.34 + (1.0 - t) * 0.38)
            twig = add_cone_between(origin, twig_tip, 0.020 * (1.0 - t * 0.45), 0.004, sides=4)
            wood.append(twig)
            # Dense individual needles are distributed along the twig. They
            # make each bough read as foliage without reverting to a single
            # smooth canopy volume.
            for bough_index in range(3):
                bough_center = origin.lerp(twig_tip, 0.30 + bough_index * 0.30) + limb_side * rng.uniform(-0.10, 0.10)
                spray_cluster(rng, spray_verts, spray_faces, spray_side, bough_center, fork, count=15,
                              blade_length=0.47 * (1.0 - t * 0.22), spread=0.55)
    # Crown tuft at the very top.
    spray_cluster(rng, spray_verts, spray_faces, spray_side, points[-1], Vector((0, 0, 1)), count=16, blade_length=0.8, spread=0.45)

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
    parts = [add_cone_between(Vector((0, 0, 0)), Vector((rng.uniform(-0.3, 0.3), rng.uniform(-0.3, 0.3), 6.4)), 0.24, 0.03, sides=7)]
    for _ in range(4):
        z = rng.uniform(2.2, 5.2)
        azimuth = rng.uniform(0, 6.283)
        direction = Vector((math.cos(azimuth), math.sin(azimuth), rng.uniform(-0.15, 0.45))).normalized()
        parts.append(add_cone_between(Vector((0, 0, z)), Vector((0, 0, z)) + direction * rng.uniform(0.9, 1.8), 0.07, 0.012, sides=5))
    for obj in parts:
        obj.data.materials.append(bark)
    bpy.ops.object.select_all(action="SELECT")
    bpy.context.view_layer.objects.active = parts[0]
    bpy.ops.object.join()
    export_glb("detail_snag.glb")


def build_boulder():
    reset_scene()
    basalt = textured_material("BoulderBasalt", "mossy_rock_diff.jpg", roughness=0.92)
    bpy.ops.mesh.primitive_ico_sphere_add(subdivisions=3, radius=1.25, location=(0, 0, 0.55))
    boulder = bpy.context.active_object
    boulder.scale = (1.3, 1.05, 0.75)
    texture = bpy.data.textures.new("boulder-noise", type="CLOUDS")
    texture.noise_scale = 1.1
    modifier = boulder.modifiers.new("Displace", "DISPLACE")
    modifier.texture = texture
    modifier.strength = 0.55
    bpy.ops.object.shade_smooth()
    boulder.data.materials.append(basalt)
    export_glb("detail_boulder.glb")


def build_fern():
    reset_scene()
    rng = random.Random(23)
    frond_dark = solid_material("FernDark", (0.020, 0.055, 0.018, 1.0))
    frond_lit = solid_material("FernLit", (0.034, 0.088, 0.026, 1.0))
    verts, faces, sides = [], [], []
    for frond in range(11):
        azimuth = frond * GOLDEN_ANGLE + rng.uniform(-0.2, 0.2)
        direction = Vector((math.cos(azimuth), math.sin(azimuth), 0))
        length = rng.uniform(0.55, 0.95)
        width = length * 0.16
        steps = 5
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
    log = add_cone_between(Vector((-2.1, 0, 0.24)), Vector((2.1, 0.3, 0.30)), 0.30, 0.22, sides=9)
    log.data.materials.append(bark)
    texture = bpy.data.textures.new("log-noise", type="CLOUDS")
    texture.noise_scale = 0.5
    modifier = log.modifiers.new("Displace", "DISPLACE")
    modifier.texture = texture
    modifier.strength = 0.08
    export_glb("detail_log.glb")


def add_leaf_blade(verts, faces, center, direction, length, width):
    """A gently cupped, two-sided leaf; small plants stay volumetric at close range."""
    side = direction.cross(Vector((0, 0, 1))).normalized() * width
    tip = center + direction * length + Vector((0, 0, length * 0.12))
    mid = center + direction * (length * 0.52) + Vector((0, 0, length * 0.16))
    i = len(verts)
    verts.extend([center, mid - side, tip, mid + side])
    faces.extend([(i, i + 1, i + 2), (i, i + 2, i + 3)])


def build_wildflower():
    """A varied meadow clump: stems, leaves, and modest moonlit flower heads."""
    reset_scene()
    rng = random.Random(47)
    stem = solid_material("FlowerStems", (0.028, 0.105, 0.026, 1.0))
    leaf = solid_material("FlowerLeaves", (0.036, 0.135, 0.038, 1.0))
    petal = solid_material("MoonflowerPetals", (0.48, 0.29, 0.58, 1.0), roughness=0.72)
    pollen = solid_material("MoonflowerPollen", (0.78, 0.50, 0.12, 1.0), roughness=0.58)
    stems, leaves = [], []
    leaf_verts, leaf_faces = [], []
    for n in range(15):
        a, r = rng.random() * math.tau, rng.uniform(0.04, 0.72)
        root = Vector((math.cos(a) * r, math.sin(a) * r, 0))
        h = rng.uniform(0.30, 0.82)
        top = root + Vector((rng.uniform(-0.10, 0.10), rng.uniform(-0.10, 0.10), h))
        stalk = add_cone_between(root, top, 0.012, 0.006, sides=5); stalk.data.materials.append(stem); stems.append(stalk)
        for t in (0.26, 0.52):
            add_leaf_blade(leaf_verts, leaf_faces, root.lerp(top, t), Vector((math.cos(a + math.pi * t * 3), math.sin(a + math.pi * t * 3), 0)), rng.uniform(0.10, 0.20), 0.035)
        if n % 2 == 0:
            for p in range(5):
                angle = p * math.tau / 5.0
                bpy.ops.mesh.primitive_uv_sphere_add(segments=10, ring_count=6, radius=0.070, location=top + Vector((math.cos(angle) * 0.075, math.sin(angle) * 0.075, 0)))
                flower = bpy.context.active_object; flower.scale = (1.0, 0.42, 0.28); flower.data.materials.append(petal)
            bpy.ops.mesh.primitive_ico_sphere_add(subdivisions=1, radius=0.045, location=top + Vector((0, 0, 0.008)))
            bpy.context.active_object.data.materials.append(pollen)
    mesh = bpy.data.meshes.new("WildflowerLeaves"); mesh.from_pydata(leaf_verts, [], leaf_faces); mesh.materials.append(leaf)
    obj = bpy.data.objects.new("WildflowerLeaves", mesh); bpy.context.collection.objects.link(obj)
    export_glb("detail_wildflower.glb")


def build_heather():
    reset_scene()
    rng = random.Random(61)
    twig = solid_material("HeatherTwig", (0.10, 0.045, 0.026, 1.0))
    foliage = solid_material("HeatherFoliage", (0.055, 0.105, 0.047, 1.0))
    bloom = solid_material("HeatherBloom", (0.38, 0.11, 0.38, 1.0), roughness=0.8)
    for n in range(28):
        a, r = rng.random() * math.tau, rng.uniform(0.02, 0.72)
        root = Vector((math.cos(a) * r, math.sin(a) * r, 0))
        top = root + Vector((math.cos(a) * rng.uniform(0.08, 0.25), math.sin(a) * rng.uniform(0.08, 0.25), rng.uniform(0.18, 0.50)))
        branch = add_cone_between(root, top, 0.018, 0.005, sides=5); branch.data.materials.append(twig)
        for b in range(3):
            q = root.lerp(top, 0.42 + b * 0.20)
            bpy.ops.mesh.primitive_ico_sphere_add(subdivisions=1, radius=0.09, location=q + Vector((rng.uniform(-0.07, 0.07), rng.uniform(-0.07, 0.07), 0.03)))
            bpy.context.active_object.data.materials.append(foliage)
        bpy.ops.mesh.primitive_uv_sphere_add(segments=8, ring_count=5, radius=0.045, location=top)
        bud = bpy.context.active_object; bud.scale = (1.0, 1.0, 1.55); bud.data.materials.append(bloom)
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
        bpy.ops.mesh.primitive_cone_add(vertices=10, radius1=rad * 0.34, radius2=rad * 0.24, depth=h, location=loc)
        bpy.context.active_object.data.materials.append(stem)
        bpy.ops.mesh.primitive_uv_sphere_add(segments=14, ring_count=8, radius=rad, location=(loc[0], loc[1], h))
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
        stalk = add_cone_between(root, top, 0.018, 0.010, sides=5); stalk.data.materials.append(reed)
        if n % 3 != 0:
            bpy.ops.mesh.primitive_uv_sphere_add(segments=10, ring_count=6, radius=0.055, location=top - Vector((0, 0, 0.12)))
            head = bpy.context.active_object; head.scale = (0.72, 0.72, 2.8); head.data.materials.append(seed)
    export_glb("detail_reeds.glb")


def build_shrub():
    reset_scene()
    rng = random.Random(113)
    wood = solid_material("ShrubWood", (0.075, 0.038, 0.020, 1.0))
    leaf = solid_material("ShrubLeaves", (0.028, 0.098, 0.025, 1.0))
    berry = solid_material("ShrubBerries", (0.16, 0.018, 0.025, 1.0), roughness=0.45)
    for n in range(21):
        a, r = rng.random() * math.tau, rng.uniform(0.02, 0.65)
        root = Vector((math.cos(a) * r, math.sin(a) * r, 0))
        top = root + Vector((math.cos(a) * rng.uniform(0.22, 0.62), math.sin(a) * rng.uniform(0.22, 0.62), rng.uniform(0.35, 0.82)))
        branch = add_cone_between(root, top, 0.025, 0.006, sides=5); branch.data.materials.append(wood)
        for b in range(3):
            q = root.lerp(top, 0.38 + b * 0.22)
            bpy.ops.mesh.primitive_ico_sphere_add(subdivisions=1, radius=0.13, location=q + Vector((rng.uniform(-0.12, 0.12), rng.uniform(-0.12, 0.12), 0.03)))
            bpy.context.active_object.data.materials.append(leaf)
        if n % 4 == 0:
            bpy.ops.mesh.primitive_ico_sphere_add(subdivisions=1, radius=0.04, location=top)
            bpy.context.active_object.data.materials.append(berry)
    export_glb("detail_shrub.glb")


def build_meadow_grass():
    """Broad-leafed grass tussock, distinct from the fine wind-swayed ground cover."""
    reset_scene()
    rng = random.Random(131)
    dark = solid_material("MeadowGrassDark", (0.035, 0.090, 0.026, 1.0))
    lit = solid_material("MeadowGrassLit", (0.075, 0.155, 0.042, 1.0))
    verts, faces = [], []
    for blade in range(30):
        a = blade * GOLDEN_ANGLE + rng.uniform(-0.16, 0.16)
        root = Vector((math.cos(a) * rng.uniform(0.00, 0.20), math.sin(a) * rng.uniform(0.00, 0.20), 0))
        direction = Vector((math.cos(a), math.sin(a), 0))
        length, width = rng.uniform(0.45, 0.92), rng.uniform(0.015, 0.032)
        side = direction.cross(Vector((0, 0, 1))).normalized() * width
        mid = root + direction * (length * 0.20) + Vector((0, 0, length * 0.62))
        tip = root + direction * (length * rng.uniform(0.44, 0.72)) + Vector((0, 0, length))
        i = len(verts); verts.extend([root - side, root + side, mid + side * 0.65, mid - side * 0.65, tip])
        faces.extend([(i, i + 1, i + 2, i + 3), (i + 3, i + 2, i + 4)])
    mesh = bpy.data.meshes.new("MeadowGrass"); mesh.from_pydata(verts, [], faces); mesh.materials.append(dark); mesh.materials.append(lit)
    for i, poly in enumerate(mesh.polygons): poly.material_index = i % 3 == 0
    obj = bpy.data.objects.new("MeadowGrass", mesh); bpy.context.collection.objects.link(obj)
    export_glb("detail_meadow_grass.glb")


def build_wayfinder():
    """Grounded third-person mage silhouette used by the playable runtime."""
    reset_scene()
    cloth = textured_material("WayfinderWool", "wayfinder_wool_albedo.png", roughness=0.82)
    leather = textured_material("WayfinderLeather", "wet_bark_albedo.png", roughness=0.68)
    face = solid_material("HoodShadow", (0.008, 0.010, 0.014, 1.0), roughness=0.95)
    metal = solid_material("StaffIron", (0.08, 0.10, 0.12, 1.0), roughness=0.38)
    rune = solid_material("StaffRune", (0.01, 0.68, 0.52, 1.0), roughness=0.22)

    # Layered travel cloak: a high-sided mantle over a tapered robe.
    bpy.ops.mesh.primitive_cone_add(vertices=40, radius1=0.72, radius2=0.30, depth=1.95, location=(0, 0, 1.0))
    robe = bpy.context.active_object; robe.name = "WayfinderRobe"; robe.data.materials.append(cloth)
    bevel = robe.modifiers.new("Cloth edge", "BEVEL"); bevel.width = 0.035; bevel.segments = 2
    bpy.ops.mesh.primitive_cone_add(vertices=40, radius1=0.58, radius2=0.20, depth=0.72, location=(0, -0.06, 1.90))
    mantle = bpy.context.active_object; mantle.name = "WayfinderMantle"; mantle.data.materials.append(cloth)
    # Hood outer shell and recessed face.
    bpy.ops.mesh.primitive_uv_sphere_add(segments=32, ring_count=16, radius=0.42, location=(0, 0, 2.30))
    hood = bpy.context.active_object; hood.name = "WayfinderHood"; hood.scale = (1.0, 0.92, 1.12); hood.data.materials.append(cloth); bpy.ops.object.shade_smooth()
    bpy.ops.mesh.primitive_uv_sphere_add(segments=24, ring_count=12, radius=0.29, location=(0, -0.30, 2.27))
    hood_void = bpy.context.active_object; hood_void.name = "HoodShadow"; hood_void.scale = (0.80, 0.25, 0.95); hood_void.data.materials.append(face)
    # Arms angled into a believable staff-bearing pose.
    left_arm = add_cone_between(Vector((-0.33, -0.02, 1.86)), Vector((-0.63, -0.32, 1.20)), 0.16, 0.11, sides=14)
    right_arm = add_cone_between(Vector((0.33, -0.02, 1.86)), Vector((0.62, -0.34, 1.46)), 0.16, 0.10, sides=14)
    left_arm.data.materials.append(cloth); right_arm.data.materials.append(cloth)
    # Staff, iron cage, and emissive-looking focus.
    staff = add_cone_between(Vector((0.70, -0.38, 0.10)), Vector((0.70, -0.38, 2.88)), 0.052, 0.035, sides=12)
    staff.data.materials.append(leather)
    bpy.ops.mesh.primitive_torus_add(major_radius=0.18, minor_radius=0.025, major_segments=24, minor_segments=8, location=(0.70, -0.38, 2.77), rotation=(math.radians(90), 0, 0))
    cage = bpy.context.active_object; cage.data.materials.append(metal)
    bpy.ops.mesh.primitive_ico_sphere_add(subdivisions=3, radius=0.12, location=(0.70, -0.38, 2.77))
    focus = bpy.context.active_object; focus.data.materials.append(rune); bpy.ops.object.shade_smooth()
    export_glb("detail_wayfinder.glb")


build_conifer("detail_pine.glb", seed=7, height=11.5, branch_rows=30, branch_length=2.6,
              needle_dark=(0.022, 0.060, 0.028, 1.0), needle_lit=(0.038, 0.095, 0.040, 1.0))
build_conifer("detail_spruce.glb", seed=19, height=8.0, branch_rows=26, branch_length=3.1,
              needle_dark=(0.020, 0.052, 0.032, 1.0), needle_lit=(0.033, 0.082, 0.048, 1.0))
# Far trees retain the authored silhouette but use fewer independently resolved
# branch clusters; they are selected only beyond readable needle distance.
build_conifer("detail_pine_lod.glb", seed=7, height=11.5, branch_rows=10, branch_length=2.6,
              needle_dark=(0.022, 0.060, 0.028, 1.0), needle_lit=(0.038, 0.095, 0.040, 1.0))
build_conifer("detail_spruce_lod.glb", seed=19, height=8.0, branch_rows=9, branch_length=3.1,
              needle_dark=(0.020, 0.052, 0.032, 1.0), needle_lit=(0.033, 0.082, 0.048, 1.0))
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

