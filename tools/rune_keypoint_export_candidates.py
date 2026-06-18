#!/usr/bin/env python3
"""Export weak-label candidate samples from rune diagnostic JSONL files."""

from __future__ import annotations

import argparse
import csv
import json
import shutil
from pathlib import Path
from typing import Any


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Export rune keypoint weak-label candidate samples."
    )
    parser.add_argument(
        "--input",
        required=True,
        help="Diagnostic run directory or batch directory containing */frames.jsonl.",
    )
    parser.add_argument(
        "--output",
        required=True,
        help="Output directory for candidate samples.",
    )
    parser.add_argument(
        "--copy-review-images",
        action="store_true",
        help="Copy already-saved diagnostic images that match exported frames.",
    )
    parser.add_argument(
        "--max-review-images",
        type=int,
        default=300,
        help="Maximum review images to copy when --copy-review-images is enabled.",
    )
    return parser.parse_args()


def find_frame_files(input_path: Path) -> list[Path]:
    if input_path.is_file() and input_path.name == "frames.jsonl":
        return [input_path]

    direct = input_path / "frames.jsonl"
    if direct.exists():
        return [direct]

    return sorted(input_path.glob("*/frames.jsonl"))


def load_image_index(run_dir: Path) -> dict[int, Path]:
    image_index = run_dir / "image_index.csv"
    if not image_index.exists():
        return {}

    result: dict[int, Path] = {}
    with image_index.open("r", newline="") as file:
        reader = csv.DictReader(file)
        for row in reader:
            try:
                frame = int(row.get("frame", ""))
            except ValueError:
                continue
            image = row.get("image", "")
            if image:
                result[frame] = run_dir / image
    return result


def has_valid_contours(active_fan: dict[str, Any]) -> bool:
    contours = active_fan.get("contour_points", [])
    if not isinstance(contours, list) or not contours:
        return False

    for contour in contours:
        if isinstance(contour, list) and len(contour) >= 6:
            return True
    return False


def contour_point_count(active_fan: dict[str, Any]) -> int:
    total = 0
    for contour in active_fan.get("contour_points", []):
        if isinstance(contour, list):
            total += len(contour)
    return total


def is_candidate(frame_data: dict[str, Any], active_fan: dict[str, Any]) -> bool:
    if frame_data.get("status") != "ok":
        return False
    if frame_data.get("used_vanish_update"):
        return False
    if len(active_fan.get("corners", [])) != 6:
        return False
    if len(active_fan.get("pnp_points", [])) != 6:
        return False
    if not has_valid_contours(active_fan):
        return False
    return True


def make_sample_id(video: str, frame: int, fan_idx: int) -> str:
    stem = Path(video).stem
    safe_stem = "".join(ch if ch.isalnum() or ch in "._-" else "_" for ch in stem)
    return f"{safe_stem}_frame_{frame:06d}_fan_{fan_idx:02d}"


def write_json_line(file, data: dict[str, Any]) -> None:
    file.write(json.dumps(data, ensure_ascii=False, separators=(",", ":")))
    file.write("\n")


