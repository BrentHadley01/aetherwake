import bpy
import math
import random
from mathutils import Vector
from pathlib import Path

random.seed(17)
ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "assets" / "models"
OUT.mkdir(parents=True, exist_ok=True)

bpy.ops.object.select_all(action="SELECT")
bpy.ops.object.delete(use_global=False)

def pbr_material(name, base_a, base_b, roughness, metallic=0.0, scale=3.0, moss=False):
    mat = bpy.data.materials.new(name)
    mat.use_nodes = True
    nodes, links = mat.node_tree.nodes, mat.node_tree.links
    for node in nodes: nodes.remove(node)
    output = nodes.new('ShaderNodeOutputMaterial')
    bsdf = nodes.new('ShaderNodeBsdfPrincipled')
    noise = nodes.new('ShaderNodeTexNoise'); noise.inputs['Scale'].default_value = scale; noise.inputs['Detail'].default_value = 7.0; noise.inputs['Roughness'].default_value = 0.72
    ramp = nodes.new('ShaderNodeValToRGB')
    ramp.color_ramp.elements[0].color = (*base_a, 1)
    ramp.color_ramp.elements[1].color = (*base_b, 1)
    bump = nodes.new('ShaderNodeBump'); bump.inputs['Strength'].default_value = 0.45; bump.inputs['Distance'].default_value = 0.16
    links.new(noise.outputs['Fac'], ramp.inputs['Fac']); links.new(ramp.outputs['Color'], bsdf.inputs['Base Color']); links.new(noise.outputs['Fac'], bump.inputs['Height']); links.new(bump.outputs['Normal'], bsdf.inputs['Normal']); links.new(bsdf.outputs['BSDF'], output.inputs['Surface'])
    bsdf.inputs['Roughness'].default_value = roughness; bsdf.inputs['Metallic'].default_value = metallic
    if moss:
        mix = nodes.new('ShaderNodeMixRGB'); mix.blend_type = 'MULTIPLY'; mix.inputs[2].default_value = (0.25, 0.55, 0.24, 1); mix.inputs[0].default_value = 0.28
        links.new(ramp.outputs['Color'], mix.inputs[1]); links.new(mix.outputs['Color'], bsdf.inputs['Base Color'])
    return mat

def emission_material(name, color, strength):
    mat = bpy.data.materials.new(name); mat.use_nodes = True
    bsdf = mat.node_tree.nodes.get('Principled BSDF'); bsdf.inputs['Base Color'].default_value = (*color, 1); bsdf.inputs['Emission Color'].default_value = (*color, 1); bsdf.inputs['Emission Strength'].default_value = strength; bsdf.inputs['Roughness'].default_value = 0.28
    return mat

stone = pbr_material('Basalt with lichen', (0.035, 0.048, 0.058), (0.19, 0.23, 0.24), 0.82, scale=4.0, moss=True)
ground = pbr_material('Rain-soaked forest soil', (0.018, 0.028, 0.012), (0.11, 0.075, 0.028), 0.94, scale=1.8)
bark = pbr_material('Ancient wet bark', (0.016, 0.007, 0.003), (0.17, 0.075, 0.02), 0.79, scale=7.0)
cloth = pbr_material('Wayfinder wool', (0.006, 0.012, 0.022), (0.035, 0.09, 0.12), 0.64, scale=10.0)
corruption = pbr_material('Thorn Warden shell', (0.005, 0.03, 0.024), (0.045, 0.17, 0.12), 0.48, metallic=0.25, scale=5.0)
rune = emission_material('Aether rune', (0.0, 0.74, 0.48), 5.5)

def apply(obj, name, mat):
    obj.name = name; obj.data.materials.append(mat); return obj

def rock(name, loc, scale, mat=stone):
    bpy.ops.mesh.primitive_ico_sphere_add(subdivisions=3, radius=1, location=loc)
    obj = bpy.context.object; obj.scale = scale; bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    disp = obj.modifiers.new('Weathered fracture', 'DISPLACE'); tex = bpy.data.textures.new(name + ' fracture', type='CLOUDS'); tex.noise_scale = 0.42; tex.noise_depth = 2; disp.texture = tex; disp.strength = 0.22
    bevel = obj.modifiers.new('Soft eroded edges', 'BEVEL'); bevel.width = 0.06; bevel.segments = 2
    return apply(obj, name, mat)

