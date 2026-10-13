#!/usr/bin/env python3
"""Pack a UTF-8 JSON object into the reserved first 256 KiB flash block."""
import argparse
import json
import hashlib
import subprocess
from pathlib import Path

REGION_SIZE = 0x40000


def pack(source: Path, output: Path, images=()) -> None:
    def unique_object(pairs):
        result = {}
        for key, value in pairs:
            if key in result:
                raise ValueError(f"Duplicate JSON key: {key}")
            result[key] = value
        return result

    def reject_constant(value):
        raise ValueError(f"Non-JSON numeric value: {value}")

    data = json.loads(source.read_text(encoding="utf-8"),
                      object_pairs_hook=unique_object, parse_constant=reject_constant)
    if not isinstance(data, dict):
        raise ValueError("Parameters must be a JSON object")
    root = Path(__file__).resolve().parent.parent
    def git(*args):
        return subprocess.check_output(['git', '-C', str(root), *args], text=True).strip()
    # Always record the repository state when parameters are generated, even
    # for standalone packing. Per-image build records remain independent.
    data['repository'] = {
        'git_hash': git('rev-parse', 'HEAD'),
        'git_dirty': bool(git('status', '--porcelain', '--untracked-files=normal')),
    }
    if images:
        provenance = {}
        for label, image in images:
            manifest = image.with_suffix('.manifest.json')
            metadata = json.loads(manifest.read_text(encoding='utf-8'))
            digest = hashlib.sha256(image.read_bytes()).hexdigest()
            if metadata['image_sha256'] != digest:
                raise ValueError(f"Image does not match build manifest: {image}; rebuild it")
            provenance[label] = metadata
        data['build'] = provenance
    payload = (json.dumps(data, indent=2, ensure_ascii=False, allow_nan=False) + "\n").encode("utf-8")
    # A NUL terminator allows firmware to use it as a bounded C string.
    if len(payload) + 1 > REGION_SIZE:
        raise ValueError("Parameters exceed the reserved 256 KiB region")
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(payload + b"\0" + b"\xff" * (REGION_SIZE - len(payload) - 1))
    print(f"Parameters: {len(payload)} JSON bytes -> {output} ({REGION_SIZE} bytes)")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument('--fsbl', type=Path)
    parser.add_argument('--application', type=Path)
    args = parser.parse_args()
    try:
        images = [(label, image) for label, image in
                  [('fsbl', args.fsbl), ('application', args.application)] if image is not None]
        pack(args.source, args.output, images)
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"Parameter packing failed: {error}\n")


if __name__ == "__main__":
    main()