def export_candidates(
    frame_files: list[Path],
    output_dir: Path,
    copy_review_images: bool,
    max_review_images: int,
) -> dict[str, Any]:
    output_dir.mkdir(parents=True, exist_ok=True)
    review_dir = output_dir / "review_images"
    if copy_review_images:
        review_dir.mkdir(parents=True, exist_ok=True)

    samples_path = output_dir / "samples.jsonl"
    index_path = output_dir / "index.csv"
    summary_path = output_dir / "summary.csv"

    run_image_indexes = {frame_file.parent: load_image_index(frame_file.parent) for frame_file in frame_files}

    total_frames = 0
    total_ok = 0
    total_active_frames = 0
    total_samples = 0
    copied_images = 0
    per_run: dict[str, dict[str, int]] = {}

    with samples_path.open("w") as samples_file, index_path.open("w", newline="") as index_file:
        index_writer = csv.DictWriter(
            index_file,
            fieldnames=[
                "sample_id",
                "video",
                "frame",
                "fan_idx",
                "corner_count",
                "pnp_point_count",
                "contour_point_count",
                "source_run",
                "review_image",
            ],
        )
        index_writer.writeheader()

        for frame_file in frame_files:
            run_dir = frame_file.parent
            run_name = run_dir.name
            per_run[run_name] = {
                "frames": 0,
                "ok": 0,
                "active_frames": 0,
                "samples": 0,
                "review_images": 0,
            }
            image_index = run_image_indexes.get(run_dir, {})

            with frame_file.open("r") as file:
                for line_number, line in enumerate(file, start=1):
                    line = line.strip()
                    if not line:
                        continue

                    total_frames += 1
                    per_run[run_name]["frames"] += 1
                    try:
                        frame_data = json.loads(line)
                    except json.JSONDecodeError as exc:
                        raise RuntimeError(f"Bad JSON in {frame_file}:{line_number}: {exc}") from exc

                    if frame_data.get("status") == "ok":
                        total_ok += 1
                        per_run[run_name]["ok"] += 1

                    active_fans = frame_data.get("active_fans", [])
                    if active_fans:
                        total_active_frames += 1
                        per_run[run_name]["active_frames"] += 1

                    video = frame_data.get("video", "")
                    frame = int(frame_data.get("frame", -1))
                    for fan_idx, active_fan in enumerate(active_fans):
                        if not is_candidate(frame_data, active_fan):
                            continue

                        sample_id = make_sample_id(video, frame, fan_idx)
                        review_image = ""
                        source_image = image_index.get(frame)
                        if copy_review_images and source_image and source_image.exists() and copied_images < max_review_images:
                            image_name = f"{sample_id}{source_image.suffix}"
                            target_image = review_dir / image_name
                            shutil.copy2(source_image, target_image)
                            review_image = str(target_image.relative_to(output_dir))
                            copied_images += 1
                            per_run[run_name]["review_images"] += 1

                        sample = {
                            "sample_id": sample_id,
                            "source": {
                                "video": video,
                                "frame": frame,
                                "frame_time_ms": frame_data.get("frame_time_ms", 0),
                                "source_run": run_name,
                                "source_frames_jsonl": str(frame_file),
                            },
                            "diagnostics": {
                                "stage_time_ms": frame_data.get("stage_time_ms", {}),
                                "candidates": frame_data.get("candidates", {}),
                                "binary_nonzero_ratio": frame_data.get("binary_nonzero_ratio", 0),
                            },
                            "label": {
                                "center": active_fan.get("center", []),
                                "direction": active_fan.get("direction", []),
                                "width": active_fan.get("width", 0),
                                "height": active_fan.get("height", 0),
                                "corners": active_fan.get("corners", []),
                                "pnp_points": active_fan.get("pnp_points", []),
                                "contour_points": active_fan.get("contour_points", []),
                            },
                            "review_image": review_image,
                        }
                        write_json_line(samples_file, sample)

                        total_samples += 1
                        per_run[run_name]["samples"] += 1
                        index_writer.writerow(
                            {
                                "sample_id": sample_id,
                                "video": video,
                                "frame": frame,
                                "fan_idx": fan_idx,
                                "corner_count": len(active_fan.get("corners", [])),
                                "pnp_point_count": len(active_fan.get("pnp_points", [])),
                                "contour_point_count": contour_point_count(active_fan),
                                "source_run": run_name,
                                "review_image": review_image,
                            }
                        )

    with summary_path.open("w", newline="") as summary_file:
        writer = csv.DictWriter(
            summary_file,
            fieldnames=["run", "frames", "ok", "active_frames", "samples", "review_images"],
        )
        writer.writeheader()
        for run_name, stats in sorted(per_run.items()):
            writer.writerow({"run": run_name, **stats})
        writer.writerow(
            {
                "run": "TOTAL",
                "frames": total_frames,
                "ok": total_ok,
                "active_frames": total_active_frames,
                "samples": total_samples,
                "review_images": copied_images,
            }
        )

    return {
        "frame_files": len(frame_files),
        "frames": total_frames,
        "ok": total_ok,
        "active_frames": total_active_frames,
        "samples": total_samples,
        "review_images": copied_images,
        "output_dir": str(output_dir),
        "samples_path": str(samples_path),
        "index_path": str(index_path),
        "summary_path": str(summary_path),
    }


def main() -> int:
    args = parse_args()
    input_path = Path(args.input)
    output_dir = Path(args.output)
    frame_files = find_frame_files(input_path)
    if not frame_files:
        raise SystemExit(f"No frames.jsonl files found under {input_path}")

    result = export_candidates(
        frame_files=frame_files,
        output_dir=output_dir,
        copy_review_images=args.copy_review_images,
        max_review_images=max(0, args.max_review_images),
    )
    print(json.dumps(result, ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
