"""Print structural metadata for a GLB supplied through AETHERWAKE_INSPECT_ASSET."""
import os
import bpy

path = os.environ["AETHERWAKE_INSPECT_ASSET"]
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=path)
meshes = [value for value in bpy.context.scene.objects if value.type == "MESH"]
armatures = [value for value in bpy.context.scene.objects if value.type == "ARMATURE"]
print("INSPECT", path)
print("MESHES", len(meshes), "VERTICES", sum(len(value.data.vertices) for value in meshes),
      "TRIANGLES", sum(len(value.data.loop_triangles) for value in meshes))
for mesh in meshes:
    mesh.data.calc_loop_triangles()
    corners = [mesh.matrix_world @ vector.co for vector in mesh.data.vertices]
    minimum = tuple(min(value[index] for value in corners) for index in range(3))
    maximum = tuple(max(value[index] for value in corners) for index in range(3))
    print("MESH", mesh.name, len(mesh.data.vertices), len(mesh.data.loop_triangles),
          "BOUNDS", minimum, maximum, "MATERIALS", [material.name for material in mesh.data.materials])
print("ARMATURES", len(armatures))
for armature in armatures:
    print("ARMATURE", armature.name, "BONES", len(armature.data.bones), [bone.name for bone in armature.data.bones][:30])
print("ACTIONS", [(action.name, tuple(action.frame_range)) for action in bpy.data.actions])
for image in bpy.data.images:
    print("IMAGE", image.name, tuple(image.size), image.file_format)
