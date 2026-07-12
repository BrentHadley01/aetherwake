"""Render an exported detail GLB for fast asset-art QA."""
import os

import bpy
from mathutils import Vector

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ASSET = os.environ.get("AETHERWAKE_PREVIEW_ASSET", "detail_wayfinder.glb")
is_tree = any(name in ASSET for name in ("birch", "pine", "spruce", "snag"))
is_landmark = "veiled_reach" in ASSET
target_height = 2.5 if is_landmark else 4.5 if is_tree else 1.4
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=os.path.join(ROOT, "assets", "models", ASSET))

# Neutral ground and a cool/warm studio rig expose silhouette and clipping.
bpy.ops.mesh.primitive_plane_add(size=20, location=(0, 0, -0.02))
ground = bpy.context.active_object
ground_material = bpy.data.materials.new("PreviewGround")
ground_material.diffuse_color = (0.025, 0.035, 0.045, 1.0)
ground.data.materials.append(ground_material)

def point_at(obj, target):
    obj.rotation_euler = (Vector(target) - obj.location).to_track_quat("-Z", "Y").to_euler()

bpy.ops.object.camera_add(location=(12.0, -18.0, 10.0) if is_landmark else (10.0, -15.0, 7.2) if is_tree else (4.6, -7.2, 3.1))
camera = bpy.context.active_object
point_at(camera, (0, 0, target_height))
bpy.context.scene.camera = camera
camera.data.lens = 58

for name, location, energy, color, size in (
    ("Key", (-4.0, -4.5, 6.0), 1100, (0.72, 0.84, 1.0), 4.0),
    ("Rim", (4.5, 2.5, 4.2), 900, (0.25, 0.55, 1.0), 3.0),
    ("Fill", (1.0, -2.0, 1.7), 450, (1.0, 0.58, 0.32), 2.0),
):
    bpy.ops.object.light_add(type="AREA", location=location)
    light = bpy.context.active_object; light.name = name; light.data.energy = energy; light.data.color = color; light.data.shape = "DISK"; light.data.size = size
    point_at(light, (0, 0, target_height))

scene = bpy.context.scene
scene.render.engine = "BLENDER_EEVEE"
scene.render.resolution_x = 800; scene.render.resolution_y = 800; scene.render.resolution_percentage = 100
scene.render.image_settings.file_format = "PNG"
scene.render.filepath = os.path.join(ROOT, "output", "review", os.path.splitext(ASSET)[0] + "_blender_preview.png")
scene.world = bpy.data.worlds.new("PreviewWorld")
scene.world.color = (0.008, 0.012, 0.02)
scene.render.film_transparent = False
bpy.ops.render.render(write_still=True)
