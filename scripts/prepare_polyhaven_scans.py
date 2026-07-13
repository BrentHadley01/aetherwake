"""Convert downloaded Poly Haven scan sources into compact runtime GLBs.

The source glTFs are downloaded from Poly Haven's public CC0 library. This
script recenters the six-rock collection, strips texture channels unsupported
by the current renderer, and embeds the remaining diffuse plate into each GLB.

Run with Blender:
  blender --background --python scripts/prepare_polyhaven_scans.py
"""

import os
import tempfile

import bpy


ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SOURCE = os.environ.get(
    "AETHERWAKE_SCAN_ROOT",
    os.path.join(tempfile.gettempdir(), "aetherwake_polyhaven"),
)
OUTPUT = os.path.join(ROOT, "assets", "models")


def reset_scene():
    bpy.ops.wm.read_factory_settings(use_empty=True)


def simplify_material_channels(objects, material_name):
    """Keep diffuse + scalar roughness; geometry carries the scan relief."""
    materials = {material for obj in objects if obj.type == "MESH" for material in obj.data.materials if material}
    for material in materials:
        material.name = material_name
        if not material.use_nodes:
            continue
        principled = next((node for node in material.node_tree.nodes if node.type == "BSDF_PRINCIPLED"), None)
        if not principled:
            continue
        for socket_name in ("Normal", "Metallic", "Roughness"):
            socket = principled.inputs.get(socket_name)
            if socket:
                for link in list(socket.links):
                    material.node_tree.links.remove(link)
        principled.inputs["Metallic"].default_value = 0.0
        principled.inputs["Roughness"].default_value = 0.88


def export_selected(path):
    bpy.ops.export_scene.gltf(
        filepath=path,
        export_format="GLB",
        use_selection=True,
        export_apply=True,
        export_texcoords=True,
        export_normals=True,
        export_materials="EXPORT",
        export_image_format="AUTO",
    )
    print("EXPORTED", path)


def ground_and_center(obj):
    """Bake a stable bottom-center origin for terrain instancing."""
    obj.location = (0.0, 0.0, 0.0)
    minimum = [min(vertex.co[axis] for vertex in obj.data.vertices) for axis in range(3)]
    maximum = [max(vertex.co[axis] for vertex in obj.data.vertices) for axis in range(3)]
    obj.location = (-(minimum[0] + maximum[0]) * 0.5, -(minimum[1] + maximum[1]) * 0.5, -minimum[2])
    bpy.context.view_layer.objects.active = obj
    obj.select_set(True)
    bpy.ops.object.transform_apply(location=True, rotation=False, scale=False)


def convert_rocks():
    reset_scene()
    source = os.path.join(SOURCE, "rock", "rock_moss_set_01_1k.gltf")
    bpy.ops.import_scene.gltf(filepath=source)
    rocks = sorted((obj for obj in bpy.context.scene.objects if obj.type == "MESH"), key=lambda obj: obj.name)
    simplify_material_channels(rocks, "ScannedMossRock")
    for index, rock in enumerate(rocks, 1):
        # The source node translations arrange the collection as a contact
        # sheet. Mesh-local coordinates are already centered for instancing.
        bpy.ops.object.select_all(action="DESELECT")
        ground_and_center(rock)
        bpy.ops.object.select_all(action="DESELECT")
        rock.select_set(True)
        bpy.context.view_layer.objects.active = rock
        export_selected(os.path.join(OUTPUT, f"detail_scan_rock_{index:02d}.glb"))


def convert_stump():
    reset_scene()
    source = os.path.join(SOURCE, "stump", "tree_stump_02_1k.gltf")
    bpy.ops.import_scene.gltf(filepath=source)
    meshes = [obj for obj in bpy.context.scene.objects if obj.type == "MESH"]
    simplify_material_channels(meshes, "ScannedWetBark")
    for obj in meshes:
        bpy.ops.object.select_all(action="DESELECT")
        ground_and_center(obj)
    bpy.ops.object.select_all(action="DESELECT")
    for obj in meshes:
        obj.select_set(True)
    if meshes:
        bpy.context.view_layer.objects.active = meshes[0]
    export_selected(os.path.join(OUTPUT, "detail_scan_stump.glb"))


def convert_single(source_folder, source_name, output_name, material_name):
    reset_scene()
    source = os.path.join(SOURCE, source_folder, source_name)
    bpy.ops.import_scene.gltf(filepath=source)
    meshes = [obj for obj in bpy.context.scene.objects if obj.type == "MESH"]
    simplify_material_channels(meshes, material_name)
    for obj in meshes:
        bpy.ops.object.select_all(action="DESELECT")
        ground_and_center(obj)
    bpy.ops.object.select_all(action="DESELECT")
    for obj in meshes:
        obj.select_set(True)
    if meshes:
        bpy.context.view_layer.objects.active = meshes[0]
    export_selected(os.path.join(OUTPUT, output_name))


def convert_hero_fir():
    reset_scene()
    source = os.path.join(SOURCE, "fir_tree_selected", "fir_tree_selected.gltf")
    if not os.path.exists(source):
        print("SKIPPED hero fir; run extract_polyhaven_fir.py first")
        return
    bpy.ops.import_scene.gltf(filepath=source)
    meshes = [obj for obj in bpy.context.scene.objects if obj.type == "MESH"]
    bpy.ops.object.select_all(action="DESELECT")
    for obj in meshes:
        obj.select_set(True)
    bpy.context.view_layer.objects.active = meshes[0]
    bpy.ops.object.join()
    tree = bpy.context.active_object
    for material in tree.data.materials:
        if material:
            material.name = "HeroFirNeedleFoliage" if "twig" in material.name.lower() else "HeroFirBark"
    target_triangles = 85000
    current_triangles = max(1, len(tree.data.polygons))
    if current_triangles > target_triangles:
        modifier = tree.modifiers.new("RuntimeTriangleBudget", "DECIMATE")
        modifier.ratio = target_triangles / current_triangles
        modifier.use_collapse_triangulate = True
        bpy.ops.object.modifier_apply(modifier=modifier.name)
    bpy.ops.object.select_all(action="DESELECT")
    ground_and_center(tree)
    bpy.ops.object.select_all(action="DESELECT")
    tree.select_set(True)
    bpy.context.view_layer.objects.active = tree
    export_selected(os.path.join(OUTPUT, "detail_scan_hero_fir.glb"))


convert_rocks()
convert_stump()
convert_single("fern_02", "fern_02_1k.gltf", "detail_scan_fern.glb", "ScannedFernFoliage")
convert_single("dead_tree_trunk", "dead_tree_trunk_1k.gltf", "detail_scan_dead_trunk.glb", "ScannedWetBark")
convert_hero_fir()
print("ALL POLY HAVEN SCANS PREPARED")
