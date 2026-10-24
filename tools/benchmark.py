#!/usr/bin/env python3
"""生成重复数据并逐进程运行冷/暖应用缓存基准。"""
import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile
import time


def generate(args):
    from PIL import Image, ImageEnhance

    root = args.root.resolve()
    samples = root / "samples"
    samples.mkdir(parents=True, exist_ok=True)
    with Image.open(args.source) as photo:
        for index in range(64):
            size = [(640, 480), (480, 640), (1024, 683), (800, 400)][index % 4]
            sample = ImageEnhance.Color(photo.convert("RGB").resize(size)).enhance(0.5 + index / 64)
            sample.save(samples / f"{index:02}.jpg", quality=80)
    for count in args.sizes:
        folder = root / str(count)
        for index in range(count):
            group = folder / f"{index // 1000:04}"
            group.mkdir(parents=True, exist_ok=True)
            target = group / f"image-{index:06}.jpg"
            if not target.exists():
                os.link(samples / f"{index % 64:02}.jpg", target)
        print(f"generated {count}: {folder}", flush=True)
    (root / "dataset.json").write_text(json.dumps({
        "source": str(args.source.resolve()), "sizes": args.sizes,
        "samples": 64, "hardlinks": True,
        "description": "真实壁纸派生的混合尺寸 JPEG；独立路径，共享内容 inode，不能代表独立冷盘原图 I/O",
    }, ensure_ascii=False, indent=2))


def run(args):
    args.output.mkdir(parents=True, exist_ok=True)
    for count in args.sizes:
        with tempfile.TemporaryDirectory(prefix="imageviewer-benchmark-") as cache_path:
            cache = Path(cache_path)
            for mode in ("cold", "warm"):
                result = args.output.resolve() / f"{count}-{mode}.json"
                env = dict(os.environ, QT_QPA_PLATFORM="offscreen", QT_SCALE_FACTOR="1",
                           XDG_CONFIG_HOME=str(cache / "config"), XDG_CACHE_HOME=str(cache))
                start = time.monotonic()
                subprocess.run([str(args.exe.resolve()), str(args.data.resolve() / str(count)),
                                str(result), mode], env=env, check=True, timeout=600)
                document = json.loads(result.read_text())
                document["revision"] = subprocess.check_output(["git", "rev-parse", "HEAD"], text=True).strip()
                result.write_text(json.dumps(document, ensure_ascii=False, indent=2))
                print(f"{count} {mode}: {time.monotonic() - start:.2f}s -> {result}", flush=True)


parser = argparse.ArgumentParser(description=__doc__)
commands = parser.add_subparsers(dest="command", required=True)
dataset = commands.add_parser("generate")
dataset.add_argument("--root", type=Path, required=True)
dataset.add_argument("--source", type=Path, required=True)
dataset.add_argument("--sizes", type=int, nargs="+", default=[10000, 50000, 100000, 300000])
dataset.set_defaults(action=generate)
bench = commands.add_parser("run")
bench.add_argument("--exe", type=Path, required=True)
bench.add_argument("--data", type=Path, required=True)
bench.add_argument("--output", type=Path, required=True)
bench.add_argument("--sizes", type=int, nargs="+", default=[10000, 50000, 100000, 300000])
bench.set_defaults(action=run)
arguments = parser.parse_args()
arguments.action(arguments)
