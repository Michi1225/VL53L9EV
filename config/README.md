Flash parameters are editable in `flash_parameters.json` (UTF-8 JSON).

Flash just the parameters:

```sh
./scripts/flash.sh --target parameters
```

Use another JSON file while flashing an application:

```sh
./scripts/flash.sh --target appli-a --parameters /path/to/settings.json
```

`--target all` writes the default parameter file, FSBL, and Application A.
Individual firmware targets preserve parameters unless `--parameters` is given.

Layout: flash offset 0 (`0x90000000`), reserved size `0x40000` (256 KiB).
The stored image contains pretty-printed JSON, one NUL byte, then `0xFF` padding.
Updating parameters erases/replaces this entire reserved block. Keep other data
out of it. FSBL begins at offset `0x40000`; Application A at `0x100000`.

These settings are currently stored only: firmware does not yet parse/apply them.
`calibration` is empty until confirmed calibration values are provided.

Identity fields in the editable JSON:

- `device`: product, serial number, hardware revision, PCB revision, assembly revision.
- `firmware`: firmware version, release number, protocol version.
- `manufacturing`: date and batch.

Use `null` for unknown/unassigned values; the script does not invent serial numbers.

The flash script adds `build.fsbl` and `build.application` automatically. Each
contains the full Git commit hash, dirty flag (including untracked files), Git
description, UTC packaging timestamp, Debug/Release configuration, image name,
size, and SHA-256. Build provenance comes from the adjacent `.manifest.json`
created by `build.sh`, not from the repository state at flashing time. Image
hashes are checked before packing; missing/mismatched manifests require rebuilding.

After changing firmware, build/package it with `scripts/build.sh` or `--build`.
For a combined operation:

```sh
./scripts/flash.sh --target all --config Release --build
```

Editable firmware version fields are manually maintained; Git hash and image
hash identify the actual binaries independently of those labels. The generated
JSON can be inspected at the start of `build/flash_parameters.bin` up to its NUL
terminator. No credentials or private signing keys are stored.

`repository.git_hash` and `repository.git_dirty` are always generated from Git
when parameters are packed, including standalone packing without image manifests.
Their `null` values in the editable template are placeholders overwritten by the
packer. They describe the repository at parameter-generation time; the separate
`build.fsbl`/`build.application` fields describe the packaged firmware images.
