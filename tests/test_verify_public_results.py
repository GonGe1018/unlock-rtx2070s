import json
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
TOOL = ROOT / "tools" / "verify_public_results.py"
RESULTS = ROOT / "results"


class VerifyPublicResultsTests(unittest.TestCase):
    def test_published_results_pass(self):
        run = subprocess.run(
            ["python3", str(TOOL), "--results-dir", str(RESULTS)],
            check=True,
            capture_output=True,
            text=True,
        )
        report = json.loads(run.stdout)
        self.assertTrue(report["verified"])
        self.assertEqual(report["checked_samples"], 21)
        self.assertEqual(report["checked_cublas_batches"], 28)
        self.assertAlmostEqual(
            report["cublas_gemm"]["unlocked_over_locked"]["4096"],
            1.63124334,
            places=8,
        )

    def test_changed_sample_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            target = Path(directory)
            for source in RESULTS.iterdir():
                if source.suffix in {".json", ".jsonl"}:
                    (target / source.name).write_bytes(source.read_bytes())
            path = target / "rtx2070s-locked-tensor.jsonl"
            text = path.read_text()
            path.write_text(text.replace('"max_abs_error":0.0',
                                         '"max_abs_error":1.0', 1))
            run = subprocess.run(
                ["python3", str(TOOL), "--results-dir", str(target)],
                capture_output=True,
                text=True,
            )
            self.assertNotEqual(run.returncode, 0)

    def test_changed_cublas_batch_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            target = Path(directory)
            for source in RESULTS.iterdir():
                if source.suffix in {".json", ".jsonl"}:
                    (target / source.name).write_bytes(source.read_bytes())
            path = target / "rtx2070s-locked-cublas.jsonl"
            text = path.read_text()
            path.write_text(text.replace('"tflops":39.084371',
                                         '"tflops":39.184371', 1))
            run = subprocess.run(
                ["python3", str(TOOL), "--results-dir", str(target)],
                capture_output=True,
                text=True,
            )
            self.assertNotEqual(run.returncode, 0)


if __name__ == "__main__":
    unittest.main()
