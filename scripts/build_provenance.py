#!/usr/bin/env python3
"""Record the revision, compiler settings and hash of a packaged image."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import shlex
import subprocess

MANIFEST_VERSION = 2


## @brief Collect the repository revision, worktree state, branch, and submodule revisions.
def repository_state(root):
    ## @brief Run a Git query in the selected repository and return trimmed text.
    def git(*args):
        return subprocess.check_output(['git', '-C', str(root), *args], text=True).strip()
    return {
        'git_hash': git('rev-parse', 'HEAD'),
        'git_dirty': bool(git('status', '--porcelain', '--untracked-files=normal')),
        'git_description': git('describe', '--always', '--tags', '--dirty'),
        'git_branch': git('rev-parse', '--abbrev-ref', 'HEAD'),
        'submodules': [dict(path=parts[1], git_hash=parts[0],
                            state={' ': 'clean', '-': 'uninitialized', '+': 'modified', 'U': 'conflict'}[line[0]])
                       for line in subprocess.check_output(
                           ['git', '-C', str(root), 'submodule', 'status', '--recursive'], text=True).splitlines()
                       if len(parts := line[1:].split()) >= 2],
    }


## @brief Extract the actual compiler, flags, definitions, and options from a CMake build.
def build_settings(build_dir, configuration):
    cache = {}
    for line in (build_dir / 'CMakeCache.txt').read_text().splitlines():
        if line and not line.startswith(('#', '//')) and ':' in line and '=' in line:
            key, value = line.split('=', 1)
            cache[key.split(':', 1)[0]] = value
    if cache.get('CMAKE_BUILD_TYPE') != configuration:
        raise ValueError('Requested configuration does not match the built CMake configuration')
    commands = json.loads((build_dir / 'compile_commands.json').read_text())
    entry = next(command for command in commands
                 if Path(command['file']).as_posix().endswith('/Core/Src/main.c'))
    arguments = entry.get('arguments') or shlex.split(entry['command'])
    compiler = arguments[0]
    flags, defines, preprocess = [], {}, [compiler]
    i = 1
    while i < len(arguments):
        arg = arguments[i]
        if arg in ('-o', '-MF', '-MT', '-MQ'):
            i += 2
            continue
        if arg in ('-c', '-MD', '-MMD', '-MP'):
            i += 1
            continue
        preprocess.append(arg)
        if arg in ('-I', '-isystem', '-iquote', '-include'):
            preprocess.append(arguments[i + 1])
            i += 2
            continue
        if arg == '-D':
            definition = arguments[i + 1]
            preprocess.append(definition)
            i += 2
        elif arg.startswith('-D'):
            definition = arg[2:]
            i += 1
        else:
            if arg.startswith('-') and not arg.startswith('-I'):
                flags.append(arg)
            i += 1
            continue
        name, _, value = definition.partition('=')
        defines[name] = value or '1'
    macros = subprocess.check_output(preprocess + ['-dM', '-E'],
                                     cwd=entry['directory'], text=True)
    macros = dict(line.removeprefix('#define ').split(' ', 1)
                  for line in macros.splitlines() if line.startswith('#define ') and
                  ' ' in line.removeprefix('#define '))
    tracked_macros = ('TX_TIMER_TICKS_PER_SECOND', 'TX_ENABLE_EXECUTION_CHANGE_NOTIFY',
                      'TX_DISABLE_PREEMPTION_THRESHOLD', 'TX_DISABLE_NOTIFY_CALLBACKS')
    return {
        'compiler': {
            'name': Path(compiler).name,
            'version': subprocess.check_output([compiler, '--version'], text=True).splitlines()[0],
            'target': subprocess.check_output([compiler, '-dumpmachine'], text=True).strip(),
        },
        'cmake': {
            'version': '.'.join(cache[f'CMAKE_CACHE_{part}_VERSION'] for part in ('MAJOR', 'MINOR', 'PATCH')),
            'generator': cache['CMAKE_GENERATOR'],
            'toolchain': Path(cache.get('CMAKE_TOOLCHAIN_FILE', '')).name,
        },
        'options': {key: cache[key].upper() in ('ON', 'TRUE', 'YES', '1')
                    for key in ('DEV_MODE', 'VL53L9_SENSOR_ONLY') if key in cache},
        'compile_source': 'Core/Src/main.c',
        'compile_flags': flags,
        'compile_definitions': defines,
        'rtos_definitions': {key: macros[key] or '1' for key in tracked_macros if key in macros},
    }


## @brief Combine repository and build metadata with the packaged image's size and SHA-256.
def record(root, image, configuration, build_dir=None):
    settings = build_settings(build_dir or image.parent, configuration)
    return {
        'manifest_version': MANIFEST_VERSION,
        **repository_state(root),
        'built_at_utc': datetime.now(timezone.utc).isoformat(),
        'configuration': configuration,
        'image': image.name,
        'image_size_bytes': image.stat().st_size,
        'image_sha256': hashlib.sha256(image.read_bytes()).hexdigest(),
        **settings,
    }


## @brief Write a provenance manifest for the selected packaged firmware image.
def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('image', type=Path)
    parser.add_argument('configuration', choices=['Debug', 'Release'])
    args = parser.parse_args()
    root = Path(__file__).resolve().parent.parent
    try:
        metadata = record(root, args.image, args.configuration)
        manifest = args.image.with_suffix('.manifest.json')
        manifest.write_text(json.dumps(metadata, indent=2) + '\n')
    except (OSError, ValueError, KeyError, StopIteration, subprocess.CalledProcessError) as error:
        parser.exit(1, f'Build provenance failed: {error}\n')


if __name__ == '__main__':
    main()
