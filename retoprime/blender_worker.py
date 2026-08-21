"""Executed by Blender in background mode. This file intentionally imports bpy."""
import argparse
import sys
from pathlib import Path

import bpy


def arguments():
    raw = sys.argv[sys.argv.index("--") + 1:]
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--target-faces", required=True, type=int)
    parser.add_argument("--symmetry", type=int, default=0)
    parser.add_argument("--preserve-sharp", type=int, default=1)
    parser.add_argument("--project-surface", type=int, default=1)
    return parser.parse_args(raw)


def clear_scene():
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete(use_global=False)


def import_mesh(path: Path):
    if path.suffix.lower() == ".fbx":
        bpy.ops.import_scene.fbx(filepath=str(path))
    else:
        bpy.ops.wm.obj_import(filepath=str(path))
    meshes = [obj for obj in bpy.context.selected_objects if obj.type == "MESH"]
    if not meshes:
        raise RuntimeError("The file did not contain a mesh.")
    bpy.context.view_layer.objects.active = meshes[0]
    for obj in meshes:
        obj.select_set(True)
    if len(meshes) > 1:
        bpy.ops.object.join()
    return bpy.context.active_object


def retopologise(source, args):
    source.name = source.name + "_HIGH"
    source.hide_render = True
    bpy.ops.object.duplicate(linked=False)
    result = bpy.context.active_object
    result.name = source.name.removesuffix("_HIGH") + "_RETOPO"
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    bpy.ops.object.quadriflow_remesh(
        target_faces=args.target_faces,
        use_mesh_symmetry=bool(args.symmetry),
        preserve_sharp=bool(args.preserve_sharp),
        preserve_boundary=True,
        smooth_normals=True,
        mode="FACES",
    )
    if args.project_surface:
        modifier = result.modifiers.new("RETOPRIME Projection", "SHRINKWRAP")
        modifier.target = source
        modifier.wrap_method = "NEAREST_SURFACEPOINT"
        bpy.context.view_layer.objects.active = result
        bpy.ops.object.modifier_apply(modifier=modifier.name)
    return result


def export_mesh(result, path: Path):
    bpy.ops.object.select_all(action="DESELECT")
    result.select_set(True)
    bpy.context.view_layer.objects.active = result
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.suffix.lower() == ".fbx":
        bpy.ops.export_scene.fbx(filepath=str(path), use_selection=True)
    else:
        bpy.ops.wm.obj_export(filepath=str(path), export_selected_objects=True)


def main():
    args = arguments()
    clear_scene()
    source = import_mesh(Path(args.input))
    result = retopologise(source, args)
    export_mesh(result, Path(args.output))
    print(f"RETOPRIME_COMPLETE:{args.output}")


if __name__ == "__main__":
    main()
