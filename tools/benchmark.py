#!/usr/bin/env python3
"""生成重复数据并逐进程运行冷/暖应用缓存基准。"""
import argparse
import hashlib
import json
import os
import ctypes
import mmap
import re
import shutil
from pathlib import Path
import subprocess
import tempfile
import time

IMAGE_SUFFIXES = {".jpg", ".jpeg", ".png", ".webp", ".gif", ".bmp"}


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


def device_snapshot(path):
    device = path.stat().st_dev
    stat = Path(f"/sys/dev/block/{os.major(device)}:{os.minor(device)}/stat")
    values = list(map(int, stat.read_text().split())) if stat.exists() else []
    return dict(zip(("reads", "reads_merged", "sectors_read", "read_ms", "writes", "writes_merged",
                     "sectors_written", "write_ms", "inflight", "busy_ms", "weighted_ms"), values))


def system_memory():
    return {key: int(value.split()[0]) for key, value in
            (line.split(":", 1) for line in Path("/proc/meminfo").read_text().splitlines())
            if key in ("Cached", "Dirty", "Writeback", "MemAvailable")}


def resident_sample(folder):
    # 固定有界样本；mincore 仅查询驻留，不触发文件内容读取。
    paths = []
    index = 0
    for parent, directories, files in os.walk(folder):
        directories.sort()
        for name in sorted(files):
            if Path(name).suffix.lower() not in IMAGE_SUFFIXES:
                continue
            if index % 127 == 0 and len(paths) < 256:
                paths.append(Path(parent) / name)
            index += 1
        if len(paths) == 256:
            break
    libc = ctypes.CDLL(None, use_errno=True)
    resident = pages = errors = 0
    for path in paths:
        try:
            with path.open("rb") as file:
                length = min(path.stat().st_size, 1024 * 1024)
                if length == 0:
                    continue
                with mmap.mmap(file.fileno(), length, access=mmap.ACCESS_COPY) as mapping:
                    address = ctypes.addressof(ctypes.c_char.from_buffer(mapping))
                    count = (length + mmap.PAGESIZE - 1) // mmap.PAGESIZE
                    vector = (ctypes.c_ubyte * count)()
                    if libc.mincore(ctypes.c_void_p(address), ctypes.c_size_t(length), vector) != 0:
                        errors += 1
                    else:
                        pages += count
                        resident += sum(value & 1 for value in vector)
        except (OSError, ValueError):
            errors += 1
    return {"files": len(paths), "pages": pages, "resident_pages": resident, "errors": errors,
            "method": "每127个路径取样，最多256文件，每文件前1MiB；非全库驻留率"}


def generate_mixed(args):
    from PIL import Image, ImageEnhance

    root = args.root.resolve()
    if root.exists():
        raise SystemExit("混合数据集必须使用新目录")
    root.mkdir(parents=True)
    samples = root / "samples"
    samples.mkdir()
    definitions = []
    with Image.open(args.source) as photo:
        photo = photo.convert("RGB")
        for index in range(80):
            format_name = ["JPEG"] * 4 + ["PNG"] * 2 + ["WEBP", "GIF", "BMP", "JPEG"]
            fmt = format_name[index % 10]
            size = [(640, 480), (480, 640), (1024, 683), (96, 96)][index % 4]
            if fmt == "BMP":
                size = (64, 48)
            image = ImageEnhance.Color(photo.resize(size)).enhance(0.5 + index / 80)
            options = {}
            if fmt == "JPEG":
                exif = Image.Exif()
                exif[274] = index % 8 + 1
                options = {"quality": 80, "exif": exif}
            if fmt == "PNG" and index % 2:
                image = image.convert("RGBA")
                image.putalpha(180)
            if fmt == "GIF":
                options = {"save_all": True, "append_images": [image.transpose(Image.Transpose.FLIP_LEFT_RIGHT)],
                           "duration": 80, "loop": 0}
            extension = {"JPEG": "jpg", "PNG": "png", "WEBP": "webp", "GIF": "gif", "BMP": "bmp"}[fmt]
            path = samples / f"{index:02}.{extension}"
            image.save(path, format=fmt, **options)
            definitions.append({"file": path.name, "format": fmt, "size": size, "sha256": hashlib.sha256(path.read_bytes()).hexdigest()})
        for name, size, fmt in [("large.jpg", (9000, 8000), "JPEG"), ("large.png", (6000, 4000), "PNG"), ("large.bmp", (4096, 4096), "BMP")]:
            photo.resize(size).save(samples / name, format=fmt)
    (samples / "corrupt.jpg").write_bytes(b"not-an-image")
    bmp = (samples / "08.bmp").read_bytes()
    (samples / "truncated.bmp").write_bytes(bmp[:54])
    largest = max(args.sizes)
    counts = {}
    def relative(index):
        group = Path(f"分组 {index // 1000:04}")
        if index % 97 == 0:
            group = group / "深层" / "一" / "二" / "三" / "四" / "五"
        remainder = index % 10000
        special = {9996: "large.jpg", 9997: "large.png", 9998: "large.bmp"}
        name = special.get(remainder)
        category = "large" if name else "normal"
        if index % 1000 == 995:
            name, category = "corrupt.jpg", "corrupt_header"
        elif index % 1000 == 994:
            name, category = "truncated.bmp", "truncated_body"
        elif index % 1000 == 993:
            name, category = definitions[index % 80]["file"], "unreadable"
        if name is None:
            name = definitions[index % 80]["file"]
        return group / (f"图片 # % []+'\" {index:06}." + name.split(".")[-1]), name, category
    for count in sorted(args.sizes, reverse=True):
        categories = {}
        for index in range(count):
            rel, sample, category = relative(index)
            target = root / str(count) / rel
            target.parent.mkdir(parents=True, exist_ok=True)
            if count == largest:
                shutil.copyfile(samples / sample, target)
                if category == "unreadable":
                    target.chmod(0)
            else:
                os.link(root / str(largest) / rel, target)
            categories[category] = categories.get(category, 0) + 1
        counts[str(count)] = categories
        print(f"generated mixed {count}", flush=True)
    (root / "dataset.json").write_text(json.dumps({"source": str(args.source.resolve()),
        "source_sha256": hashlib.sha256(args.source.read_bytes()).hexdigest(), "samples": definitions,
        "sizes": args.sizes, "categories": counts, "storage": "independent within each library; hardlinks across sizes",
        "description": "现有真实壁纸派生的五格式场景集，不是独立摄影原片；每万张含3个大文件，每千张含权限/损坏头/截断体各1个"}, ensure_ascii=False, indent=2))


