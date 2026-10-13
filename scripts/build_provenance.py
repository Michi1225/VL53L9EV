#!/usr/bin/env python3
"""Record source revision and image hash when an image is built/packaged."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import subprocess


def record(root, image, configuration):
    def git(*args):
        return subprocess.check_output(['git', '-C', str(root), *args], text=True).strip()
    return {
        'git_hash': git('rev-parse', 'HEAD'),
        'git_dirty': bool(git('status', '--porcelain', '--untracked-files=normal')),
        'git_description': git('describe', '--always', '--tags', '--dirty'),
        'built_at_utc': datetime.now(timezone.utc).isoformat(),
        'configuration': configuration,
        'image': image.name,
        'image_size_bytes': image.stat().st_size,
        'image_sha256': hashlib.sha256(image.read_bytes()).hexdigest(),
    }


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('image', type=Path)
    parser.add_argument('configuration', choices=['Debug', 'Release'])
    args = parser.parse_args()
    root = Path(__file__).resolve().parent.parent
    manifest = args.image.with_suffix('.manifest.json')
    manifest.write_text(json.dumps(record(root, args.image, args.configuration), indent=2) + '\n')
