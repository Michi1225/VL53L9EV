# Magnecko Sense User Guide and Reference Manual

[magnecko-sense-user-guide.tex](magnecko-sense-user-guide.tex) is a standalone
LaTeX reference manual with the project's red document styling, a linked table
of contents, numbered chapters, repeating headers, and wrapped code examples.
It includes every technical wiki page and the root README's hardware revision
standard. Dependencies and the historical flash-layout proposal are appendices;
the proposal is not the implemented memory map.

## Edit once, update both formats

The **wiki is the maintained source**. Edit the relevant Markdown page, then run
from the repository root:

```sh
python3 scripts/build_docs.py
```

This requires [Pandoc](https://pandoc.org/installing.html) 3.x. It regenerates
the complete manual and the electrical section of the
[datasheet](../datasheet/README.md). The ratings are shared from
[electrical-ratings.md](../wiki/hardware/electrical-ratings.md); empty value cells
remain empty in both documents. Edit other datasheet sections directly when the
corresponding specifications or behavior change.

`sources.json` selects chapter order, appendix boundaries, titles, and document
revision/date. Add new technical wiki pages there; generation fails if a page is
missing from the list. Navigation pages are represented by the PDF contents.
Edit `template.tex` for document styling. Do not edit the generated manual or the
datasheet block between `BEGIN/END GENERATED ELECTRICAL RATINGS` markers.

Check that everything is current without installing Pandoc or TeX:

```sh
python3 scripts/build_docs.py --check
```

The committed `generated.json` records source and output hashes. CI checks it
and rejects stale documents. Keep regenerated `.tex` files and the manifest in
the same change as the wiki edits. Generated PDFs and TeX auxiliary files are
local build products and are ignored by Git.

## Build PDFs

Install Pandoc 3.x, DejaVu Sans and DejaVu Sans Mono, plus **Tectonic** or a
TeX Live/MiKTeX installation with **LuaLaTeX** or **XeLaTeX**. Then run:

```sh
python3 scripts/build_docs.py --pdf
```

This also regenerates the sources. The script selects Tectonic, LuaLaTeX, then
XeLaTeX in that order, or accepts explicit executables:

```sh
python3 scripts/build_docs.py --pdf --pandoc /path/to/pandoc --tex-engine /path/to/tectonic
```

The VS Code task **Docs: Build PDFs** runs the same command. Outputs:

- `manual/build/magnecko-sense-user-guide.pdf`
- `datasheet/build/magnecko-sense-datasheet.pdf`

Each output directory also contains `build.log` and the TeX engine's log. On
Debian/Ubuntu, a LuaLaTeX setup typically uses `pandoc texlive-luatex
texlive-latex-extra texlive-pictures texlive-fonts-recommended fonts-dejavu-core`.
Tectonic downloads its TeX bundle on first use. The script does not install tools.

The committed manual `.tex` also compiles independently, without Pandoc or wiki
files, using Tectonic, LuaLaTeX, or XeLaTeX. For example:

```sh
mkdir -p manual/build
tectonic --outdir manual/build manual/magnecko-sense-user-guide.tex
```

The manual uses Unicode fonts and requires one of those engines. The datasheet
continues to support pdfLaTeX as described in its own build instructions.
