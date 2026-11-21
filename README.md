# PDF-Betrachter für GeoWorks Ensemble 1.2

Ein PDF-Betrachter für das deutsche GeoWorks Ensemble 1.2 (Kernel 1.28):
Text, Vektorgrafik, Bilder (auch JPEG und Transparenz), PDF 1.0–1.7,
Drucken über den GeoWorks-Druckdialog, Echtfarben mit 64K-Bildschirmtreibern.

![Programmsymbol](icon/preview.png)

- **Fertige Dateien:** [`dist/`](dist/) (`PDFCORE.GEO` → `GEOWORKS\SYSTEM`,
  `PDFVIEW.GEO` → `GEOWORKS\WORLD`), Anleitung in `dist/LIESMICH.TXT`
- **Alles Wissenswerte:** [DOSSIER.md](DOSSIER.md): Funktionen, Aufbau,
  Erkenntnisse über GeoWorks 1, Bauanleitung, offene Punkte
- **Bauen:** `SDK=/pfad/zu/pcgeos-1-sdk-v2 ./build.sh` mit dem SDK
  [pcgeos-1-sdk-v2](https://github.com/dnesbitt142/pcgeos-1-sdk-v2)

| Ordner | Inhalt |
|---|---|
| `core/` | PDF-Kern in portablem C (läuft auch auf dem PC) |
| `lib/` | Bibliotheksprojekt für Open Watcom (PDFCORE.GEO) |
| `app/` | Programm in NASM (PDFVIEW.GEO) |
| `icon/` | Symbol und Nachbearbeitung der GEO-Datei (Menüs, Druckobjekt) |
| `host/`, `tools/` | Testtreiber für den PC, Anzeigeliste → PNG, Prüfskripte |
| `testdocs/`, `testdocs15/` | Testdokumente |
