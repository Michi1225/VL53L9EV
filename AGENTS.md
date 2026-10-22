# Documentation maintenance

When changing documented hardware, firmware, protocols, or desktop tools, update
the relevant `wiki/` pages and the corresponding manually maintained datasheet
sections in the same change. Keep the user guide and wiki synchronized by running
`python3 scripts/build_docs.py` after documentation edits, then
`python3 scripts/build_docs.py --check`.

The wiki is the source for `manual/magnecko-sense-user-guide.tex`. Add new technical
wiki pages to `manual/sources.json`. Edit `manual/template.tex` for layout changes,
not the generated manual. Electrical ratings are shared from
`wiki/hardware/electrical-ratings.md`; do not edit the generated ratings block in
the datasheet separately or infer unassigned values. See `manual/README.md` for
dependencies and PDF build instructions. Commit generated LaTeX and
`manual/generated.json` with their source changes; PDFs remain local build output.
