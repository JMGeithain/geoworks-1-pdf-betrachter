#!/bin/sh
# build host driver and render all test docs to PNG
set -e
cd "$(dirname "$0")/.."
gcc -g -O1 -Wall -Wno-unused-parameter -fsanitize=address,undefined -fno-sanitize=alignment -o host/pdfhost core/pdfcore.c core/pdfrend.c host/hostmain.c
rm -f out/*.dl out/*.png
for f in ${@:-testdocs/*.pdf}; do
  b=$(basename $f .pdf); echo "== $b"
  ./host/pdfhost $f out/$b 256 || true
  for d in out/$b-*.dl; do [ -f "$d" ] && python3 tools/dl2png.py $d ${d%.dl}.png >/dev/null; done
done