def run(args):
    args.output.mkdir(parents=True, exist_ok=True)
    revision = subprocess.check_output(["git", "rev-parse", "--verify", (args.revision or "HEAD") + "^{commit}"], text=True).strip()
    runner_sha = hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
    probe_sha = hashlib.sha256(args.probe_source.read_bytes()).hexdigest()
    for count in args.sizes if args.data is not None else [None]:
        directory = args.data / str(count) if args.data is not None else args.directory
        label = str(count) if count is not None else "real"
        with tempfile.TemporaryDirectory(prefix="imageviewer-benchmark-") as cache_path:
            cache = Path(cache_path)
            for mode in ("cold", "warm"):
                if mode == "cold" and args.evict_files:
                    for path in directory.rglob("*"):
                        if path.is_file() and path.suffix.lower() in IMAGE_SUFFIXES:
                            with path.open("rb") as file:
                                os.posix_fadvise(file.fileno(), 0, 0, os.POSIX_FADV_DONTNEED)
                result = args.output.resolve() / f"{label}-{mode}.json"
                env = dict(os.environ, QT_QPA_PLATFORM=args.backend, QT_SCALE_FACTOR=str(args.scale),
                           IV_BENCH_WINDOW=args.window,
                           XDG_CONFIG_HOME=str(cache / "config"), XDG_CACHE_HOME=str(cache))
                pages_before = resident_sample(directory)
                device_before = device_snapshot(cache)
                source_device_before = device_snapshot(directory)
                memory_before = system_memory()
                start = time.monotonic()
                subprocess.run([str(args.exe.resolve()), str(directory.resolve()),
                                str(result), mode if args.scenario == "legacy" else args.scenario], env=env, check=True, timeout=600)
                document = json.loads(result.read_text())
                document["revision"] = revision
                document["runner_sha256"] = runner_sha
                document["probe_sha256"] = probe_sha
                document["executable_sha256"] = hashlib.sha256(args.exe.read_bytes()).hexdigest()
                document["scenario"] = args.scenario
                device_after = device_snapshot(cache)
                document["whole_device_delta"] = {key: device_after[key] - value for key, value in device_before.items()}
                document["whole_device_note"] = "缓存分区，包含其他进程；不是进程或SSD介质独占归因"
                source_device_after = device_snapshot(directory)
                document["source_device_delta"] = {key: source_device_after[key] - value for key, value in source_device_before.items()}
                document["source_device"] = {"major": os.major(directory.stat().st_dev), "minor": os.minor(directory.stat().st_dev)}
                document["cache_device"] = {"major": os.major(cache.stat().st_dev), "minor": os.minor(cache.stat().st_dev)}
                document["system_memory_kib"] = {"before": memory_before, "after": system_memory()}
                document["source_page_sample"] = {"before": pages_before, "after": resident_sample(directory)}
                document["advised_drop_file_pages"] = args.evict_files
                manifest = args.data / "dataset.json" if args.data is not None else None
                document["dataset_definition"] = json.loads(manifest.read_text()) if manifest is not None and manifest.exists() else {}
                result.write_text(json.dumps(document, ensure_ascii=False, indent=2))
                print(f"{label} {mode}: {time.monotonic() - start:.2f}s -> {result}", flush=True)


