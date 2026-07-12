"""Authors the streamed-world detail meshes (pine, snag, boulder) and exports GLBs.

Run headless:  blender --background --python scripts/build_detail_assets.py
Each asset keeps its origin at the ground plane so the engine can drop it onto
the terrain heightfield directly.
"""
import os

import bpy

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TEXTURES = os.path.join(ROOT, "assets", "textures")
MODELS = os.path.join(ROOT, "assets", "models")


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


def displace(obj, strength, size=1.4, noise_seed=1):
    texture = bpy.data.textures.new(f"{obj.name}-noise", type="CLOUDS")
    texture.noise_scale = size
    modifier = obj.modifiers.new("Displace", "DISPLACE")
    modifier.texture = texture
    modifier.strength = strength


def export_glb(filename):
    bpy.ops.export_scene.gltf(
        filepath=os.path.join(MODELS, filename),
        export_format="GLB",
        export_apply=True,
        export_yup=True,
    )


def build_pine():
    reset_scene()
    bark = textured_material("PineTrunk", "wet_bark_albedo.png")
    needles_dark = solid_material("PineNeedlesDark", (0.030, 0.075, 0.038, 1.0))
    needles_lit = solid_material("PineNeedlesLit", (0.052, 0.115, 0.055, 1.0))
    bpy.ops.mesh.primitive_cone_add(vertices=10, radius1=0.24, radius2=0.04, depth=8.0, location=(0, 0, 4.0))
    trunk = bpy.context.active_object
    trunk.data.materials.append(bark)
    tiers = [
        (2.7, 3.0, 2.6, 0.14, 0.05), (2.35, 2.8, 3.9, -0.10, 0.12), (2.0, 2.7, 5.1, 0.07, -0.11),
        (1.65, 2.5, 6.2, -0.12, -0.04), (1.3, 2.3, 7.2, 0.09, 0.08), (0.95, 2.1, 8.1, -0.05, 0.06), (0.55, 1.9, 9.0, 0.0, 0.0),
    ]
    for index, (radius, depth, z, ox, oy) in enumerate(tiers):
        bpy.ops.mesh.primitive_cone_add(vertices=14, radius1=radius, radius2=0.02, depth=depth, location=(ox, oy, z))
        tier = bpy.context.active_object
        bpy.ops.object.mode_set(mode="EDIT")
        bpy.ops.mesh.select_all(action="SELECT")
        bpy.ops.mesh.subdivide(number_cuts=3)
        bpy.ops.object.mode_set(mode="OBJECT")
        displace(tier, 0.38, 0.35, noise_seed=index)
        bpy.ops.object.shade_smooth()
        tier.data.materials.append(needles_lit if index % 2 else needles_dark)
    export_glb("detail_pine.glb")


def build_snag():
    reset_scene()
    bark = textured_material("SnagBark", "wet_bark_albedo.png", roughness=0.95)
    bpy.ops.mesh.primitive_cone_add(vertices=9, radius1=0.26, radius2=0.03, depth=6.2, location=(0, 0, 3.1))
    trunk = bpy.context.active_object
    displace(trunk, 0.10, 0.6)
    trunk.data.materials.append(bark)
    branches = [((0.05, 0, 3.9), (0, 1.05, 0.4), 1.9), ((-0.04, 0.03, 4.8), (0, -0.9, -0.7), 1.5), ((0, -0.05, 2.9), (0.35, 0.95, 0.9), 1.6)]
    for location, rotation, depth in branches:
        bpy.ops.mesh.primitive_cone_add(vertices=7, radius1=0.09, radius2=0.015, depth=depth, location=location, rotation=rotation)
        branch = bpy.context.active_object
        branch.data.materials.append(bark)
    export_glb("detail_snag.glb")


def build_boulder():
    reset_scene()
    basalt = textured_material("BoulderBasalt", "granite_lichen_albedo.png", roughness=0.92)
    bpy.ops.mesh.primitive_ico_sphere_add(subdivisions=3, radius=1.25, location=(0, 0, 0.55))
    boulder = bpy.context.active_object
    boulder.scale = (1.3, 1.05, 0.75)
    displace(boulder, 0.55, 1.1)
    bpy.ops.object.shade_smooth()
    boulder.data.materials.append(basalt)
    export_glb("detail_boulder.glb")


build_pine()
build_snag()
build_boulder()
print("DETAIL ASSETS EXPORTED")