def column(name, loc, height, radius=0.65):
    bpy.ops.mesh.primitive_cylinder_add(vertices=32, radius=radius, depth=height, location=(loc[0], loc[1], loc[2] + height / 2))
    obj = apply(bpy.context.object, name, stone); bevel = obj.modifiers.new('Worn edges', 'BEVEL'); bevel.width = 0.08; bevel.segments = 3
    for z in (0.12, height - 0.12):
        bpy.ops.mesh.primitive_torus_add(major_radius=radius * 1.12, minor_radius=0.09, major_segments=32, minor_segments=8, location=(loc[0], loc[1], loc[2] + z))
        apply(bpy.context.object, name + ' band', stone)
    return obj

def tree(name, x, y, height, radius):
    bpy.ops.mesh.primitive_cylinder_add(vertices=18, radius=radius, depth=height, location=(x, y, height / 2))
    trunk = apply(bpy.context.object, name + ' trunk', bark); bevel = trunk.modifiers.new('Bark edge softening', 'BEVEL'); bevel.width = 0.06; bevel.segments = 2
    for j in range(4):
        angle = random.random() * math.tau; z = height * (0.42 + j * 0.11)
        bpy.ops.mesh.primitive_cone_add(vertices=12, radius1=radius * 1.8, radius2=radius * 0.35, depth=height * 0.55, location=(x + math.cos(angle) * radius * 0.7, y + math.sin(angle) * radius * 0.7, z + height * 0.25))
        foliage = apply(bpy.context.object, name + ' crown', pbr_material(name + str(j), (0.008, 0.025, 0.012), (0.05, 0.16, 0.06), 0.88, scale=9.0)); foliage.rotation_euler = (random.random(), random.random(), angle)

# Dense displaced terrain with a damp, uneven forest floor.
bpy.ops.mesh.primitive_grid_add(x_subdivisions=96, y_subdivisions=96, size=52, location=(0, 0, 0))
terrain = apply(bpy.context.object, 'Hollowmere terrain', ground)
terrain_disp = terrain.modifiers.new('Terrain erosion', 'DISPLACE'); terrain_tex = bpy.data.textures.new('Eroded soil', type='CLOUDS'); terrain_tex.noise_scale = 2.7; terrain_tex.noise_depth = 3; terrain_disp.texture = terrain_tex; terrain_disp.strength = 0.58

# Observatory ruins: deliberately irregular columns, broken wall fragments, and scattered stones.
for i, angle in enumerate(range(0, 360, 45)):
    a = math.radians(angle); r = 8.5; height = random.uniform(4.0, 7.2)
    column('Observatory column %02d' % i, (math.cos(a) * r, math.sin(a) * r, 0), height)
    if i % 2 == 0: rock('Fallen capital %02d' % i, (math.cos(a) * (r - 1.1), math.sin(a) * (r - 1.1), 0.45), (1.25, 0.8, 0.65))
for i in range(30):
    a, r = random.random() * math.tau, random.uniform(3.5, 17.0)
    rock('Forest stone %02d' % i, (math.cos(a) * r, math.sin(a) * r, random.uniform(-0.1, 0.28)), (random.uniform(0.25, 1.1), random.uniform(0.22, 0.78), random.uniform(0.18, 0.56)))
for i, (x, y) in enumerate([(-20,-18),(-18,3),(-16,18),(19,-16),(20,4),(17,18),(-4,22),(5,-21)]): tree('Old growth %02d' % i, x, y, random.uniform(8, 13), random.uniform(0.35, 0.72))

# Mage silhouette with a hand-built cloak, staff, and luminous focus.
bpy.ops.mesh.primitive_cone_add(vertices=48, radius1=0.92, radius2=0.36, depth=2.7, location=(0, -8.5, 1.35)); apply(bpy.context.object, 'Wayfinder cloak', cloth)
bpy.ops.mesh.primitive_uv_sphere_add(segments=32, ring_count=16, radius=0.48, location=(0, -8.5, 2.75)); apply(bpy.context.object, 'Wayfinder hood', cloth)
bpy.ops.mesh.primitive_cylinder_add(vertices=12, radius=0.055, depth=3.3, location=(0.78, -8.45, 1.65), rotation=(0.12, -0.22, 0.18)); apply(bpy.context.object, 'Wayfinder ashwood staff', bark)
bpy.ops.mesh.primitive_ico_sphere_add(subdivisions=3, radius=0.19, location=(1.05, -8.22, 3.2)); apply(bpy.context.object, 'Wayfinder aether focus', rune)

