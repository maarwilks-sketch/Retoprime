import os
import unittest
from pathlib import Path
from tempfile import TemporaryDirectory
from unittest.mock import patch


class CoreTests(unittest.TestCase):
    def test_validate_job_accepts_fbx_and_obj(self):
        from retoprime.core import RetopoJob

        for suffix in (".fbx", ".obj"):
            with self.subTest(suffix=suffix):
                job = RetopoJob(Path("model" + suffix), Path("out" + suffix), 10000)
                job.validate()

    def test_validate_job_rejects_unsupported_input(self):
        from retoprime.core import RetopoJob

        with self.assertRaisesRegex(ValueError, "FBX or OBJ"):
            RetopoJob(Path("model.stl"), Path("out.obj"), 10000).validate()

    def test_validate_job_rejects_invalid_target(self):
        from retoprime.core import RetopoJob

        with self.assertRaisesRegex(ValueError, "between 100 and 2,000,000"):
            RetopoJob(Path("model.obj"), Path("out.obj"), 99).validate()

    def test_default_output_adds_retopo_suffix(self):
        from retoprime.core import default_output_path

        self.assertEqual(default_output_path(Path("hero.fbx")), Path("hero_RETOPO.fbx"))

    def test_find_blender_uses_environment_override(self):
        from retoprime.core import find_blender

        with TemporaryDirectory() as folder:
            exe = Path(folder) / "blender.exe"
            exe.touch()
            with patch.dict(os.environ, {"RETOPRIME_BLENDER": str(exe)}):
                self.assertEqual(find_blender(), exe)

    def test_build_command_passes_job_settings_after_separator(self):
        from retoprime.core import RetopoJob, build_blender_command

        job = RetopoJob(
            Path("source.obj"), Path("result.fbx"), 25000,
            symmetry=True, preserve_sharp=False, project_surface=True,
        )
        command = build_blender_command(Path("blender.exe"), Path("worker.py"), job)
        self.assertEqual(command[:5], [
            str(Path("blender.exe")), "--background", "--python", str(Path("worker.py")), "--"
        ])
        self.assertIn("--target-faces=25000", command)
        self.assertIn("--symmetry=1", command)
        self.assertIn("--preserve-sharp=0", command)
        self.assertIn("--project-surface=1", command)


if __name__ == "__main__":
    unittest.main()
