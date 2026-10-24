#!/usr/bin/env python3
"""生成重复数据并逐进程运行冷/暖应用缓存基准。"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
import time


def generate(args):
    from PIL import Image, ImageEnhance

    root = args.root.resolve()
    source_hash = hashlib.sha256(args.source.read_bytes()).hexdigest()
    manifest = root / "dataset.json"
    if manifest.exists():
        previous = json.loads(manifest.read_text())
        if previous.get("storage") != args.storage or previous.get("source_sha256") != source_hash:
            raise SystemExit("目标目录已有不同的数据集定义，请使用新目录")
    samples = root / "samples"
    samples.mkdir(parents=True, exist_ok=True)
    with Image.open(args.source) as photo:
        for index in range(64):
            size = [(640, 480), (480, 640), (1024, 683), (800, 400)][index % 4]
            sample = ImageEnhance.Color(photo.convert("RGB").resize(size)).enhance(0.5 + index / 64)
            sample.save(samples / f"{index:02}.jpg", quality=80)
    encoded = [(samples / f"{index:02}.jpg").read_bytes() for index in range(64)]
    largest = max(args.sizes)
    for count in sorted(args.sizes, reverse=True):
        folder = root / str(count)
        for index in range(count):
            group = folder / f"{index // 1000:04}"
            group.mkdir(parents=True, exist_ok=True)
            target = group / f"image-{index:06}.jpg"
            if not target.exists():
                if args.storage == "independent" and count == largest:
                    comment = f"imageviewer-{index:06}".encode()
                    marker = b"\xff\xfe" + (len(comment) + 2).to_bytes(2, "big") + comment
                    jpeg = encoded[index % 64]
                    target.write_bytes(jpeg[:2] + marker + jpeg[2:])
                elif args.storage == "independent":
                    os.link(root / str(largest) / group.name / target.name, target)
                else:
                    os.link(samples / f"{index % 64:02}.jpg", target)
        print(f"generated {count}: {folder}", flush=True)
    (root / "dataset.json").write_text(json.dumps({
        "source": str(args.source.resolve()), "sizes": args.sizes,
        "source_sha256": source_hash,
        "samples": 64, "storage": args.storage,
        "description": "真实壁纸派生的混合尺寸 JPEG；independent 模式每个图库具有 N 个独立 inode 和不同 JPEG 注释，各规模间共享文件",
    }, ensure_ascii=False, indent=2))


def run(args):
    args.output.mkdir(parents=True, exist_ok=True)
    revision = subprocess.check_output(["git", "rev-parse", "--verify", (args.revision or "HEAD") + "^{commit}"], text=True).strip()
    for count in args.sizes:
        with tempfile.TemporaryDirectory(prefix="imageviewer-benchmark-") as cache_path:
            cache = Path(cache_path)
            for mode in ("cold", "warm"):
                if mode == "cold" and args.evict_files:
                    for path in (args.data / str(count)).rglob("*"):
                        if path.is_file():
                            with path.open("rb") as file:
                                os.posix_fadvise(file.fileno(), 0, 0, os.POSIX_FADV_DONTNEED)
                result = args.output.resolve() / f"{count}-{mode}.json"
                env = dict(os.environ, QT_QPA_PLATFORM="offscreen", QT_SCALE_FACTOR="1",
                           XDG_CONFIG_HOME=str(cache / "config"), XDG_CACHE_HOME=str(cache))
                start = time.monotonic()
                subprocess.run([str(args.exe.resolve()), str(args.data.resolve() / str(count)),
                                str(result), mode], env=env, check=True, timeout=600)
                document = json.loads(result.read_text())
                document["revision"] = revision
                document["advised_drop_file_pages"] = args.evict_files
                manifest = args.data / "dataset.json"
                document["dataset_definition"] = json.loads(manifest.read_text()) if manifest.exists() else {}
                result.write_text(json.dumps(document, ensure_ascii=False, indent=2))
                print(f"{count} {mode}: {time.monotonic() - start:.2f}s -> {result}", flush=True)


def generate_viewer(args):
    from PIL import Image

    args.root.mkdir(parents=True, exist_ok=True)
    with Image.open(args.source) as source:
        source.convert("RGB").resize((9000, 8000)).save(args.root / "large-0.jpg", quality=80)
    for name in ("large-1.jpg", "large-2.jpg", "large-extra.jpg"):
        if not (args.root / name).exists():
            os.link(args.root / "large-0.jpg", args.root / name)
    frames = [Image.new("RGB", (640, 360), (index * 2, 80, 140)) for index in range(120)]
    frames[0].save(args.root / "animation.gif", save_all=True, append_images=frames[1:], duration=20, loop=0)


parser = argparse.ArgumentParser(description=__doc__)
commands = parser.add_subparsers(dest="command", required=True)
dataset = commands.add_parser("generate")
dataset.add_argument("--root", type=Path, required=True)
dataset.add_argument("--source", type=Path, required=True)
dataset.add_argument("--sizes", type=int, nargs="+", default=[10000, 50000, 100000, 300000])
dataset.add_argument("--storage", choices=["hardlinks", "independent"], default="hardlinks")
dataset.set_defaults(action=generate)
viewer_data = commands.add_parser("generate-viewer")
viewer_data.add_argument("--root", type=Path, required=True)
viewer_data.add_argument("--source", type=Path, required=True)
viewer_data.set_defaults(action=generate_viewer)
bench = commands.add_parser("run")
bench.add_argument("--exe", type=Path, required=True)
bench.add_argument("--data", type=Path, required=True)
bench.add_argument("--output", type=Path, required=True)
bench.add_argument("--sizes", type=int, nargs="+", default=[10000, 50000, 100000, 300000])
bench.add_argument("--revision")
bench.add_argument("--evict-files", action="store_true")
bench.set_defaults(action=run)
arguments = parser.parse_args()
arguments.action(arguments)
