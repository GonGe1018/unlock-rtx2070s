#!/usr/bin/env python3
"""Recompute and verify the sanitized RTX 2070 SUPER public A/B results."""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import statistics
import sys
from pathlib import Path


FILES = {
    "locked": (
        "rtx2070s-locked-tensor.jsonl",
        "8ca5ea30de13b7c9052266aefb117c115b666a5a98cdc741af1d0f37bc4e5f38",
    ),
    "unlocked_run_1": (
        "rtx2070s-unlocked-tensor-run1.jsonl",
        "b63c7cf5cb34fbbc2cfbb50ae39f2c7f349d994aaff7363c1a79fdd332a17d84",
    ),
    "unlocked_run_2": (
        "rtx2070s-unlocked-tensor-run2.jsonl",
        "e4e5e45a2b844801d63545342081458e49e25b04bf686f769e79256bddd8405f",
    ),
}

EXPECTED_MEDIANS = {
    "locked": (32.4437, 511.6813),
    "unlocked_run_1": (65.1408, 1021.9858),
    "unlocked_run_2": (64.5049, 1021.9858),
}

CUBLAS_FILES = {
    "locked": (
        "rtx2070s-locked-cublas.jsonl",
        "629d9e37f77c100f71d15792911321b6659254dd91bd27407baa15aa4224c9de",
    ),
    "unlocked": (
        "rtx2070s-unlocked-cublas.jsonl",
        "f94acf774a2779624bee4e3f4e0485bcad6b52d6d83d7fa72e9ba50539d7356b",
    ),
}

CUBLAS_EXPECTED_MEDIANS = {
    "locked": {
        4096: (3.515529, 39.094819),
        8192: (27.603284, 39.832639),
    },
    "unlocked": {
        4096: (2.155122, 63.773163),
        8192: (16.741280, 65.676676),
    },
}


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def require(condition: bool, message: str) -> None:
    if not condition:
        raise ValueError(message)


def load_run(path: Path) -> tuple[dict[str, object], list[dict[str, object]]]:
    records = [json.loads(line) for line in path.read_text().splitlines() if line]
    require(len(records) == 8, f"{path}: expected one header and seven samples")
    header, samples = records[0], records[1:]
    expected_header = {
        "gpu": "NVIDIA GeForce RTX 2070 SUPER",
        "bdf": "0000:01:00.0",
        "sms": 40,
        "sm": 75,
        "driver_api": 13030,
        "threads_per_block": 1024,
        "rounds": 2048,
        "reported_clock_khz": 1905000,
    }
    require(header == expected_header, f"{path}: target/header mismatch")
    require([sample.get("sample") for sample in samples] == list(range(7)),
            f"{path}: sample sequence mismatch")
    for sample in samples:
        require(sample.get("test") == "mma_f16_acc_fp32",
                f"{path}: workload mismatch")
        require(sample.get("max_abs_error") == 0.0,
                f"{path}: nonzero numerical error")
        require(sample.get("unique_sms") == 40,
                f"{path}: incomplete SM coverage")
        for field in ("ms", "tflops", "median_sm_cycles",
                      "flops_per_sm_cycle"):
            value = sample.get(field)
            require(isinstance(value, (int, float)) and math.isfinite(value)
                    and value > 0, f"{path}: invalid {field}")
    return header, samples


