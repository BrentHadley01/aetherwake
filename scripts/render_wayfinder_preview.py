"""Render the exported Wayfinder GLB for fast character-art QA."""
import os

import bpy
from mathutils import Vector

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=os.path.join(ROOT, "assets", "models", "detail_wayfinder.glb"))

# Neutral ground and a cool/warm studio rig expose silhouette and clipping.
bpy.ops.mesh.primitive_plane_add(size=20, location=(0, 0, -0.02))
ground = bpy.context.active_object
ground_material = bpy.data.materials.new("PreviewGround")
ground_material.diffuse_color = (0.025, 0.035, 0.045, 1.0)
ground.data.materials.append(ground_material)

def point_at(obj, target):
    obj.rotation_euler = (Vector(target) - obj.location).to_track_quat("-Z", "Y").to_euler()

bpy.ops.object.camera_add(location=(4.6, -7.2, 3.1))
camera = bpy.context.active_object
point_at(camera, (0, 0, 1.4))
bpy.context.scene.camera = camera
camera.data.lens = 58

for name, location, energy, color, size in (
    ("Key", (-4.0, -4.5, 6.0), 1100, (0.72, 0.84, 1.0), 4.0),
    ("Rim", (4.5, 2.5, 4.2), 900, (0.25, 0.55, 1.0), 3.0),
    ("Fill", (1.0, -2.0, 1.7), 450, (1.0, 0.58, 0.32), 2.0),
):
    bpy.ops.object.light_add(type="AREA", location=location)
    light = bpy.context.active_object; light.name = name; light.data.energy = energy; light.data.color = color; light.data.shape = "DISK"; light.data.size = size
    point_at(light, (0, 0, 1.4))

scene = bpy.context.scene
scene.render.engine = "BLENDER_EEVEE"
scene.render.resolution_x = 800; scene.render.resolution_y = 800; scene.render.resolution_percentage = 100
scene.render.image_settings.file_format = "PNG"
scene.render.filepath = os.path.join(ROOT, "output", "review", "wayfinder_blender_preview.png")
scene.world = bpy.data.worlds.new("PreviewWorld")
scene.world.color = (0.008, 0.012, 0.02)
scene.render.film_transparent = False
bpy.ops.render.render(write_still=True)
