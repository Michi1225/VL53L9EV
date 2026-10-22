"""Build provenance and persistent flash workflow, using a simulated programmer."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
from datetime import datetime, timezone
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
import build_provenance as provenance
import pack_parameters as parameters

REPOSITORY = dict(git_hash='a' * 40, git_dirty=False, git_description='test',
                  git_branch='main', submodules=[])


## @brief Write a valid image provenance fixture with optional field overrides.
def manifest(image, **changes):
    data = dict(manifest_version=2, **REPOSITORY, built_at_utc='2026-01-01T00:00:00+00:00',
                configuration='Release', image=image.name, image_size_bytes=image.stat().st_size,
                image_sha256=hashlib.sha256(image.read_bytes()).hexdigest(),
                compiler={'name': 'arm-none-eabi-gcc', 'version': 'test gcc', 'target': 'arm-none-eabi'},
                cmake={'version': '3.22.1', 'generator': 'Ninja', 'toolchain': 'test.cmake'},
                options={'DEV_MODE': False}, compile_source='Core/Src/main.c',
                compile_flags=['-mcpu=cortex-m55', '-O3'], compile_definitions={'STM32N657xx': '1'})
    data.update(changes)
    image.with_suffix('.manifest.json').write_text(json.dumps(data))
    return data


## @brief Encode parameter JSON as a padded flash readback fixture.
def readback(data):
    payload = json.dumps(data).encode() + b'\0'
    return payload + b'\xff' * (parameters.MAX_JSON_SIZE - len(payload))


class ParameterPackingTests(unittest.TestCase):
    ## @brief Prepare isolated files and fixtures for the next test.
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.path = Path(self.temp.name)
        self.source = self.path / 'source.json'
        self.source.write_text(json.dumps({'schema_version': 2,
                                          'device': {'serial_number': None, 'name': None},
                                          'firmware': {'version': '0.2.0'}, 'calibration': {}}))
        self.output = self.path / 'packed.bin'
        self.existing = self.path / 'old.bin'
        self.old = dict(device={'serial_number': 'BOARD-42', 'name': 'Bench'},
                        firmware={'version': '0.1.0'}, calibration={'bias': [1, 2, 3]},
                        build={'fsbl': {'image_sha256': 'previous-fsbl'},
                               'application': {'image_sha256': 'previous-a'}})
        self.existing.write_bytes(readback(self.old))
        self.image = self.path / 'application-signed.bin'
        self.image.write_bytes(b'signed application')
        self.metadata = manifest(self.image)

    ## @brief Run parameter packing with deterministic repository metadata and decode its output.
    def pack(self, images=(), **kwargs):
        with patch.object(parameters, 'repository_state', return_value=REPOSITORY):
            parameters.pack(self.source, self.output, images, existing=self.existing, **kwargs)
        return json.loads(self.output.read_bytes().split(b'\0', 1)[0])

    ## @brief Verify partial update preserves identity calibration and other slot.
    def test_partial_update_preserves_identity_calibration_and_other_slot(self):
        data = self.pack([('application_b', self.image)], target='appli-b')
        self.assertEqual(data['device']['serial_number'], 'BOARD-42')
        self.assertEqual(data['calibration'], self.old['calibration'])
        self.assertEqual(data['build']['fsbl'], self.old['build']['fsbl'])
        self.assertEqual(data['build']['application'], self.old['build']['application'])
        self.assertEqual(data['build']['application_b']['flash_address'], '0x90900000')
        self.assertEqual(data['build']['application_b']['configuration'], 'Release')
        self.assertEqual(data['build']['application_b']['compiler'], self.metadata['compiler'])
        self.assertEqual(data['flash']['updated_images'], ['application_b'])
        self.assertEqual(len(self.output.read_bytes()), parameters.REGION_SIZE)
        self.assertEqual(json.loads(self.output.with_suffix('.json').read_text()), data)
        self.assertLess(len(self.output.read_bytes().split(b'\0')[0]), parameters.MAX_JSON_SIZE)

    ## @brief Verify parameter build time refreshes without relabelling firmware.
    def test_parameter_build_time_refreshes_without_relabelling_firmware(self):
        self.old['parameter_build'] = {'built_at_utc': '2020-01-01T00:00:00+00:00'}
        self.existing.write_bytes(readback(self.old))
        with patch.object(parameters, 'datetime') as clock:
            clock.now.return_value = datetime(2026, 10, 16, 12, 0, tzinfo=timezone.utc)
            first = self.pack()
            clock.now.return_value = datetime(2026, 10, 16, 13, 0, tzinfo=timezone.utc)
            second = self.pack(target='parameters')
        self.assertEqual(first['parameter_build']['built_at_utc'], '2026-10-16T12:00:00+00:00')
        self.assertEqual(second['parameter_build']['built_at_utc'], '2026-10-16T13:00:00+00:00')
        self.assertEqual(second['parameter_build']['source'], self.source.name)
        self.assertEqual(second['flash']['parameters_generated_at_utc'], second['parameter_build']['built_at_utc'])
        self.assertEqual(first['build'], self.old['build'])
        self.assertEqual(second['build'], self.old['build'])

    ## @brief Verify selected image date comes from current manifest.
    def test_selected_image_date_comes_from_current_manifest(self):
        date = '2026-10-16T11:30:00+00:00'
        manifest(self.image, built_at_utc=date)
        data = self.pack([('application', self.image)], target='appli-a')
        self.assertEqual(data['build']['application']['image_built_at_utc'], date)
        self.assertEqual(data['build']['application']['built_at_utc'], data['parameter_build']['built_at_utc'])
        self.assertEqual(data['build']['fsbl'], self.old['build']['fsbl'])

    ## @brief Verify explicit settings replace editable fields and retain provenance.
    def test_explicit_settings_replace_editable_fields_and_retain_provenance(self):
        data = self.pack(replace=True, target='parameters')
        self.assertEqual(data['device']['serial_number'], None)
        self.assertEqual(data['firmware']['version'], '0.2.0')
        self.assertEqual(data['calibration'], {})
        self.assertEqual(data['build'], self.old['build'])
        self.assertEqual(data['flash']['updated_images'], [])

    ## @brief Verify erased device uses template.
    def test_erased_device_uses_template(self):
        self.existing.write_bytes(b'\xff' * parameters.MAX_JSON_SIZE)
        data = self.pack([('application', self.image)], target='appli-a')
        self.assertIsNone(data['device']['serial_number'])
        self.assertNotIn('fsbl', data['build'])
        self.assertEqual(data['repository'], REPOSITORY)

    ## @brief Verify corrupt readback requires explicit replacement.
    def test_corrupt_readback_requires_explicit_replacement(self):
        self.existing.write_bytes(b'bad\0' + b'\xff' * (parameters.MAX_JSON_SIZE - 4))
        with self.assertRaisesRegex(ValueError, 'Stored parameters are invalid'):
            self.pack()
        self.assertFalse(self.output.exists())
        self.assertEqual(self.pack(replace=True)['device']['name'], None)
        self.existing.write_bytes(b'')
        with self.assertRaisesRegex(ValueError, 'exactly 16 KiB'):
            self.pack(replace=True)

    ## @brief Verify missing old tampered and development manifests fail.
    def test_missing_old_tampered_and_development_manifests_fail(self):
        for changes in [dict(manifest_version=1), dict(image_sha256='wrong'),
                        dict(image_size_bytes=999), dict(compiler={}), dict(options={'DEV_MODE': True}),
                        dict(built_at_utc=None), dict(built_at_utc='invalid'),
                        dict(built_at_utc='2026-01-01T00:00:00')]:
            with self.subTest(changes=changes):
                manifest(self.image, **changes)
                with self.assertRaises(ValueError):
                    self.pack([('application', self.image)])
                self.assertFalse(self.output.exists())
        self.image.with_suffix('.manifest.json').unlink()
        with self.assertRaises(OSError):
            self.pack([('application', self.image)])

    ## @brief Verify invalid or oversized json never creates flash image.
    def test_invalid_or_oversized_json_never_creates_flash_image(self):
        for value in ['{"device":{},"device":{}}', '[]', '{"value":NaN}',
                      json.dumps({'large': 'x' * parameters.MAX_JSON_SIZE})]:
            self.source.write_text(value)
            with self.assertRaises(ValueError):
                self.pack()
            self.assertFalse(self.output.exists())


class BuildProvenanceTests(unittest.TestCase):
    ## @brief Verify records actual compile flags macros options and compiler.
    def test_records_actual_compile_flags_macros_options_and_compiler(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            build = root / 'build'
            build.mkdir()
            (build / 'CMakeCache.txt').write_text('''CMAKE_BUILD_TYPE:STRING=Release
CMAKE_GENERATOR:INTERNAL=Ninja
CMAKE_TOOLCHAIN_FILE:FILEPATH=/example/gcc-arm-none-eabi.cmake
CMAKE_CACHE_MAJOR_VERSION:INTERNAL=3
CMAKE_CACHE_MINOR_VERSION:INTERNAL=22
CMAKE_CACHE_PATCH_VERSION:INTERNAL=1
DEV_MODE:BOOL=OFF
VL53L9_SENSOR_ONLY:BOOL=ON
''')
            args = ['fake-gcc', '-DSTM32N657xx', '-D', 'FEATURE=3', '-I', '/some includes',
                    '-mcpu=cortex-m55', '-Os', '-O3', '-std=gnu2x', '-o', 'main.o',
                    '-c', str(root / 'Core/Src/main.c')]
            (build / 'compile_commands.json').write_text(json.dumps([
                dict(directory=str(build), arguments=args, file=str(root / 'Core/Src/main.c'))]))
            ## @brief Emulate compiler version and preprocessor queries for provenance validation.
            def output(command, **kwargs):
                if '-dM' in command:
                    self.assertNotIn('-o', command)
                    self.assertNotIn('-c', command)
                    self.assertIn('/some includes', command)
                    return '#define TX_TIMER_TICKS_PER_SECOND 1000\n#define TX_ENABLE_EXECUTION_CHANGE_NOTIFY \n'
                if '--version' in command:
                    return 'Compiler test version\nCopyright\n'
                return 'arm-none-eabi\n'
            with patch.object(provenance.subprocess, 'check_output', side_effect=output):
                data = provenance.build_settings(build, 'Release')
            self.assertEqual(data['options'], {'DEV_MODE': False, 'VL53L9_SENSOR_ONLY': True})
            self.assertEqual(data['compile_flags'], ['-mcpu=cortex-m55', '-Os', '-O3', '-std=gnu2x'])
            self.assertEqual(data['compile_definitions'], {'STM32N657xx': '1', 'FEATURE': '3'})
            self.assertEqual(data['rtos_definitions']['TX_ENABLE_EXECUTION_CHANGE_NOTIFY'], '1')
            self.assertEqual(data['compiler']['version'], 'Compiler test version')
            with self.assertRaisesRegex(ValueError, 'configuration'):
                provenance.build_settings(build, 'Debug')


PROGRAMMER = r'''#!/usr/bin/env python3
import json
import os
from pathlib import Path
import sys
args = sys.argv[1:]
log = Path(os.environ['FAKE_LOG'])
with log.open('a') as file:
    file.write(json.dumps(args) + '\n')
state = Path(os.environ['FAKE_STATE'])
boot = state.with_suffix('.boot.bin')
if '-u' in args:
    index = args.index('-u')
    address = args[index + 1]
    data = (boot if address == '0x91100000' else state).read_bytes()[:int(args[index + 2])]
    if address == '0x91100000':
        if os.environ.get('FAKE_BOOT_SHORT'):
            data = data[:-1]
        if os.environ.get('FAKE_BOOT_VERIFY_BAD') and data == b'\xff' * 524288:
            data = data[:-1] + b'\0'
    Path(args[index + 3]).write_bytes(data)
if '-w' in args:
    index = args.index('-w')
    image, address = Path(args[index + 1]), args[index + 2]
    if address == os.environ.get('FAKE_FAIL_ADDRESS'):
        sys.exit(1)
    if address == '0x90000000':
        state.write_bytes(image.read_bytes())
    if address == '0x91100000':
        assert image.read_bytes() == b'\xff' * 524288
        boot.write_bytes(image.read_bytes())
'''


class FlashWorkflowTests(unittest.TestCase):
    ## @brief Prepare isolated files and fixtures for the next test.
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        (self.root / 'scripts').mkdir()
        for name in ['flash.sh', 'pack_parameters.py', 'build_provenance.py', 'firmware_update.py']:
            shutil.copyfile(ROOT / 'scripts' / name, self.root / 'scripts' / name)
        (self.root / 'config').mkdir()
        shutil.copyfile(ROOT / 'config/flash_parameters.json', self.root / 'config/flash_parameters.json')
        self.loader = self.root / 'ExtMemLoader/build/magnecko-sense-fw_ExtMemLoader.stldr'
        self.loader.parent.mkdir(parents=True)
        self.loader.write_bytes(b'fake external loader')
        self.images = {}
        for label, name in [('fsbl', 'FSBL/build/magnecko-sense-fw_FSBL-trusted.bin'),
                            ('application', 'Appli/build/magnecko-sense-fw_Appli-signed.bin')]:
            image = self.root / name
            image.parent.mkdir(parents=True)
            image.write_bytes(label.encode())
            if label == 'application':
                raw = bytearray(2048)
                for offset, value in [(0, 0x324d5453), (0x68, 0x20300), (0x6c, 2048-0x240),
                                      (0x70, 0x34000409), (0x400, 0x34200000), (0x404, 0x34000409)]:
                    struct.pack_into('<I', raw, offset, value)
                struct.pack_into('<I', raw, 0x64, sum(raw[0x240:]))
                image.write_bytes(raw)
            manifest(image)
            self.images[label] = image
        self.programmer = self.root / 'fake programmer'
        self.programmer.write_text(PROGRAMMER)
        self.programmer.chmod(0o755)
        self.log = self.root / 'programmer.jsonl'
        self.state = self.root / 'device.bin'
        self.state.write_bytes(readback({'device': {'serial_number': 'BOARD-42'},
                                        'build': {'fsbl': {'image_sha256': 'original'}}}))
        self.boot = self.state.with_suffix('.boot.bin')
        self.boot.write_bytes(bytes(range(256)) * 2048)
        self.env = dict(os.environ, STM32_PROGRAMMER_CLI=str(self.programmer),
                        FAKE_LOG=str(self.log), FAKE_STATE=str(self.state), PYTHONDONTWRITEBYTECODE='1')
        subprocess.run(['git', 'init', '-q', str(self.root)], check=True)
        subprocess.run(['git', '-C', str(self.root), '-c', 'user.name=Test', '-c', 'user.email=test@example.com',
                        'commit', '-q', '--allow-empty', '-m', 'Fixture'], check=True)

    ## @brief Execute the flash script against the simulated programmer with the selected options.
    def run_flash(self, target, *extra, fail_address=None, **environment):
        env = dict(self.env)
        env.update(environment)
        if fail_address:
            env['FAKE_FAIL_ADDRESS'] = fail_address
        return subprocess.run(['bash', str(self.root / 'scripts/flash.sh'), '--target', target, *extra],
                              text=True, capture_output=True, env=env)

    ## @brief Read the programmer invocation records captured by the test.
    def calls(self):
        return [json.loads(line) for line in self.log.read_text().splitlines()]

    ## @brief Return the flash addresses written by the simulated programmer.
    def writes(self):
        return [call[call.index('-w') + 2] for call in self.calls() if '-w' in call]

    ## @brief Decode the simulated device parameter block.
    def data(self):
        return json.loads(self.state.read_bytes().split(b'\0', 1)[0])

    ## @brief Verify every target refreshes parameters after verified firmware.
    def test_every_target_refreshes_parameters_after_verified_firmware(self):
        for target, addresses, images in [
                ('fsbl', ['0x90040000', '0x90000000'], ['fsbl']),
                ('appli-a', ['0x90100000', '0x90000000'], ['application']),
                ('appli-b', ['0x90900000', '0x90000000'], ['application_b']),
                ('all', ['0x90040000', '0x90100000', '0x90000000'], ['fsbl', 'application']),
                ('parameters', ['0x90000000'], [])]:
            with self.subTest(target=target):
                self.log.unlink(missing_ok=True)
                result = self.run_flash(target)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertIn('-u', self.calls()[0])
                self.assertEqual(self.writes(), addresses)
                self.assertTrue(all('-v' in call for call in self.calls() if '-w' in call))
                self.assertEqual(self.data()['flash']['updated_images'], images)
                if target != 'parameters':
                    self.assertEqual(self.data()['device']['serial_number'], 'BOARD-42')

    ## @brief Verify single image does not need other local images or manifests.
    def test_single_image_does_not_need_other_local_images_or_manifests(self):
        self.images['fsbl'].unlink()
        self.images['fsbl'].with_suffix('.manifest.json').unlink()
        result = self.run_flash('appli-a')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.data()['build']['fsbl'], {'image_sha256': 'original'})
        # Default --config Debug must not relabel the existing Release image.
        self.assertEqual(self.data()['build']['application']['configuration'], 'Release')

    ## @brief Verify firmware flashes refresh selected build dates even for same image.
    def test_firmware_flashes_refresh_selected_build_dates_even_for_same_image(self):
        for target, slots in [('fsbl', ['fsbl']), ('appli-a', ['application']),
                              ('appli-b', ['application_b']), ('all', ['fsbl', 'application'])]:
            with self.subTest(target=target):
                previous = {slot: {'built_at_utc': '2020-01-01T00:00:00+00:00',
                                   'image_sha256': slot}
                            for slot in ('fsbl', 'application', 'application_b')}
                self.state.write_bytes(readback({'build': previous}))
                for _ in range(2):
                    before = datetime.now(timezone.utc)
                    result = self.run_flash(target)
                    after = datetime.now(timezone.utc)
                    self.assertEqual(result.returncode, 0, result.stderr)
                    data = self.data()
                    for slot in previous:
                        record = data['build'][slot]
                        if slot in slots:
                            stamp = datetime.fromisoformat(record['built_at_utc'])
                            self.assertLessEqual(before, stamp)
                            self.assertLessEqual(stamp, after)
                            self.assertNotEqual(record['built_at_utc'], previous[slot]['built_at_utc'])
                            self.assertEqual(record['programmed_at_utc'], record['built_at_utc'])
                            self.assertEqual(record['built_at_utc'], data['parameter_build']['built_at_utc'])
                            self.assertEqual(record['image_built_at_utc'], '2026-01-01T00:00:00+00:00')
                        else:
                            self.assertEqual(record, previous[slot])
                    previous = data['build']

    ## @brief Verify parameter only does not need any firmware images.
    def test_parameter_only_does_not_need_any_firmware_images(self):
        for image in self.images.values():
            image.unlink()
            image.with_suffix('.manifest.json').unlink()
        result = self.run_flash('parameters')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.writes(), ['0x90000000'])
        self.assertEqual(self.data()['build']['fsbl']['image_sha256'], 'original')

    ## @brief Verify validation failure prevents all writes.
    def test_validation_failure_prevents_all_writes(self):
        self.images['application'].write_bytes(b'tampered')
        result = self.run_flash('all')
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('does not match build manifest', result.stderr)
        self.assertEqual(self.writes(), [])

    ## @brief Verify short readback prevents all writes even with explicit settings.
    def test_short_readback_prevents_all_writes_even_with_explicit_settings(self):
        self.state.write_bytes(b'short')
        result = self.run_flash('all', '--parameters', str(self.root / 'config/flash_parameters.json'))
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('exactly 16 KiB', result.stderr)
        self.assertEqual(self.writes(), [])

    ## @brief Verify failed firmware write keeps previous parameter block.
    def test_failed_firmware_write_keeps_previous_parameter_block(self):
        before = self.state.read_bytes()
        result = self.run_flash('all', fail_address='0x90100000')
        self.assertNotEqual(result.returncode, 0)
        self.assertNotIn('0x90000000', self.writes())
        self.assertEqual(self.state.read_bytes(), before)

    def test_reprovision_backs_up_then_resets_only_boot_metadata_after_verified_images(self):
        for target in ('all', 'appli-a'):
            with self.subTest(target=target):
                self.log.unlink(missing_ok=True)
                before = self.boot.read_bytes()
                result = self.run_flash(target, '--reset-boot-metadata')
                self.assertEqual(result.returncode, 0, result.stderr)
                expected = ['0x90040000'] if target == 'all' else []
                self.assertEqual(self.writes(), expected + ['0x90100000', '0x90000000', '0x91100000'])
                calls = self.calls()
                readbacks = [i for i, call in enumerate(calls) if '-u' in call and '0x91100000' in call]
                write_indexes = [i for i, call in enumerate(calls) if '-w' in call]
                self.assertEqual(len(readbacks), 2)
                self.assertLess(readbacks[0], write_indexes[0])
                self.assertGreater(readbacks[1], write_indexes[-1])
                backup_call = calls[readbacks[0]]
                self.assertEqual(backup_call[backup_call.index('-u')+2], '524288')
                backup = Path(backup_call[-1])
                self.assertEqual(backup.read_bytes(), before)
                self.assertEqual(self.boot.read_bytes(), b'\xff' * 524288)
                self.assertEqual(self.data()['device']['serial_number'], 'BOARD-42')
                self.assertTrue(all('-v' in call for call in calls if '-w' in call))

    def test_boot_metadata_reset_is_explicit_and_restricted_to_slot_a_baseline(self):
        original = self.boot.read_bytes()
        result = self.run_flash('all')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.boot.read_bytes(), original)
        self.assertNotIn('0x91100000', self.writes())
        for target in ('fsbl', 'appli-b', 'parameters'):
            self.log.unlink(missing_ok=True)
            result = self.run_flash(target, '--reset-boot-metadata')
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('requires --target all or appli-a', result.stderr)
            self.assertFalse(self.log.exists())

    def test_reprovision_requires_complete_backup_and_verified_firmware_and_parameters(self):
        original = self.boot.read_bytes()
        result = self.run_flash('all', '--reset-boot-metadata', FAKE_BOOT_SHORT='1')
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('exactly 512 KiB', result.stderr)
        self.assertEqual(self.writes(), [])
        for address in ('0x90040000', '0x90100000', '0x90000000'):
            self.log.unlink(missing_ok=True)
            result = self.run_flash('all', '--reset-boot-metadata', fail_address=address)
            self.assertNotEqual(result.returncode, 0)
            self.assertNotIn('0x91100000', self.writes())
            self.assertEqual(self.boot.read_bytes(), original)
        self.assertTrue(list((self.root / 'build').glob('boot-metadata-backup.*.bin')))
        self.log.unlink(missing_ok=True)
        result = self.run_flash('all', '--reset-boot-metadata', fail_address='0x91100000')
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(self.boot.read_bytes(), original)
        metadata_reads = [call for call in self.calls() if '-u' in call and '0x91100000' in call]
        self.assertEqual(len(metadata_reads), 1)  # Backup only; no successful reset/readback claimed.

    def test_reprovision_rejects_invalid_image_and_checks_erased_readback(self):
        image = self.images['application']
        valid = image.read_bytes()
        image.write_bytes(b'invalid header')
        manifest(image)
        result = self.run_flash('all', '--reset-boot-metadata')
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('valid persistent application', result.stderr)
        self.assertEqual(self.writes(), [])
        image.write_bytes(valid)
        manifest(image)
        result = self.run_flash('all', '--reset-boot-metadata', FAKE_BOOT_VERIFY_BAD='1')
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('did not read back as erased', result.stderr)


if __name__ == '__main__':
    unittest.main()
