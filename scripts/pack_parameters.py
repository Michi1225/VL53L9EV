#!/usr/bin/env python3
"""Merge device parameters and verified image provenance into the flash block."""
import argparse
from copy import deepcopy
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import subprocess

from build_provenance import MANIFEST_VERSION, repository_state

REGION_SIZE = 0x40000
MAX_JSON_SIZE = 16384  # App startup reader capacity, including NUL.
IMAGE_ADDRESSES = {'fsbl': '0x90040000', 'application': '0x90100000',
                   'application_b': '0x90900000'}
GENERATED_KEYS = {'repository', 'build', 'flash', 'parameter_build'}


## @brief Parse a parameter object while rejecting duplicate keys and non-JSON numbers.
def load_json(text):
    ## @brief Build a JSON object while rejecting repeated keys.
    def unique_object(pairs):
        result = {}
        for key, value in pairs:
            if key in result:
                raise ValueError(f'Duplicate JSON key: {key}')
            result[key] = value
        return result

    ## @brief Reject numeric constants that are outside the JSON format.
    def reject_constant(value):
        raise ValueError(f'Non-JSON numeric value: {value}')

    data = json.loads(text, object_pairs_hook=unique_object, parse_constant=reject_constant)
    if not isinstance(data, dict):
        raise ValueError('Parameters must be a JSON object')
    return data


## @brief Decode a complete parameter readback or recognize an erased flash block.
def read_existing(path):
    raw = path.read_bytes()
    if len(raw) != MAX_JSON_SIZE:
        raise ValueError('Parameter readback must contain exactly 16 KiB')
    if raw == b'\xff' * len(raw):
        return {}
    payload, separator, _ = raw.partition(b'\0')
    if not separator:
        raise ValueError('Stored parameters have no NUL terminator; use --parameters to replace them')
    try:
        return load_json(payload.decode('utf-8'))
    except (UnicodeError, ValueError) as error:
        raise ValueError('Stored parameters are invalid; use --parameters to replace them') from error


## @brief Recursively merge changed settings into an independent copy of the original object.
def merge(base, changes):
    result = deepcopy(base)
    for key, value in changes.items():
        if isinstance(value, dict) and isinstance(result.get(key), dict):
            result[key] = merge(result[key], value)
        else:
            result[key] = deepcopy(value)
    return result


## @brief Validate an image's provenance, hash, size, build timestamp, and flash suitability.
def image_metadata(image):
    metadata = load_json(image.with_suffix('.manifest.json').read_text(encoding='utf-8'))
    if metadata.get('manifest_version') != MANIFEST_VERSION:
        raise ValueError(f'Build manifest needs updating: {image}; rebuild with scripts/build.sh or --build')
    if (metadata.get('image_sha256') != hashlib.sha256(image.read_bytes()).hexdigest() or
            metadata.get('image_size_bytes') != image.stat().st_size or metadata.get('image') != image.name):
        raise ValueError(f'Image does not match build manifest: {image}; rebuild it')
    if not all(isinstance(metadata.get(key), dict) and metadata[key]
               for key in ('compiler', 'cmake', 'compile_definitions')) or not metadata.get('compile_flags'):
        raise ValueError(f'Incomplete build parameters: {image}; rebuild it')
    if metadata.get('options', {}).get('DEV_MODE'):
        raise ValueError(f'{image} was built with DEV_MODE=ON; rebuild without --dev before flashing')
    try:
        stamp = datetime.fromisoformat(metadata.get('built_at_utc'))
        if stamp.tzinfo is None:
            raise ValueError('Missing timezone')
    except (TypeError, ValueError) as error:
        raise ValueError(f'Missing or invalid image build timestamp: {image}; rebuild it') from error
    return metadata