# Thorn Warden: denser shell, thorns, and exposed magic core.
rock('Thorn Warden shell', (0, 3.0, 1.45), (1.55, 1.18, 1.55), corruption)
for i in range(16):
    a = math.tau * i / 16; z = random.uniform(0.65, 2.5)
    bpy.ops.mesh.primitive_cone_add(vertices=10, radius1=0.22, radius2=0.015, depth=random.uniform(1.3, 2.5), location=(math.cos(a)*1.35, 3.0 + math.sin(a)*1.15, z), rotation=(math.radians(70), 0, a))
    apply(bpy.context.object, 'Warden thorn', stone)
bpy.ops.mesh.primitive_ico_sphere_add(subdivisions=3, radius=0.38, location=(0, 2.0, 1.75)); apply(bpy.context.object, 'Warden exposed core', rune)

# Observatory heart / objective.
column('Observatory heart plinth', (0, 10.5, 0), 1.1, 1.05)
bpy.ops.mesh.primitive_cylinder_add(vertices=8, radius=0.58, depth=2.2, location=(0, 10.5, 2.05)); apply(bpy.context.object, 'Observatory heart', rune)

# Cinematic atmosphere: moon, warm rim, volumetric mist, and a low player-facing camera.
bpy.ops.object.light_add(type='AREA', location=(2, -5, 12)); moon = bpy.context.object; moon.data.energy = 2800; moon.data.color = (0.26, 0.58, 0.90); moon.data.shape = 'DISK'; moon.data.size = 11
bpy.ops.object.light_add(type='AREA', location=(-8, 2, 6)); bpy.context.object.data.energy = 1550; bpy.context.object.data.color = (0.10, 0.95, 0.54); bpy.context.object.data.size = 5
bpy.ops.object.light_add(type='AREA', location=(8, 7, 4)); bpy.context.object.data.energy = 1050; bpy.context.object.data.color = (0.60, 0.28, 0.09); bpy.context.object.data.size = 4
bpy.ops.mesh.primitive_cube_add(location=(0, 2, 5), scale=(22, 22, 5)); fog = bpy.context.object; fog.name = 'Low rain fog'; fog_mat = bpy.data.materials.new('Volumetric mist'); fog_mat.use_nodes = True; nodes = fog_mat.node_tree.nodes; bsdf = nodes.get('Principled BSDF'); fog_mat.node_tree.nodes.remove(bsdf); volume = nodes.new('ShaderNodeVolumePrincipled'); volume.inputs['Density'].default_value = 0.005; volume.inputs['Color'].default_value = (0.06, 0.12, 0.14, 1); fog_mat.node_tree.links.new(volume.outputs['Volume'], nodes.get('Material Output').inputs['Volume']); fog.data.materials.append(fog_mat)
bpy.ops.object.camera_add(location=(16.5, -25, 9.5)); camera = bpy.context.object; bpy.context.scene.camera = camera; camera.rotation_euler = (Vector((0, 1.0, 1.9)) - camera.location).to_track_quat('-Z', 'Y').to_euler(); camera.data.lens = 52

scene = bpy.context.scene; scene.render.engine = 'BLENDER_EEVEE'; scene.render.resolution_x = 1600; scene.render.resolution_y = 900; scene.render.resolution_percentage = 100; scene.render.image_settings.file_format = 'PNG'; scene.render.film_transparent = False
scene.world.color = (0.012, 0.025, 0.04)
scene.render.filepath = str(OUT / 'veiled_reach-realistic-preview.png')
bpy.ops.wm.save_as_mainfile(filepath=str(OUT / 'veiled_reach-realistic.blend'))
bpy.ops.export_scene.gltf(filepath=str(OUT / 'veiled_reach-realistic.glb'), export_format='GLB', use_selection=False)
bpy.ops.render.render(write_still=True)
print('Created realistic Veiled Reach asset set.')
