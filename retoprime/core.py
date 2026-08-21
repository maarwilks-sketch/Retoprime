from __future__ import annotations

import os
import shutil
from dataclasses import dataclass
from pathlib import Path


SUPPORTED_FORMATS = {".fbx", ".obj"}


@dataclass(frozen=True)
class RetopoJob:
    input_path: Path
    output_path: Path
    target_faces: int
    symmetry: bool = False
    preserve_sharp: bool = True
    project_surface: bool = True

    def validate(self) -> None:
        if self.input_path.suffix.lower() not in SUPPORTED_FORMATS:
            raise ValueError("Input must be an FBX or OBJ file.")
        if self.output_path.suffix.lower() not in SUPPORTED_FORMATS:
            raise ValueError("Output must be an FBX or OBJ file.")
        if not 100 <= self.target_faces <= 2_000_000:
            raise ValueError("Target faces must be between 100 and 2,000,000.")


def default_output_path(input_path: Path) -> Path:
    return input_path.with_name(f"{input_path.stem}_RETOPO{input_path.suffix}")


def find_blender() -> Path | None:
    override = os.environ.get("RETOPRIME_BLENDER")
    if override and Path(override).is_file():
        return Path(override)

    executable = shutil.which("blender") or shutil.which("blender.exe")
    if executable:
        return Path(executable)

    roots = [Path(os.environ.get("PROGRAMFILES", "C:/Program Files")) / "Blender Foundation"]
    for root in roots:
        if root.is_dir():
            candidates = sorted(root.glob("Blender */blender.exe"), reverse=True)
            if candidates:
                return candidates[0]
    return None


def build_blender_command(blender: Path, worker: Path, job: RetopoJob) -> list[str]:
    job.validate()
    return [
        str(blender), "--background", "--python", str(worker), "--",
        f"--input={job.input_path.resolve()}",
        f"--output={job.output_path.resolve()}",
        f"--target-faces={job.target_faces}",
        f"--symmetry={int(job.symmetry)}",
        f"--preserve-sharp={int(job.preserve_sharp)}",
        f"--project-surface={int(job.project_surface)}",
    ]
