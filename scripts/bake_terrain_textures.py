"""Bakes tileable albedo textures (forest floor, granite, bark) with Cycles.

Run headless:  blender --background --python scripts/bake_terrain_textures.py
Each texture is an EMIT bake of a layered procedural node graph, so the result
is exact, fast, and seamless (all noise is 3D and evaluated on a unit plane).
"""
import os

import bpy

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TEXTURES = os.path.join(ROOT, "assets", "textures")
SIZE = 1024


def rgba_in(node, name):
    return next(s for s in node.inputs if s.name == name and s.type == "RGBA")


def rgba_out(node, name):
    return next(s for s in node.outputs if s.name == name and s.type == "RGBA")


def bake(name, build_graph):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = bpy.context.scene
    scene.render.engine = "CYCLES"
    scene.cycles.device = "CPU"
    scene.cycles.samples = 4
    bpy.ops.mesh.primitive_plane_add(size=2)
    plane = bpy.context.active_object
    material = bpy.data.materials.new(name)
    material.use_nodes = True
    plane.data.materials.append(material)
    nodes, links = material.node_tree.nodes, material.node_tree.links
    nodes.clear()
    output = nodes.new("ShaderNodeOutputMaterial")
    emission = nodes.new("ShaderNodeEmission")
    links.new(emission.outputs["Emission"], output.inputs["Surface"])
    color_socket = build_graph(nodes, links)
    links.new(color_socket, emission.inputs["Color"])
    image = bpy.data.images.new(name, SIZE, SIZE)
    target = nodes.new("ShaderNodeTexImage")
    target.image = image
    nodes.active = target
    bpy.ops.object.bake(type="EMIT")
    image.filepath_raw = os.path.join(TEXTURES, f"{name}.png")
    image.file_format = "PNG"
    image.save()
    print(f"BAKED {name}")


def noise(nodes, scale, detail=6.0):
    node = nodes.new("ShaderNodeTexNoise")
    node.inputs["Scale"].default_value = scale
    node.inputs["Detail"].default_value = detail
    return node


def ramp(nodes, links, source, stops):
    node = nodes.new("ShaderNodeValToRGB")
    node.color_ramp.elements[0].position = stops[0][0]
    node.color_ramp.elements[0].color = stops[0][1]
    node.color_ramp.elements[1].position = stops[1][0]
    node.color_ramp.elements[1].color = stops[1][1]
    links.new(source, node.inputs["Fac"])
    return node.outputs["Color"]


def const(nodes, rgba):
    node = nodes.new("ShaderNodeRGB")
    node.outputs["Color"].default_value = rgba
    return node.outputs["Color"]


def mix_color(nodes, links, fac, a, b):
    node = nodes.new("ShaderNodeMix")
    node.data_type = "RGBA"
    links.new(fac, node.inputs["Factor"])
    links.new(a, rgba_in(node, "A"))
    links.new(b, rgba_in(node, "B"))
    return rgba_out(node, "Result")


def multiply_color(nodes, links, a, b):
    node = nodes.new("ShaderNodeMix")
    node.data_type = "RGBA"
    node.blend_type = "MULTIPLY"
    node.inputs["Factor"].default_value = 1.0
    links.new(a, rgba_in(node, "A"))
    links.new(b, rgba_in(node, "B"))
    return rgba_out(node, "Result")


def forest_floor(nodes, links):
    base = ramp(nodes, links, noise(nodes, 7.0).outputs["Fac"],
                [(0.25, (0.086, 0.062, 0.040, 1)), (0.75, (0.168, 0.128, 0.078, 1))])
    moss_mask = ramp(nodes, links, noise(nodes, 11.0, 8.0).outputs["Fac"],
                     [(0.52, (0, 0, 0, 1)), (0.68, (1, 1, 1, 1))])
    moss = mix_color(nodes, links, moss_mask, base, const(nodes, (0.070, 0.120, 0.045, 1)))
    debris_mask = ramp(nodes, links, noise(nodes, 34.0, 10.0).outputs["Fac"],
                       [(0.62, (0, 0, 0, 1)), (0.72, (1, 1, 1, 1))])
    debris = mix_color(nodes, links, debris_mask, moss, const(nodes, (0.058, 0.042, 0.028, 1)))
    grain = ramp(nodes, links, noise(nodes, 120.0, 4.0).outputs["Fac"],
                 [(0.35, (0.75, 0.75, 0.75, 1)), (0.65, (1.0, 1.0, 1.0, 1))])
    return multiply_color(nodes, links, debris, grain)


def granite_lichen(nodes, links):
    base = ramp(nodes, links, noise(nodes, 4.0).outputs["Fac"],
                [(0.2, (0.075, 0.083, 0.094, 1)), (0.8, (0.150, 0.162, 0.178, 1))])
    voronoi = nodes.new("ShaderNodeTexVoronoi")
    voronoi.feature = "DISTANCE_TO_EDGE"
    voronoi.inputs["Scale"].default_value = 9.0
    cracks = ramp(nodes, links, voronoi.outputs["Distance"],
                  [(0.0, (0.30, 0.30, 0.32, 1)), (0.09, (1, 1, 1, 1))])
    cracked = multiply_color(nodes, links, base, cracks)
    lichen_mask = ramp(nodes, links, noise(nodes, 21.0, 9.0).outputs["Fac"],
                       [(0.60, (0, 0, 0, 1)), (0.70, (1, 1, 1, 1))])
    lichened = mix_color(nodes, links, lichen_mask, cracked, const(nodes, (0.150, 0.170, 0.095, 1)))
    grain = ramp(nodes, links, noise(nodes, 140.0, 3.0).outputs["Fac"],
                 [(0.3, (0.82, 0.82, 0.82, 1)), (0.7, (1.0, 1.0, 1.0, 1))])
    return multiply_color(nodes, links, lichened, grain)


def bark(nodes, links):
    coords = nodes.new("ShaderNodeTexCoord")
    mapping = nodes.new("ShaderNodeMapping")
    mapping.inputs["Scale"].default_value = (1.0, 14.0, 1.0)   # stretch noise into vertical ridges
    links.new(coords.outputs["UV"], mapping.inputs["Vector"])
    ridge_noise = nodes.new("ShaderNodeTexNoise")
    ridge_noise.inputs["Scale"].default_value = 9.0
    ridge_noise.inputs["Detail"].default_value = 7.0
    links.new(mapping.outputs["Vector"], ridge_noise.inputs["Vector"])
    base = ramp(nodes, links, ridge_noise.outputs["Fac"],
                [(0.30, (0.052, 0.036, 0.024, 1)), (0.72, (0.135, 0.098, 0.062, 1))])
    moss_mask = ramp(nodes, links, noise(nodes, 15.0, 8.0).outputs["Fac"],
                     [(0.58, (0, 0, 0, 1)), (0.70, (1, 1, 1, 1))])
    return mix_color(nodes, links, moss_mask, base, const(nodes, (0.055, 0.085, 0.038, 1)))


bake("forest_floor_albedo", forest_floor)
bake("granite_lichen_albedo", granite_lichen)
bake("wet_bark_albedo", bark)
print("ALL TEXTURES BAKED")
