from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path


SUPPORTED_INPUT_FORMATS = {".fbx", ".obj"}
SUPPORTED_OUTPUT_FORMATS = {".obj"}


@dataclass(frozen=True)
class RetopoJob:
    input_path: Path
    output_path: Path
    target_faces: int
    preserve_sharp: bool = True
    project_surface: bool = True

    def validate(self) -> None:
        if self.input_path.suffix.lower() not in SUPPORTED_INPUT_FORMATS:
            raise ValueError("Input must be an FBX or OBJ file.")
        if self.output_path.suffix.lower() not in SUPPORTED_OUTPUT_FORMATS:
            raise ValueError("This standalone build currently exports OBJ.")
        if not 100 <= self.target_faces <= 2_000_000:
            raise ValueError("Target faces must be between 100 and 2,000,000.")


def default_output_path(input_path: Path) -> Path:
    return input_path.with_name(f"{input_path.stem}_RETOPO.obj")
