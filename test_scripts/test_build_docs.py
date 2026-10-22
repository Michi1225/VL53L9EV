"""Regressions for portable cross-references and synchronized documentation."""
from contextlib import redirect_stdout
from copy import deepcopy
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
import build_docs as docs


def header(identifier, text, level=1):
    return {'t': 'Header', 'c': [level, [identifier, [], []], [{'t': 'Str', 'c': text}]]}


def link(target):
    return {'t': 'Link', 'c': [['', [], []], [{'t': 'Str', 'c': 'See reference'}], [target, '']]}


class DocumentationTests(unittest.TestCase):
    def test_chapter_links_resolve_with_duplicate_heading_names(self):
        a = {'path': 'wiki/firmware/a.md'}
        b = {'path': 'wiki/hardware/b.md'}
        first = {'blocks': [header('overview', 'A'), header('setup', 'Setup', 2),
                            link('#setup'), link('../hardware/b.md#setup'), link('../hardware/b.md')]}
        second = {'blocks': [header('overview', 'B'), header('setup', 'Setup', 2)]}
        aliases = {(ROOT / a['path']).resolve(): docs.prepare_page(first, a),
                   (ROOT / b['path']).resolve(): docs.prepare_page(second, b)}
        result = docs.transform_page(first, a, ROOT, aliases)
        targets = [node['c'][2][0] for node in result['blocks'] if node['t'] == 'Link']
        self.assertEqual(targets, ['#wiki-firmware-a--setup', '#wiki-hardware-b--setup',
                                   '#wiki-hardware-b--overview'])
        self.assertNotEqual(first['blocks'][1]['c'][1][0], second['blocks'][1]['c'][1][0])

    def test_missing_chapter_anchor_and_escaping_source_are_rejected(self):
        entry = {'path': 'wiki/a.md'}
        aliases = {(ROOT / entry['path']).resolve(): {'': 'chapter'}}
        for target, message in [('#missing', 'Broken chapter anchor'),
                                ('../../outside.c', 'Reference escapes repository')]:
            with self.subTest(target=target), self.assertRaisesRegex(ValueError, message):
                docs.transform_page({'blocks': [link(target)]}, entry, ROOT, aliases)

    def test_external_links_and_portable_source_references(self):
        entry = {'path': 'wiki/a.md'}
        external = link('https://example.com/reference#setup')
        result = docs.transform_page({'blocks': [deepcopy(external), link('../Appli/Core/Src/main.c')]},
                                     entry, ROOT, {})
        self.assertEqual(result['blocks'][0], external)
        self.assertIn(r'\nolinkurl{Appli/Core/Src/main.c}', json.dumps(result).replace('\\\\', '\\'))

    def test_readme_section_and_navigation_do_not_duplicate_contents(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'README.md').write_text('# Home\n## Hardware revision standard\nKeep this.\n## Other\nOmit this.\n')
            extracted = docs.source_text(root, {'path': 'README.md', 'section': 'Hardware revision standard'})
            self.assertIn('Keep this.', extracted)
            self.assertNotIn('Omit this.', extracted)
            (root / 'a.md').write_text('# A\n[Wiki home](README.md) · [Full contents](SUMMARY.md)\n'
                                       '## Contents\n- [Setup](#setup)\n## Setup\nKeep setup.\n')
            extracted = docs.source_text(root, {'path': 'a.md'})
            self.assertNotIn('Wiki home', extracted)
            self.assertNotIn('## Contents', extracted)
            self.assertIn('## Setup\nKeep setup.', extracted)

    def test_ratings_preserve_empty_values_and_propagate_assigned_values(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            path = root / docs.RATINGS
            path.parent.mkdir(parents=True)
            text = ('## Absolute maximum ratings\n| Parameter | Symbol | Min | Max | Unit |\n'
                    '| --- | --- | --- | --- | --- |\n| Supply | VIN | | | V |\n'
                    '## Recommended operating conditions\n| Parameter | Symbol | Min | Typ | Max | Unit |\n'
                    '| --- | --- | --- | --- | --- | --- |\n| Supply | VIN | | | | V |\n'
                    '## Electrical characteristics\n| Parameter | Symbol | Conditions | Min | Typ | Max | Unit |\n'
                    '| --- | --- | --- | --- | --- | --- | --- |\n| Current | IIN | | | | | A |\n')
            path.write_text(text)
            generated = docs.ratings_latex(root)
            self.assertIn('Supply & VIN &  &  & V', generated)
            self.assertIn('Current & IIN &  &  &  &  & A', generated)
            path.write_text(text.replace('| VIN | | | V |', '| VIN | | 5 | V |'))
            self.assertIn('Supply & VIN &  & 5 & V', docs.ratings_latex(root))

    def test_freshness_detects_wiki_and_output_changes_without_pandoc(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            config = {'chapters': [{'path': 'wiki/a.md'}], 'navigation_only': []}
            files = {'manual/sources.json': json.dumps(config), 'wiki/a.md': '# A\n',
                     'manual/template.tex': 'template', 'scripts/build_docs.py': 'script',
                     'config/flash_parameters.json': '{}', docs.GUIDE: 'guide', docs.DATASHEET: 'datasheet'}
            for path, text in files.items():
                destination = root / path
                destination.parent.mkdir(parents=True, exist_ok=True)
                destination.write_text(text)
            _, inputs = docs.config_and_inputs(root)
            manifest = {'schema_version': 1, 'inputs': inputs,
                        'outputs': {p: docs.digest((root / p).read_bytes()) for p in (docs.GUIDE, docs.DATASHEET)}}
            (root / docs.MANIFEST).write_text(json.dumps(manifest))
            with redirect_stdout(io.StringIO()):
                docs.check(root)
            for path in ('wiki/a.md', docs.GUIDE, docs.DATASHEET):
                with self.subTest(path=path):
                    original = (root / path).read_text()
                    (root / path).write_text(original + 'changed')
                    with self.assertRaisesRegex(ValueError, 'Documentation is stale'):
                        docs.check(root)
                    (root / path).write_text(original)
            (root / 'wiki/new.md').write_text('# New\n')
            with self.assertRaisesRegex(ValueError, 'Add new wiki pages'):
                docs.check(root)


if __name__ == '__main__':
    unittest.main()