def long_run(args):
    for directory in [args.directory, args.alternate, args.fixtures]:
        if not directory.is_dir():
            raise SystemExit(f"测试目录不存在：{directory}")
    args.output.mkdir(parents=True, exist_ok=True)
    timeline = args.output.resolve() / "timeline.jsonl"
    if timeline.exists():
        raise SystemExit("长稳输出已存在，请使用新目录")
    revision = subprocess.check_output(["git", "rev-parse", "HEAD"], text=True).strip()
    metadata = {"revision": revision, "seconds_requested": args.seconds,
                "tracked_changes_at_start": subprocess.check_output(["git", "diff", "HEAD", "--name-only"], text=True).splitlines(),
                "executable_sha256": hashlib.sha256(args.exe.read_bytes()).hexdigest(),
                "probe_sha256": hashlib.sha256(Path(__file__).with_name("performance_probe.cc").read_bytes()).hexdigest(),
                "runner_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest()}
    with tempfile.TemporaryDirectory(prefix="iv-long-") as temp:
        env = dict(os.environ, QT_QPA_PLATFORM="xcb", QT_SCALE_FACTOR="1", IV_BENCH_WINDOW="1280x800",
                   XDG_CACHE_HOME=temp + "/cache", XDG_CONFIG_HOME=temp + "/config", XDG_DATA_HOME=temp + "/data",
                   IV_BENCH_SECONDS=str(args.seconds), IV_BENCH_ALTERNATE=str(args.alternate.resolve()),
                   IV_BENCH_FIXTURES=str(args.fixtures.resolve()))
        monitors = []
        files = []
        stderr_bytes = 0
        metadata["monitors"] = {}
        process = subprocess.Popen([str(args.exe.resolve()), str(args.directory.resolve()), str(timeline), "long"],
                                   env=env, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
        try:
            metadata["pid"] = process.pid
            (args.output / "run.json").write_text(json.dumps(metadata, indent=2))
            devices = set()
            for directory in [args.directory, args.alternate, Path(temp)]:
                device = directory.stat().st_dev
                node = Path(f"/sys/dev/block/{os.major(device)}:{os.minor(device)}").resolve()
                if node.exists():
                    devices.add(node.parent.name if (node / "partition").exists() else node.name)
            for name, command in [("iostat", ["iostat", "-x", "-d", "-y", "-p", ",".join(sorted(devices)), "30"]),
                                  ("pidstat", ["pidstat", "-h", "-u", "-r", "-d", "-p", str(process.pid), "30"])]:
                available = bool(shutil.which(name)) and (name != "iostat" or bool(devices))
                metadata["monitors"][name] = {"command": command, "available": available}
                if available:
                    log = (args.output / f"{name}.txt").open("w")
                    files.append(log)
                    monitors.append((name, subprocess.Popen(command, stdout=log, stderr=log)))
            with (args.output / "stderr.txt").open("wb") as log:
                for block in iter(lambda: process.stderr.read(65536), b""):
                    if stderr_bytes < 1024 * 1024:
                        log.write(block[:1024 * 1024 - stderr_bytes])
                    stderr_bytes += len(block)
            code = process.wait()
        finally:
            if process.poll() is None:
                process.terminate()
                process.wait()
            process.stderr.close()
            for name, monitor in monitors:
                metadata["monitors"][name]["running_when_stopped"] = monitor.poll() is None
                monitor.terminate()
                metadata["monitors"][name]["returncode"] = monitor.wait()
            for log in files:
                log.close()
            metadata.update(returncode=process.returncode, stderr_bytes=stderr_bytes, stderr_truncated=stderr_bytes > 1024 * 1024)
            (args.output / "run.json").write_text(json.dumps(metadata, indent=2))
        if code != 0:
            raise SystemExit(f"长稳进程退出：{code}；保留阶段证据")
        print(f"long run complete: {timeline}", flush=True)


def trace_writes(args):
    args.output.mkdir(parents=True, exist_ok=True)
    for mode in ("usage", "paced"):
        with tempfile.TemporaryDirectory(prefix="iv-write-trace-") as temp:
            trace = Path(temp) / "writes.txt"
            result = args.output.resolve() / f"{mode}.json"
            env = dict(os.environ, QT_QPA_PLATFORM="xcb", QT_SCALE_FACTOR="1", IV_BENCH_WINDOW="1000x600",
                       XDG_CACHE_HOME=temp + "/cache", XDG_CONFIG_HOME=temp + "/config")
            subprocess.run(["strace", "-f", "-yy", "-s", "1", "-e", "trace=write,pwrite64,writev,pwritev",
                            "-o", str(trace), str(args.exe.resolve()), str(args.directory.resolve()), str(result), mode],
                           env=env, check=True, timeout=600)
            pending = {}
            encoded_bytes = calls = unfinished = 0
            for line in trace.read_text().splitlines():
                thread = line.split(maxsplit=1)[0]
                cache_write = ".thumb>" in line
                if "<unfinished ...>" in line and cache_write:
                    pending[thread] = True
                    continue
                if "resumed>" in line:
                    cache_write = pending.pop(thread, False)
                match = re.search(r" = ([0-9]+)$", line)
                if cache_write and match:
                    encoded_bytes += int(match[1])
                    calls += 1
                elif cache_write:
                    unfinished += 1
            document = json.loads(result.read_text())
            document["cache_write_trace"] = {"encoded_bytes": encoded_bytes, "completed_write_calls": calls,
                "unmatched_lines": unfinished, "pending_lines": len(pending),
                "note": "仅 .thumb 文件成功 write/pwrite/writev 字节；非设备落盘，strace 场景不用作正常延迟/CPU对比"}
            document["executable_sha256"] = hashlib.sha256(args.exe.read_bytes()).hexdigest()
            document["probe_sha256"] = hashlib.sha256(Path(__file__).with_name("performance_probe.cc").read_bytes()).hexdigest()
            result.write_text(json.dumps(document, ensure_ascii=False, indent=2))
            print(f"{mode}: {encoded_bytes} encoded bytes, {calls} writes, {unfinished} unmatched", flush=True)


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
mixed = commands.add_parser("generate-mixed")
mixed.add_argument("--root", type=Path, required=True)
mixed.add_argument("--source", type=Path, required=True)
mixed.add_argument("--sizes", type=int, nargs="+", default=[1000, 10000, 100000, 300000])
mixed.set_defaults(action=generate_mixed)
viewer_data = commands.add_parser("generate-viewer")
viewer_data.add_argument("--root", type=Path, required=True)
viewer_data.add_argument("--source", type=Path, required=True)
viewer_data.set_defaults(action=generate_viewer)
trace = commands.add_parser("trace-writes")
trace.add_argument("--exe", type=Path, required=True)
trace.add_argument("--directory", type=Path, required=True)
trace.add_argument("--output", type=Path, required=True)
trace.set_defaults(action=trace_writes)
bench = commands.add_parser("run")
bench.add_argument("--exe", type=Path, required=True)
input_group = bench.add_mutually_exclusive_group(required=True)
input_group.add_argument("--data", type=Path)
input_group.add_argument("--directory", type=Path)
bench.add_argument("--output", type=Path, required=True)
bench.add_argument("--sizes", type=int, nargs="+", default=[10000, 50000, 100000, 300000])
bench.add_argument("--revision")
bench.add_argument("--probe-source", type=Path, default=Path(__file__).with_name("performance_probe.cc"))
bench.add_argument("--backend", choices=["offscreen", "xcb"], default="offscreen")
bench.add_argument("--scale", type=float, choices=[1, 1.5, 2], default=1)
bench.add_argument("--window", default="1280x800")
bench.add_argument("--scenario", choices=["legacy", "usage", "paced", "soak", "profile"], default="legacy")
bench.add_argument("--evict-files", action="store_true")
bench.set_defaults(action=run)
long_test = commands.add_parser("long")
long_test.add_argument("--exe", type=Path, required=True)
long_test.add_argument("--directory", type=Path, required=True)
long_test.add_argument("--alternate", type=Path, required=True)
long_test.add_argument("--fixtures", type=Path, required=True)
long_test.add_argument("--output", type=Path, required=True)
long_test.add_argument("--seconds", type=int, default=7200)
long_test.set_defaults(action=long_run)
arguments = parser.parse_args()
if hasattr(arguments, "window") and not re.fullmatch(r"[1-9][0-9]{2,3}x[1-9][0-9]{2,3}", arguments.window):
    parser.error("window 格式为 宽x高，范围100至9999")
arguments.action(arguments)
