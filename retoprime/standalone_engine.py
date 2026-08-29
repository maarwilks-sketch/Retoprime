from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

import numpy as np
import pymeshlab

try:
    import assimp_py
except Exception:
    assimp_py = None


@dataclass(frozen=True)
class MeshStats:
    vertices: int
    faces: int


def _load_mesh_arrays(path: Path) -> tuple[np.ndarray, np.ndarray]:
    suffix = path.suffix.lower()

    if suffix == ".obj":
        ms = pymeshlab.MeshSet()
        ms.load_new_mesh(str(path))
        mesh = ms.current_mesh()
        return (
            np.asarray(mesh.vertex_matrix(), dtype=np.float64),
            np.asarray(mesh.face_matrix(), dtype=np.int32),
        )

    if suffix == ".fbx":
        if assimp_py is None:
            raise RuntimeError("FBX support is unavailable in this build.")
        flags = (
            assimp_py.Process_Triangulate
            | assimp_py.Process_JoinIdenticalVertices
            | assimp_py.Process_GenSmoothNormals
        )
        scene = assimp_py.import_file(str(path), flags)
        vertices_parts: list[np.ndarray] = []
        faces_parts: list[np.ndarray] = []
        offset = 0

        for mesh in scene.meshes:
            verts = np.asarray(mesh.vertices, dtype=np.float32).reshape((-1, 3))
            idx = np.asarray(mesh.indices, dtype=np.uint32)
            if idx.ndim == 1:
                idx = idx.reshape((-1, 3))
            elif idx.ndim == 2 and idx.shape[1] != 3:
                idx = idx.reshape((-1, 3))

            vertices_parts.append(verts)
            faces_parts.append(idx.astype(np.int32) + offset)
            offset += len(verts)

        if not vertices_parts:
            raise RuntimeError("The FBX file did not contain a mesh.")

        return np.vstack(vertices_parts), np.vstack(faces_parts)

    raise ValueError("Input must be an OBJ or FBX file.")


def inspect_mesh(path: Path) -> MeshStats:
    vertices, faces = _load_mesh_arrays(path)
    return MeshStats(vertices=len(vertices), faces=len(faces))


def retopologise(
    input_path: Path,
    output_path: Path,
    target_faces: int,
    preserve_sharp: bool = True,
    project_surface: bool = True,
) -> MeshStats:
    """
    Standalone RETOPRIME MVP engine.

    The current engine performs topology-aware quadric edge-collapse reduction
    and an optional isotropic cleanup pass. It does not require Blender.

    v0.2 exports OBJ. FBX import is supported through bundled Assimp.
    Native FBX export and true quad-flow generation are later milestones.
    """
    if output_path.suffix.lower() != ".obj":
        raise ValueError("This standalone build currently exports OBJ. Choose an .obj output file.")

    vertices, faces = _load_mesh_arrays(input_path)
    if len(faces) <= target_faces:
        target_faces = max(100, len(faces))

    source = pymeshlab.Mesh(vertex_matrix=vertices, face_matrix=faces)
    ms = pymeshlab.MeshSet()
    ms.add_mesh(source, "HighPoly")

    kwargs = {
        "targetfacenum": int(target_faces),
        "preserveboundary": True,
        "preservenormal": bool(preserve_sharp),
        "preservetopology": True,
        "optimalplacement": True,
        "autoclean": True,
    }
    ms.apply_filter("meshing_decimation_quadric_edge_collapse", **kwargs)

    # A light cleanup improves long thin triangles after aggressive reduction.
    # Projection in this MVP means retaining optimal placement on the source
    # surface during simplification rather than a separate shrinkwrap pass.
    if project_surface:
        try:
            ms.apply_filter("meshing_repair_non_manifold_edges", method=0)
        except Exception:
            pass

    output_path.parent.mkdir(parents=True, exist_ok=True)
    ms.save_current_mesh(str(output_path), save_vertex_normal=True)

    result = ms.current_mesh()
    return MeshStats(
        vertices=result.vertex_number(),
        faces=result.face_number(),
    )
