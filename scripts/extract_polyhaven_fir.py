"""Extract the smallest fir_tree_01 mesh without downloading its 456 MB buffer.

Poly Haven's glTF stores three multi-million-triangle trees in one binary. This
tool range-downloads only buffer views referenced by variant C, rewrites their
offsets, and keeps only the diffuse materials needed by the runtime pipeline.
The resulting source is then decimated and packed by prepare_polyhaven_scans.py.
"""

import json
import os
import tempfile
import urllib.request


ASSET = "fir_tree_01"
GLTF_URL = "https://dl.polyhaven.org/file/ph-assets/Models/gltf/1k/fir_tree_01/fir_tree_01_1k.gltf"
BIN_URL = "https://dl.polyhaven.org/file/ph-assets/Models/gltf/8k/fir_tree_01/fir_tree_01.bin"
IMAGE_ROOT = "https://dl.polyhaven.org/file/ph-assets/Models/jpg/1k/fir_tree_01/"
OUTPUT = os.path.join(tempfile.gettempdir(), "aetherwake_polyhaven", "fir_tree_selected")


def fetch(url, start=None, end=None):
    headers = {"User-Agent": "Aetherwake asset preparation"}
    if start is not None:
        headers["Range"] = f"bytes={start}-{end}"
    with urllib.request.urlopen(urllib.request.Request(url, headers=headers)) as response:
        return response.read()


os.makedirs(os.path.join(OUTPUT, "textures"), exist_ok=True)
source = json.loads(fetch(GLTF_URL))
mesh = source["meshes"][2]  # fir_tree_01_c_LOD0: smallest official variant

used_accessors = []
for primitive in mesh["primitives"]:
    used_accessors.append(primitive["indices"])
    used_accessors.extend(primitive.get("attributes", {}).values())
used_accessors = sorted(set(used_accessors))
used_views = sorted({source["accessors"][index]["bufferView"] for index in used_accessors})

new_views = []
view_map = {}
binary = bytearray()
for old_index in used_views:
    view = source["bufferViews"][old_index]
    while len(binary) % 4:
        binary.append(0)
    offset = len(binary)
    start = view.get("byteOffset", 0)
    length = view["byteLength"]
    binary.extend(fetch(BIN_URL, start, start + length - 1))
    rebuilt = {key: value for key, value in view.items() if key not in ("buffer", "byteOffset")}
    rebuilt["buffer"] = 0
    rebuilt["byteOffset"] = offset
    view_map[old_index] = len(new_views)
    new_views.append(rebuilt)

new_accessors = []
accessor_map = {}
for old_index in used_accessors:
    accessor = dict(source["accessors"][old_index])
    accessor["bufferView"] = view_map[accessor["bufferView"]]
    accessor_map[old_index] = len(new_accessors)
    new_accessors.append(accessor)

used_materials = sorted({primitive["material"] for primitive in mesh["primitives"] if "material" in primitive})
material_map = {old: new for new, old in enumerate(used_materials)}
used_textures = []
new_materials = []
for old_index in used_materials:
    old = source["materials"][old_index]
    old_pbr = old.get("pbrMetallicRoughness", {})
    pbr = {
        "baseColorFactor": old_pbr.get("baseColorFactor", [1, 1, 1, 1]),
        "metallicFactor": 0.0,
        "roughnessFactor": 0.88,
    }
    if "baseColorTexture" in old_pbr:
        used_textures.append(old_pbr["baseColorTexture"]["index"])
    material = {"name": old.get("name", f"material_{old_index}"), "pbrMetallicRoughness": pbr}
    if old.get("doubleSided"):
        material["doubleSided"] = True
    if old.get("alphaMode"):
        material["alphaMode"] = old["alphaMode"]
        material["alphaCutoff"] = old.get("alphaCutoff", 0.5)
    new_materials.append(material)

used_textures = sorted(set(used_textures))
texture_map = {old: new for new, old in enumerate(used_textures)}
used_images = sorted({source["textures"][index]["source"] for index in used_textures})
image_map = {old: new for new, old in enumerate(used_images)}
new_images = []
for old_index in used_images:
    image = source["images"][old_index]
    filename = os.path.basename(image["uri"])
    with open(os.path.join(OUTPUT, "textures", filename), "wb") as handle:
        handle.write(fetch(IMAGE_ROOT + filename))
    new_images.append({"name": image.get("name", filename), "uri": f"textures/{filename}"})

new_textures = []
for old_index in used_textures:
    texture = source["textures"][old_index]
    new_textures.append({"source": image_map[texture["source"]]})
for new_index, old_index in enumerate(used_materials):
    old_pbr = source["materials"][old_index].get("pbrMetallicRoughness", {})
    if "baseColorTexture" in old_pbr:
        new_materials[new_index]["pbrMetallicRoughness"]["baseColorTexture"] = {
            "index": texture_map[old_pbr["baseColorTexture"]["index"]]
        }

primitives = []
for primitive in mesh["primitives"]:
    rebuilt = dict(primitive)
    rebuilt["indices"] = accessor_map[primitive["indices"]]
    rebuilt["attributes"] = {name: accessor_map[index] for name, index in primitive["attributes"].items()}
    if "material" in primitive:
        rebuilt["material"] = material_map[primitive["material"]]
    primitives.append(rebuilt)

result = {
    "asset": source["asset"],
    "scene": 0,
    "scenes": [{"nodes": [0]}],
    "nodes": [{"name": "fir_tree_01_c_LOD0", "mesh": 0}],
    "meshes": [{"name": mesh.get("name", "fir_tree_selected"), "primitives": primitives}],
    "buffers": [{"uri": "fir_tree_selected.bin", "byteLength": len(binary)}],
    "bufferViews": new_views,
    "accessors": new_accessors,
    "materials": new_materials,
    "textures": new_textures,
    "images": new_images,
}
with open(os.path.join(OUTPUT, "fir_tree_selected.bin"), "wb") as handle:
    handle.write(binary)
with open(os.path.join(OUTPUT, "fir_tree_selected.gltf"), "w", encoding="utf-8") as handle:
    json.dump(result, handle, separators=(",", ":"))
print(f"EXTRACTED {len(binary) / 1048576:.1f} MB to {OUTPUT}")