def load_cublas_run(
    path: Path, label: str
) -> dict[int, dict[str, object]]:
    records = [json.loads(line) for line in path.read_text().splitlines() if line]
    require(len(records) == 19,
            f"{path}: expected one header and nine records per size")
    header = records[0]
    expected_header = {
        "schema": "rtx2070s-cublas-gemm/1",
        "label": label,
        "gpu": "NVIDIA GeForce RTX 2070 SUPER",
        "bdf": "0000:01:00.0",
        "sms": 40,
        "sm": 75,
        "driver_api": 13030,
        "cublas_version": 120002,
        "reported_clock_khz": 1905000,
        "input": "FP16",
        "output": "FP32",
        "accumulation": "FP32",
        "warmup_seconds": 2.0,
        "batches": 7,
        "operations_per_batch": 30,
    }
    require(header == expected_header, f"{path}: cuBLAS target/header mismatch")

    result: dict[int, dict[str, object]] = {}
    for n in (4096, 8192):
        warmups = [record for record in records
                   if record.get("phase") == "warmup"
                   and record.get("n") == n]
        require(len(warmups) == 1, f"{path}: wrong warm-up count for n={n}")
        warmup = warmups[0]
        require(isinstance(warmup.get("operations"), int)
                and int(warmup["operations"]) > 0,
                f"{path}: invalid warm-up operations for n={n}")
        require(isinstance(warmup.get("elapsed_s"), (int, float))
                and math.isfinite(float(warmup["elapsed_s"]))
                and float(warmup["elapsed_s"]) >= 2.0,
                f"{path}: short warm-up for n={n}")

        samples = [record for record in records
                   if record.get("test") == "cublas_gemm_f16_f32acc"
                   and record.get("n") == n]
        require([sample.get("batch") for sample in samples] == list(range(7)),
                f"{path}: cuBLAS batch sequence mismatch for n={n}")
        for sample in samples:
            require(sample.get("operations") == 30,
                    f"{path}: cuBLAS operation count mismatch for n={n}")
            for field in ("ms_per_gemm", "tflops"):
                value = sample.get(field)
                require(isinstance(value, (int, float))
                        and math.isfinite(float(value)) and float(value) > 0,
                        f"{path}: invalid cuBLAS {field} for n={n}")
            calculated = 2.0 * n ** 3 / (
                float(sample["ms_per_gemm"]) * 1.0e9
            )
            require(close(calculated, float(sample["tflops"]), 2e-5),
                    f"{path}: cuBLAS TFLOPS arithmetic mismatch for n={n}")

        summaries = [record for record in records
                     if record.get("summary") == "cublas_gemm_f16_f32acc"
                     and record.get("n") == n]
        require(len(summaries) == 1, f"{path}: wrong summary count for n={n}")
        summary = summaries[0]
        median_ms = statistics.median(
            float(sample["ms_per_gemm"]) for sample in samples
        )
        median_tflops = statistics.median(
            float(sample["tflops"]) for sample in samples
        )
        require(close(float(summary["median_ms"]), median_ms, 5e-7),
                f"{path}: cuBLAS time median mismatch for n={n}")
        require(close(float(summary["median_tflops"]), median_tflops, 5e-7),
                f"{path}: cuBLAS TFLOPS median mismatch for n={n}")
        require(close(float(summary["min_tflops"]),
                      min(float(sample["tflops"]) for sample in samples),
                      5e-7), f"{path}: cuBLAS minimum mismatch for n={n}")
        require(close(float(summary["max_tflops"]),
                      max(float(sample["tflops"]) for sample in samples),
                      5e-7), f"{path}: cuBLAS maximum mismatch for n={n}")
        require(summary.get("checked_outputs") == 4,
                f"{path}: cuBLAS sampled-output count mismatch for n={n}")
        require(summary.get("max_abs_error") == 0.0,
                f"{path}: cuBLAS sampled-output error for n={n}")
        result[n] = {
            "median_ms": median_ms,
            "median_tflops": median_tflops,
            "min_tflops": min(float(sample["tflops"]) for sample in samples),
            "max_tflops": max(float(sample["tflops"]) for sample in samples),
        }
    return result


def close(actual: float, expected: float, tolerance: float = 1e-8) -> bool:
    return math.isclose(actual, expected, rel_tol=0.0, abs_tol=tolerance)


