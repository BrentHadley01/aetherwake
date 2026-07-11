import bpy
import math
from mathutils import Vector
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "assets" / "models"
OUT.mkdir(parents=True, exist_ok=True)

bpy.ops.object.select_all(action="SELECT")
bpy.ops.object.delete(use_global=False)

def material(name, color, metallic=0.0, roughness=0.6, emission=None):
    mat = bpy.data.materials.new(name)
    mat.diffuse_color = (*color, 1.0)
    mat.use_nodes = True
    bsdf = mat.node_tree.nodes.get("Principled BSDF")
    bsdf.inputs["Base Color"].default_value = (*color, 1.0)
    bsdf.inputs["Metallic"].default_value = metallic
    bsdf.inputs["Roughness"].default_value = roughness
    if emission:
        bsdf.inputs["Emission Color"].default_value = (*emission, 1.0)
        bsdf.inputs["Emission Strength"].default_value = 3.0
    return mat

moss = material("Moss Stone", (0.08, 0.16, 0.10), 0.0, 0.9)
stone = material("Ruin Basalt", (0.14, 0.17, 0.20), 0.15, 0.75)
wood = material("Wet Wood", (0.09, 0.045, 0.02), 0.0, 0.88)
cloth = material("Wayfinder Cloth", (0.045, 0.08, 0.13), 0.0, 0.62)
skin = material("Ash Skin", (0.21, 0.24, 0.25), 0.0, 0.8)
corruption = material("Corruption", (0.02, 0.13, 0.11), 0.15, 0.45, (0.0, 0.85, 0.62))
rune = material("Rune Light", (0.0, 0.34, 0.29), 0.1, 0.25, (0.0, 0.95, 0.7))

def apply(obj, mat, name):
    obj.name = name
    obj.data.materials.append(mat)
    return obj

def cube(name, loc, scale, mat, bevel=0.0):
    bpy.ops.mesh.primitive_cube_add(location=loc)
    obj = bpy.context.object
    obj.scale = scale
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    if bevel:
        mod = obj.modifiers.new("Soft weathered edges", "BEVEL")
        mod.width = bevel
        mod.segments = 2
    return apply(obj, mat, name)

def cylinder(name, loc, radius, depth, mat, vertices=12):
    bpy.ops.mesh.primitive_cylinder_add(vertices=vertices, radius=radius, depth=depth, location=loc)
    return apply(bpy.context.object, mat, name)

def sphere(name, loc, radius, mat, segments=16):
    bpy.ops.mesh.primitive_uv_sphere_add(segments=segments, ring_count=8, radius=radius, location=loc)
    return apply(bpy.context.object, mat, name)

# The playable arena: ground, ancient observatory, bramble gate, and paths.
cube("Hollowmere Ground", (0, 0, -0.35), (18, 18, 0.35), moss, 0.12)
for x in (-6, 6):
    for y in (-5, 5):
        cylinder("Observatory Column", (x, y, 2.1), 0.55, 4.2, stone)
        sphere("Column Cap", (x, y, 4.3), 0.72, stone, 12)
for a in range(0, 360, 30):
    angle = math.radians(a)
    cylinder("Observatory Ring", (math.cos(angle) * 8, math.sin(angle) * 8, 0.35), 0.4, 0.7, stone)

# Player mage, original simple production blockout.
mage = cube("Wayfinder Body", (0, -8, 1.4), (0.45, 0.32, 1.0), cloth, 0.12)
sphere("Wayfinder Hood", (0, -8, 2.55), 0.52, cloth)
staff = cylinder("Wayfinder Staff", (0.65, -8, 1.45), 0.07, 2.8, wood, 8)
staff.rotation_euler = (0.15, 0.0, 0.2)
sphere("Wayfinder Focus", (0.85, -8, 2.85), 0.20, rune, 12)

# Corrupted enemy and a clear rune objective.
sphere("Thorn Warden Core", (0, 4, 1.45), 1.25, corruption, 12)
for a in range(0, 360, 45):
    angle = math.radians(a)
    spike = cylinder("Thorn Warden Spike", (math.cos(angle) * 1.25, 4 + math.sin(angle) * 1.25, 1.55), 0.12, 2.0, stone, 6)
    spike.rotation_euler = (math.radians(72), 0, angle)
cylinder("Observatory Heart", (0, 10, 1.2), 0.8, 2.4, rune, 8)

# Forest silhouette / environmental cover.
for i, (x, y) in enumerate([(-14, -12), (-12, 4), (13, -10), (15, 5), (-11, 12), (12, 13)]):
    cylinder(f"Tree Trunk {i}", (x, y, 2.4), 0.45, 4.8, wood, 10)
    bpy.ops.mesh.primitive_cone_add(vertices=10, radius1=2.2, radius2=0.25, depth=5.5, location=(x, y, 6.3))
    apply(bpy.context.object, moss, f"Tree Canopy {i}")

# Lighting and camera give the asset an immediately inspectable presentation.
bpy.ops.object.light_add(type="AREA", location=(0, -2, 12))
bpy.context.object.data.energy = 1400
bpy.context.object.data.color = (0.18, 0.65, 0.72)
bpy.context.object.data.shape = "DISK"
bpy.context.object.data.size = 12
bpy.ops.object.light_add(type="SUN", location=(0, 0, 10))
bpy.context.object.data.energy = 1.2
bpy.context.object.rotation_euler = (math.radians(25), math.radians(-15), math.radians(-35))
bpy.ops.object.camera_add(location=(23, -28, 19))
camera = bpy.context.object
bpy.context.scene.camera = camera
direction = Vector((0, 1, 1.5)) - camera.location
camera.rotation_euler = direction.to_track_quat('-Z', 'Y').to_euler()

scene = bpy.context.scene
scene.render.engine = 'BLENDER_EEVEE'
scene.render.resolution_x = 1280
scene.render.resolution_y = 720
scene.render.resolution_percentage = 100
scene.world.color = (0.008, 0.015, 0.03)

bpy.ops.wm.save_as_mainfile(filepath=str(OUT / "veiled_reach.blend"))
bpy.ops.export_scene.gltf(filepath=str(OUT / "veiled_reach.glb"), export_format='GLB', use_selection=False)
scene.render.filepath = str(OUT / "veiled_reach-preview.png")
bpy.ops.render.render(write_still=True)
print("Created", OUT / "veiled_reach.blend")
