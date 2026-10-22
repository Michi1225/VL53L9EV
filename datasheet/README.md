# Magnecko Sense datasheet

[`magnecko-sense-datasheet.tex`](magnecko-sense-datasheet.tex) is a standalone,
board datasheet with a two-column opening, red section accents,
repeating document headers, specification tables, and TikZ vector diagrams.
The original Magnecko layout takes inspiration from semiconductor datasheets,
including [TI's datasheet layout](https://www.ti.com/lit/gpn/lp5815).
It needs no external images or proprietary fonts.

Build from this directory with a TeX Live or MiKTeX installation:

```sh
latexmk -pdf -interaction=nonstopmode -halt-on-error -outdir=build magnecko-sense-datasheet.tex
```

Or run pdfLaTeX twice to resolve page references:

```sh
mkdir -p build
pdflatex -interaction=nonstopmode -halt-on-error -output-directory=build magnecko-sense-datasheet.tex
pdflatex -interaction=nonstopmode -halt-on-error -output-directory=build magnecko-sense-datasheet.tex
```

Tectonic also resolves references automatically:

```sh
mkdir -p build
tectonic --outdir build magnecko-sense-datasheet.tex
```

The output is `build/magnecko-sense-datasheet.pdf`. The source can also be
uploaded directly to Overleaf using pdfLaTeX. On Debian/Ubuntu, the usual package
set is `latexmk texlive-latex-extra texlive-pictures texlive-fonts-recommended`.

Edit the identity, document revision, and date macros near the top of the source.
Hardware identity must agree with `common/board/board_identity.h`,
`config/flash_parameters.json`, and the CubeMX USB product string. Update
specifications against the listed firmware sources and the wiki when behavior
changes. Most sections are maintained manually, not generated from the firmware.

Absolute maximum ratings, recommended operating conditions, and electrical
characteristics are shared with the wiki and
[User Guide and Reference Manual](../manual/README.md). Their value cells are
intentionally blank. Edit [the wiki ratings page](../wiki/hardware/electrical-ratings.md),
then run `python3 scripts/build_docs.py` from the repository root. Do not edit
the generated ratings block directly. After any datasheet edit, regenerate and
run `python3 scripts/build_docs.py --check` to refresh and verify the manifest.

This is preliminary engineering documentation. Firmware validation bounds and
configured sampling rates are labeled separately from measured performance.
Supply ratings, optical limits, thermal ratings, pinout, and mechanical dimensions
need board drawings and characterization before they can be published as ratings.
