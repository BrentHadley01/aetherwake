"""Normalize downloaded Tripo meshes and export runtime male/female GLBs."""
import math
import os

import bpy
from mathutils import Matrix, Vector

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DOWNLOADS = os.path.join(os.path.expanduser("~"), "Downloads")
MODELS = os.path.join(ROOT, "assets", "models")
SOURCES = (
    ("adult male 3d model.glb", "player_male.glb", "PlayerMale"),
    ("hyperrealistic female 3d model.glb", "player_female.glb", "PlayerFemale"),
)


for source_name, output_name, model_name in SOURCES:
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.gltf(filepath=os.path.join(DOWNLOADS, source_name))
    mesh = next(value for value in bpy.context.scene.objects if value.type == "MESH")
    mesh.name = model_name

    # Tripo uses X as depth and Y as arm span. Rotate into the game's
    # conventional X-width, Y-depth, Z-up space, normalize to 1.82 m and put
    # the origin exactly under the feet.
    mesh.rotation_euler[2] = -math.pi * 0.5
    bpy.context.view_layer.objects.active = mesh; mesh.select_set(True)
    bpy.ops.object.transform_apply(location=False, rotation=True, scale=False)
    minimum = min(vertex.co.z for vertex in mesh.data.vertices)
    maximum = max(vertex.co.z for vertex in mesh.data.vertices)
    uniform_scale = 1.82 / max(0.001, maximum - minimum)
    for vertex in mesh.data.vertices:
        vertex.co *= uniform_scale
        vertex.co.z -= minimum * uniform_scale

    # Lower the generated T-pose arms into a deformation-friendly A-pose.
    # Weighting by horizontal distance keeps shoulders smooth rather than
    # introducing a hard cut into the torso.
    shoulder_z = 1.48
    for vertex in mesh.data.vertices:
        side = 1.0 if vertex.co.x >= 0.0 else -1.0
        weight = max(0.0, min(1.0, (abs(vertex.co.x) - 0.20) / 0.22))
        if weight <= 0.0 or vertex.co.z < 0.72:
            continue
        angle = side * math.radians(98.0) * weight
        pivot = Vector((side * 0.22, 0.0, shoulder_z))
        relative = vertex.co - pivot
        vertex.co = pivot + Matrix.Rotation(angle, 4, "Y") @ relative

    mesh.data.update()
    for polygon in mesh.data.polygons: polygon.use_smooth = True
    for material in mesh.data.materials:
        material.name = model_name + "BasePBR"
        if material.use_nodes:
            shader = material.node_tree.nodes.get("Principled BSDF")
            if shader: shader.inputs["Roughness"].default_value = 0.72

    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.export_scene.gltf(filepath=os.path.join(MODELS, output_name), export_format="GLB", export_apply=True, export_yup=True)
    print("EXPORTED", output_name)