def verify(results_dir: Path) -> dict[str, object]:
    summary: dict[str, object] = {}
    for label, (filename, expected_hash) in FILES.items():
        path = results_dir / filename
        require(path.is_file(), f"missing {path}")
        require(digest(path) == expected_hash, f"{path}: SHA-256 mismatch")
        _, samples = load_run(path)
        median_tflops = statistics.median(float(s["tflops"]) for s in samples)
        median_cycle = statistics.median(
            float(s["flops_per_sm_cycle"]) for s in samples
        )
        expected_tflops, expected_cycle = EXPECTED_MEDIANS[label]
        require(close(median_tflops, expected_tflops, 5e-5),
                f"{path}: TFLOPS median mismatch")
        require(close(median_cycle, expected_cycle, 5e-5),
                f"{path}: cycle median mismatch")
        summary[label] = {
            "sha256": expected_hash,
            "samples": len(samples),
            "median_tflops": median_tflops,
            "median_flops_per_sm_cycle": median_cycle,
            "min_tflops": min(float(s["tflops"]) for s in samples),
            "max_tflops": max(float(s["tflops"]) for s in samples),
        }

    locked = summary["locked"]
    first = summary["unlocked_run_1"]
    second = summary["unlocked_run_2"]
    assert isinstance(locked, dict) and isinstance(first, dict)
    assert isinstance(second, dict)
    tflops_ratio = float(first["median_tflops"]) / float(
        locked["median_tflops"]
    )
    cycle_ratio = float(first["median_flops_per_sm_cycle"]) / float(
        locked["median_flops_per_sm_cycle"]
    )
    repeat_tflops_delta = (
        float(second["median_tflops"]) / float(first["median_tflops"]) - 1
    ) * 100
    repeat_cycle_delta = (
        float(second["median_flops_per_sm_cycle"])
        / float(first["median_flops_per_sm_cycle"])
        - 1
    ) * 100
    require(close(tflops_ratio, 2.00781045, 5e-9), "TFLOPS ratio mismatch")
    require(close(cycle_ratio, 1.99730926, 5e-9), "cycle ratio mismatch")

    cublas_summary: dict[str, object] = {}
    for label, (filename, expected_hash) in CUBLAS_FILES.items():
        path = results_dir / filename
        require(path.is_file(), f"missing {path}")
        require(digest(path) == expected_hash, f"{path}: SHA-256 mismatch")
        sizes = load_cublas_run(path, label)
        for n, (expected_ms, expected_tflops) in (
            CUBLAS_EXPECTED_MEDIANS[label].items()
        ):
            require(close(float(sizes[n]["median_ms"]), expected_ms, 5e-7),
                    f"{path}: expected cuBLAS time mismatch for n={n}")
            require(close(float(sizes[n]["median_tflops"]),
                          expected_tflops, 5e-7),
                    f"{path}: expected cuBLAS TFLOPS mismatch for n={n}")
        cublas_summary[label] = {
            "sha256": expected_hash,
            "sizes": {str(n): values for n, values in sizes.items()},
        }

    cublas_locked = cublas_summary["locked"]
    cublas_unlocked = cublas_summary["unlocked"]
    assert isinstance(cublas_locked, dict) and isinstance(cublas_unlocked, dict)
    locked_sizes = cublas_locked["sizes"]
    unlocked_sizes = cublas_unlocked["sizes"]
    assert isinstance(locked_sizes, dict) and isinstance(unlocked_sizes, dict)
    cublas_ratios = {
        n: float(unlocked_sizes[str(n)]["median_tflops"])
        / float(locked_sizes[str(n)]["median_tflops"])
        for n in (4096, 8192)
    }
    require(close(cublas_ratios[4096], 1.63124334, 5e-9),
            "cuBLAS 4096 ratio mismatch")
    require(close(cublas_ratios[8192], 1.64881559, 5e-9),
            "cuBLAS 8192 ratio mismatch")

    validation_path = results_dir / "rtx2070s-field-validation.json"
    validation = json.loads(validation_path.read_text())
    benchmark = validation["benchmark"]
    require(close(float(benchmark["unlocked_run_1_over_locked_tflops"]),
                  tflops_ratio, 5e-9), "field summary TFLOPS ratio mismatch")
    require(close(float(benchmark[
        "unlocked_run_1_over_locked_flops_per_sm_cycle"
    ]), cycle_ratio, 5e-9), "field summary cycle ratio mismatch")
    require(benchmark["all_21_samples_max_abs_error"] == 0.0,
            "field summary numerical status mismatch")
    require(benchmark["all_21_samples_unique_sms"] == 40,
            "field summary SM coverage mismatch")
    cublas_validation = validation["cublas_gemm"]
    require(cublas_validation["binary_sha256"] ==
            "0c6c5dd6e908e9e15d98baf7b74fa9f8c2d61f3a47a0dc85103081567853f580",
            "field summary cuBLAS binary mismatch")
    for label in ("locked", "unlocked"):
        recorded = cublas_validation[label]
        current = cublas_summary[label]
        require(recorded["sha256"] == current["sha256"],
                f"field summary cuBLAS hash mismatch for {label}")
        for n in (4096, 8192):
            require(close(float(recorded["results"][str(n)]["median_tflops"]),
                          float(current["sizes"][str(n)]["median_tflops"]),
                          5e-7),
                    f"field summary cuBLAS median mismatch for {label} n={n}")
    for n in (4096, 8192):
        require(close(float(cublas_validation["unlocked_over_locked"][str(n)]),
                      cublas_ratios[n], 5e-9),
                f"field summary cuBLAS ratio mismatch for n={n}")
    require(cublas_validation["all_sampled_outputs_max_abs_error"] == 0.0,
            "field summary cuBLAS numerical status mismatch")

    return {
        "schema_version": 1,
        "verified": True,
        "runs": summary,
        "comparison": {
            "unlocked_run_1_over_locked_tflops": tflops_ratio,
            "unlocked_run_1_over_locked_flops_per_sm_cycle": cycle_ratio,
            "unlocked_run_2_vs_run_1_tflops_percent": repeat_tflops_delta,
            "unlocked_run_2_vs_run_1_flops_per_sm_cycle_percent":
                repeat_cycle_delta,
        },
        "cublas_gemm": {
            "runs": cublas_summary,
            "unlocked_over_locked": {
                str(n): cublas_ratios[n] for n in (4096, 8192)
            },
        },
        "checked_samples": 21,
        "checked_cublas_batches": 28,
        "maximum_absolute_error": 0.0,
        "unique_sms_each_sample": 40,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--results-dir",
        type=Path,
        default=Path(__file__).resolve().parents[1] / "results",
    )
    args = parser.parse_args()
    try:
        result = verify(args.results_dir)
    except (OSError, ValueError, KeyError, TypeError, json.JSONDecodeError) as error:
        print(f"public result verification failed: {error}", file=sys.stderr)
        return 1
    print(json.dumps(result, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