## @brief Merge settings and image provenance into a bounded, padded flash parameter image.
def pack(source: Path, output: Path, images=(), *, existing=None, replace=False,
         target=None, connect_mode='HOTPLUG', root=None) -> None:
    template = load_json(source.read_text(encoding='utf-8'))
    try:
        previous = read_existing(existing) if existing else {}
    except ValueError:
        if not replace or existing.stat().st_size != MAX_JSON_SIZE:
            raise
        print('Replacing invalid stored parameters with the explicitly selected settings.')
        previous = {}
    template = {key: value for key, value in template.items() if key not in GENERATED_KEYS}
    if replace:
        # An explicit file replaces editable settings, preserving other slots'
        # build records. Explicit replacement also recovers invalid stored JSON.
        data = deepcopy(template)
        if isinstance(previous.get('build'), dict):
            data['build'] = deepcopy(previous['build'])
    else:
        data = merge(template, previous)
    data['schema_version'] = 2
    data['repository'] = repository_state(root or Path(__file__).resolve().parent.parent)
    data['build'] = data.get('build', {})
    if not isinstance(data['build'], dict):
        raise ValueError('Stored build parameters must be an object')
    generated_at = datetime.now(timezone.utc).isoformat()
    # Always record this artifact's build time, even for standalone packing
    # without --target. Selected firmware records are refreshed for this flash;
    # preserve their image build dates separately from the record build time.
    data['parameter_build'] = {
        'built_at_utc': generated_at,
        'source': source.name,
    }
    updated_images = []
    for label, image in images:
        if label not in IMAGE_ADDRESSES:
            raise ValueError(f'Unknown firmware slot: {label}')
        metadata = image_metadata(image)
        data['build'][label] = {
            **metadata,
            'image_built_at_utc': metadata['built_at_utc'],
            'built_at_utc': generated_at,
            'flash_address': IMAGE_ADDRESSES[label],
            'programmed_at_utc': generated_at,
        }
        updated_images.append(label)
    if target:
        data['flash'] = {
            'parameters_generated_at_utc': generated_at,
            'target': target,
            'updated_images': updated_images,
            'connection_mode': connect_mode,
            'parameters_address': '0x90000000',
            'parameters_region_bytes': REGION_SIZE,
        }
    # Compact JSON keeps expanded records for all three slots within the
    # firmware's existing 16 KiB reader capacity. A sidecar stays human-readable.
    payload = (json.dumps(data, ensure_ascii=False, allow_nan=False,
                          separators=(',', ':')) + '\n').encode('utf-8')
    if len(payload) + 1 > MAX_JSON_SIZE:
        raise ValueError('Parameters exceed the app startup reader limit of 16 KiB')
    output.parent.mkdir(parents=True, exist_ok=True)
    output.with_suffix('.json').write_text(json.dumps(data, indent=2, ensure_ascii=False) + '\n', encoding='utf-8')
    output.write_bytes(payload + b'\0' + b'\xff' * (REGION_SIZE - len(payload) - 1))
    print(f'Parameters: {len(payload)} JSON bytes -> {output} ({REGION_SIZE} bytes)')


## @brief Parse parameter packing options and write the requested flash image and JSON sidecar.
def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--fsbl', type=Path)
    parser.add_argument('--application', type=Path, help='Application slot A image')
    parser.add_argument('--application-b', type=Path)
    parser.add_argument('--existing', type=Path, help='16 KiB parameter readback from this device')
    parser.add_argument('--replace', action='store_true', help='Replace editable fields with the source JSON')
    parser.add_argument('--target', choices=['fsbl', 'appli-a', 'appli-b', 'parameters', 'all'])
    parser.add_argument('--connect-mode', default='HOTPLUG')
    args = parser.parse_args()
    try:
        images = [(label, image) for label, image in
                  [('fsbl', args.fsbl), ('application', args.application),
                   ('application_b', args.application_b)] if image is not None]
        pack(args.source, args.output, images, existing=args.existing, replace=args.replace,
             target=args.target, connect_mode=args.connect_mode)
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        parser.exit(1, f'Parameter packing failed: {error}\n')


if __name__ == '__main__':
    main()
