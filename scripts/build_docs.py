#!/usr/bin/env python3
"""Generate the reference manual and shared datasheet ratings from the wiki."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys
from urllib.parse import unquote, urlsplit

ROOT = Path(__file__).resolve().parents[1]
GUIDE = 'manual/magnecko-sense-user-guide.tex'
DATASHEET = 'datasheet/magnecko-sense-datasheet.tex'
MANIFEST = 'manual/generated.json'
RATINGS = 'wiki/hardware/electrical-ratings.md'
BEGIN_RATINGS = '% BEGIN GENERATED ELECTRICAL RATINGS'
END_RATINGS = '% END GENERATED ELECTRICAL RATINGS'


def digest(data):
    return hashlib.sha256(data).hexdigest()


def tex_escape(value):
    replacements = {'\\': r'\textbackslash{}', '{': r'\{', '}': r'\}',
                    '$': r'\$', '&': r'\&', '#': r'\#', '%': r'\%',
                    '_': r'\_', '~': r'\textasciitilde{}', '^': r'\textasciicircum{}'}
    return ''.join(replacements.get(char, char) for char in value)


def source_text(root, entry):
    text = (root / entry['path']).read_text(encoding='utf-8')
    if entry.get('section'):
        heading = '## ' + entry['section']
        match = re.search(r'^' + re.escape(heading) + r'\s*$', text, re.M)
        if not match:
            raise ValueError(f'Missing section: {heading}')
        text = '# ' + entry['section'] + '\n' + text[match.end():].split('\n## ', 1)[0]
    # Navigation belongs to the PDF's generated contents, not every chapter.
    text = re.sub(r'^\[Wiki home\].*$', '', text, flags=re.M)
    text = re.sub(r'^## Contents\s*\n.*?(?=^## |\Z)', '', text, flags=re.M | re.S)
    return text


def walk(value, visit):
    if isinstance(value, list):
        return [walk(item, visit) for item in value]
    if isinstance(value, dict):
        value = {key: walk(item, visit) for key, item in value.items()}
        return visit(value)
    return value


def plain(value):
    if isinstance(value, list):
        return ''.join(plain(item) for item in value)
    if not isinstance(value, dict):
        return ''
    kind, content = value.get('t'), value.get('c')
    if kind == 'Str':
        return content
    if kind in ('Space', 'SoftBreak', 'LineBreak'):
        return ' '
    if kind == 'Code':
        return content[1]
    if kind == 'RawInline':
        return re.sub(r'\\[a-zA-Z]+(?:\{\})?|[{}]', '', content[1])
    if kind in ('Link', 'Image', 'Span'):
        return plain(content[1])
    return plain(content)


def namespace(path):
    return re.sub(r'[^a-z0-9]+', '-', path.removesuffix('.md').lower()).strip('-')


def pandoc_json(pandoc, text):
    result = subprocess.run([pandoc, '-f', 'gfm', '-t', 'json'], input=text,
                            text=True, capture_output=True, check=True)
    return json.loads(result.stdout)


def prepare_page(document, entry):
    prefix = namespace(entry['path'])
    aliases = {}
    first = True
    for block in document['blocks']:
        if block['t'] != 'Header':
            continue
        level, attr, inlines = block['c']
        original = attr[0]
        attr[0] = prefix + '--' + original
        aliases[original] = attr[0]
        if first:
            if level != 1:
                raise ValueError(f"Chapter must start with H1: {entry['path']}")
            aliases[''] = attr[0]
            if entry.get('title'):
                inlines[:] = [{'t': 'Str', 'c': entry['title']}]
            first = False
        elif inlines and inlines[0].get('t') == 'Str' and re.fullmatch(r'\d+(?:\.\d+)*\.?', inlines[0]['c']):
            # The report supplies section numbers; preserve original anchor IDs.
            del inlines[0]
            if inlines and inlines[0]['t'] == 'Space':
                del inlines[0]
    if first:
        raise ValueError(f"No chapter heading: {entry['path']}")
    return aliases


def table_layout(node):
    if node.get('t') != 'Table':
        return node
    # Explicit fractional widths let longtables wrap text instead of extending
    # past the page. Pandoc's cell blocks, row groups and repeated headers remain.
    content = node['c']
    columns = content[2]
    widths = [3.0] * len(columns)
    rows = list(content[3][1]) + list(content[5][1])
    for body in content[4]:
        rows += list(body[2]) + list(body[3])
    for row in rows:
        for index, cell in enumerate(row[1]):
            if index < len(widths):
                widths[index] = max(widths[index], min(100, len(plain(cell[4]))) ** 0.65)
    base = 0.20 if len(columns) == 2 else 0.09
    total = sum(widths)
    for index, column in enumerate(columns):
        column[1] = {'t': 'ColWidth', 'c': base + (1 - base * len(columns)) * widths[index] / total}
    for row in content[3][1]:
        for cell in row[1]:
            for block in cell[4]:
                if block['t'] in ('Plain', 'Para'):
                    block['c'] = [{'t': 'Strong', 'c': block['c']}]
    return node


def transform_page(document, entry, root, aliases_by_path):
    source = (root / entry['path']).resolve()

    def visit(node):
        if node.get('t') == 'HorizontalRule':
            return {'t': 'RawBlock', 'c': ['latex', '']}
        if node.get('t') == 'Code':
            # Break at path/identifier separators while preserving literal text.
            escaped = ''.join(tex_escape(char) + (r'\allowbreak{}' if char in '/_.:-' else '')
                              for char in node['c'][1])
            return {'t': 'RawInline', 'c': ['latex', r'\texttt{' + escaped + '}']}
        if node.get('t') != 'Link':
            return table_layout(node)
        target = urlsplit(node['c'][2][0])
        if target.scheme or target.netloc:
            return node
        dest = (source.parent / unquote(target.path)).resolve() if target.path else source
        if dest in aliases_by_path:
            anchors = aliases_by_path[dest]
            anchor = unquote(target.fragment)
            if anchor not in anchors:
                raise ValueError(f"Broken chapter anchor in {entry['path']}: {node['c'][2][0]}")
            node['c'][2][0] = '#' + anchors[anchor]
            return node
        # Source files stay portable repo-relative references, not broken local
        # hyperlinks when someone reads the PDF outside this checkout.
        try:
            reference = dest.relative_to(root.resolve()).as_posix()
        except ValueError as error:
            raise ValueError(f"Reference escapes repository: {node['c'][2][0]}") from error
        label = node['c'][1]
        return {'t': 'Span', 'c': [['', [], []], label + [
            {'t': 'Space'}, {'t': 'Str', 'c': '('},
            {'t': 'RawInline', 'c': ['latex', r'\nolinkurl{' + reference + '}']},
            {'t': 'Str', 'c': ')'}]]}

    return walk(document, visit)


def ratings_latex(root):
    text = (root / RATINGS).read_text(encoding='utf-8')
    blocks = []
    section = None
    for line in text.splitlines():
        if line.startswith('## '):
            section = line[3:]
        elif line.startswith('|'):
            if blocks and blocks[-1][0] == section:
                blocks[-1][1].append(line)
            else:
                blocks.append((section, [line]))
    if len(blocks) != 3:
        raise ValueError('Expected three shared ratings tables')
    output = [BEGIN_RATINGS, r'\section{Electrical ratings}\label{sec:ratings}',
              r'\pageintro{Blank value cells are intentionally unassigned. Populate them from approved',
              r'design limits and board characterization; they do not represent zero or an established rating.}',
              r'\begingroup\renewcommand{\arraystretch}{1.65}']
    for title, lines in blocks:
        rows = [[cell.strip() for cell in line.strip().strip('|').split('|')] for line in lines]
        header, rows = rows[0], rows[2:]
        if any(len(row) != len(header) for row in rows):
            raise ValueError('Malformed ratings table')
        spec = {5: '@{}Y L{19mm}L{17mm}L{17mm}L{16mm}@{}',
                6: '@{}Y L{19mm}L{15mm}L{15mm}L{15mm}L{16mm}@{}',
                7: '@{}Y L{18mm}L{23mm}L{12mm}L{12mm}L{12mm}L{13mm}@{}'}[len(header)]
        output += [r'\subsection{' + tex_escape(title) + '}',
                   r'{\small\begin{tabularx}{\linewidth}{' + spec + '}', r'\toprule',
                   r'\rowcolor{MGSGray}' + ' & '.join(r'\thead{' + tex_escape(c) + '}' for c in header) + r' \\',
                   r'\midrule']
        for row in rows:
            # Empty strings remain empty cells, with no dashes or placeholders.
            cells = [tex_escape(c).replace('°C', r'$^{\circ}$C') for c in row]
            output.append(' & '.join(cells) + r' \\')
        output += [r'\bottomrule', r'\end{tabularx}}']
    output += [r'\endgroup', r'\source{wiki/hardware/electrical-ratings.md}', END_RATINGS]
    return '\n'.join(output)


def config_and_inputs(root):
    config = json.loads((root / 'manual/sources.json').read_text())
    listed = {entry['path'] for entry in config['chapters']} | set(config['navigation_only'])
    wiki = {p.relative_to(root).as_posix() for p in (root / 'wiki').rglob('*.md')}
    if wiki - listed:
        raise ValueError('Add new wiki pages to manual/sources.json: ' + ', '.join(sorted(wiki - listed)))
    paths = sorted(listed | {'manual/sources.json', 'manual/template.tex',
                            'scripts/build_docs.py', 'config/flash_parameters.json'})
    inputs = {path: digest((root / path).read_bytes()) for path in paths}
    return config, inputs


def check(root):
    _, inputs = config_and_inputs(root)
    manifest = json.loads((root / MANIFEST).read_text())
    if manifest.get('schema_version') != 1 or set(manifest.get('outputs', {})) != {GUIDE, DATASHEET}:
        raise ValueError('Invalid documentation manifest; run python3 scripts/build_docs.py')
    changed = sorted(path for path in inputs.keys() | manifest['inputs'].keys()
                     if inputs.get(path) != manifest['inputs'].get(path))
    changed += [path for path, expected in manifest['outputs'].items()
                if not (root / path).exists() or digest((root / path).read_bytes()) != expected]
    if changed:
        raise ValueError('Documentation is stale; run python3 scripts/build_docs.py. Changed: ' + ', '.join(changed))
    print('Wiki, generated guide, and shared datasheet ratings are in sync.')


def generate(root, pandoc):
    config, inputs = config_and_inputs(root)
    documents, aliases = [], {}
    for entry in config['chapters']:
        document = pandoc_json(pandoc, source_text(root, entry))
        aliases[(root / entry['path']).resolve()] = prepare_page(document, entry)
        documents.append((entry, document))
    body, in_appendix = [], False
    for entry, document in documents:
        document = transform_page(document, entry, root, aliases)
        if entry.get('appendix') and not in_appendix:
            body.append({'t': 'RawBlock', 'c': ['latex', r'\appendix']})
            in_appendix = True
        body += document['blocks'][:1]
        body.append({'t': 'RawBlock', 'c': ['latex', r'{\small\color{MGSMuted}Source: \nolinkurl{' + entry['path'] + r'}\par}\medskip']})
        body += document['blocks'][1:]
    combined = {'pandoc-api-version': documents[0][1]['pandoc-api-version'], 'meta': {}, 'blocks': body}
    result = subprocess.run([pandoc, '-f', 'json', '-t', 'latex', '--top-level-division=chapter',
                             '--no-highlight', '--wrap=none'], input=json.dumps(combined),
                            text=True, capture_output=True, check=True)
    identity = json.loads((root / 'config/flash_parameters.json').read_text())['device']
    template = (root / 'manual/template.tex').read_text()
    values = {'PRODUCT': identity['product'], 'BOARD': identity['board_version'],
              'DOCUMENT_ID': config['document_id'], 'REVISION': config['revision'], 'DATE': config['date']}
    for name, value in values.items():
        template = template.replace('@@' + name + '@@', tex_escape(value))
    guide = template.replace('@@BODY@@', result.stdout)
    datasheet = (root / DATASHEET).read_text()
    start, end = datasheet.index(BEGIN_RATINGS), datasheet.index(END_RATINGS) + len(END_RATINGS)
    datasheet = datasheet[:start] + ratings_latex(root) + datasheet[end:]
    (root / GUIDE).write_text(guide, encoding='utf-8')
    (root / DATASHEET).write_text(datasheet, encoding='utf-8')
    manifest = {'schema_version': 1, 'inputs': inputs,
                'outputs': {path: digest((root / path).read_bytes()) for path in (GUIDE, DATASHEET)},
                'pandoc': subprocess.check_output([pandoc, '--version'], text=True).splitlines()[0]}
    (root / MANIFEST).write_text(json.dumps(manifest, indent=2) + '\n')
    print(f'Generated {GUIDE} from {len(documents)} chapters, plus shared datasheet rating tables.')


def build_pdfs(root, engine):
    for source in (GUIDE, DATASHEET):
        tex = root / source
        out = tex.parent / 'build'
        out.mkdir(exist_ok=True)
        if 'tectonic' in Path(engine).name:
            command = [engine, '--untrusted', '--keep-logs', '--outdir', str(out), str(tex)]
            passes = 1
        else:
            command = [engine, '-halt-on-error', '-interaction=nonstopmode', '-no-shell-escape',
                       '-output-directory=' + str(out), str(tex)]
            passes = 3
        log = out / 'build.log'
        with log.open('w') as stream:
            for _ in range(passes):
                result = subprocess.run(command, cwd=tex.parent, stdout=stream, stderr=subprocess.STDOUT)
                if result.returncode:
                    raise ValueError(f'PDF build failed; see {log}')
        print('PDF: ' + str(out / tex.with_suffix('.pdf').name))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true', help='Check freshness without Pandoc, TeX, or file writes')
    parser.add_argument('--pdf', action='store_true', help='Also compile both PDFs')
    parser.add_argument('--pandoc', default='pandoc', help='Pandoc executable')
    parser.add_argument('--tex-engine', help='Tectonic, LuaLaTeX, or XeLaTeX executable')
    args = parser.parse_args()
    try:
        if args.check:
            if args.pdf:
                raise ValueError('--check and --pdf are mutually exclusive')
            check(ROOT)
            return
        pandoc = shutil.which(args.pandoc)
        if not pandoc:
            raise ValueError('Pandoc is required to generate LaTeX; install it or pass --pandoc /path/to/pandoc')
        generate(ROOT, pandoc)
        if args.pdf:
            engine = shutil.which(args.tex_engine) if args.tex_engine else next(
                (path for name in ('tectonic', 'lualatex', 'xelatex') if (path := shutil.which(name))), None)
            if not engine:
                raise ValueError('PDF build needs Tectonic, LuaLaTeX, or XeLaTeX; use --tex-engine /path/to/engine')
            build_pdfs(ROOT, engine)
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        if isinstance(error, subprocess.CalledProcessError) and error.stderr:
            print(error.stderr.strip(), file=sys.stderr)
        parser.exit(1, str(error) + '\n')


if __name__ == '__main__':
    main()
