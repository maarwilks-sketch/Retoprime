import unittest
from pathlib import Path


class CoreTests(unittest.TestCase):
    def test_validate_job_accepts_fbx_input_obj_output(self):
        from retoprime.core import RetopoJob

        for suffix in (".fbx", ".obj"):
            with self.subTest(suffix=suffix):
                job = RetopoJob(Path("model" + suffix), Path("out.obj"), 10000)
                job.validate()

    def test_validate_job_rejects_unsupported_input(self):
        from retoprime.core import RetopoJob

        with self.assertRaisesRegex(ValueError, "FBX or OBJ"):
            RetopoJob(Path("model.stl"), Path("out.obj"), 10000).validate()

    def test_validate_job_rejects_non_obj_output(self):
        from retoprime.core import RetopoJob

        with self.assertRaisesRegex(ValueError, "exports OBJ"):
            RetopoJob(Path("model.fbx"), Path("out.fbx"), 10000).validate()

    def test_validate_job_rejects_invalid_target(self):
        from retoprime.core import RetopoJob

        with self.assertRaisesRegex(ValueError, "between 100 and 2,000,000"):
            RetopoJob(Path("model.obj"), Path("out.obj"), 99).validate()

    def test_default_output_adds_retopo_suffix_and_obj_extension(self):
        from retoprime.core import default_output_path

        self.assertEqual(default_output_path(Path("hero.fbx")), Path("hero_RETOPO.obj"))
        self.assertEqual(default_output_path(Path("hero.obj")), Path("hero_RETOPO.obj"))


if __name__ == "__main__":
    unittest.main()
