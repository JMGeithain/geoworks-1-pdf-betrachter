#!/bin/sh
# Baut PDFCORE.GEO und PDFVIEW.GEO mit dem SDK pcgeos-1-sdk-v2
# (https://github.com/dnesbitt142/pcgeos-1-sdk-v2). Aufruf:
#   SDK=/pfad/zu/pcgeos-1-sdk-v2 ./build.sh
# Ergebnis in out/geo: PDFCORE.GEO (-> GEOWORKS\SYSTEM), PDFVIEW.GEO (-> WORLD)
set -e
cd "$(dirname "$0")"
HERE=$(pwd)
SDK=${SDK:-$HERE/../pcgeos-1-sdk-v2}
export SDK
# 1. Bibliothek: C-Quellen fuer Open Watcom aufbereiten (Tabellen/Strings ins
#    Segment PDFTABS), bauen, auf DS-Zugriffe pruefen
mkdir -p build
python3 tools/mkwatcom.py
cp build/pdfcore_w.c core/pdfint.h core/pdfcore.h lib/
(cd "$SDK" && perl sdk.pl build --project "$HERE/lib" --kernel-reference build/pdfview/pdfview.geo)
"$SDK/.sdk-local/toolchains/watcom/binl64/wdis" "$SDK/build/pdfcore/00-pdfcore_w.obj" > build/pdfcore.asm
sh tools/checkseg.sh build/pdfcore.asm
# 2. Programm (NASM) bauen und Symbol/Menues/Druckobjekte einsetzen
(cd "$SDK" && perl sdk.pl build --project "$HERE/app")
mkdir -p out/geo
perl icon/postbuild.pl "$SDK/build/pdfview/pdfview.geo" out/geo/PDFVIEW.GEO icon 640 460
cp "$SDK/build/pdfcore/pdfcore.geo" out/geo/PDFCORE.GEO
ls -l out/geo
