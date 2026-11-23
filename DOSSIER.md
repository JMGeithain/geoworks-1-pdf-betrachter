# Dossier: PDF-Betrachter für GeoWorks Ensemble 1.2

Stand: 23. November 2026, Version 0.13

## 1. Worum es geht

Der PDF-Betrachter zeigt PDF-Dateien unter dem deutschen GeoWorks Ensemble 1.2
(Kernel 1.28) an und druckt sie. Er läuft als normales GeoWorks-Programm mit
Motif-Oberfläche und Datei-Menü, wie die mitgelieferten Programme.

Vorbild war der PDF-Betrachter von Jirka Kunze für FreeGEOS. Dessen Code
läuft unter GeoWorks 1 nicht. Der Betrachter hier ist deshalb eine eigene
Neuentwicklung für die Programmierschnittstelle von GeoWorks 1.

Gebaut wird mit dem SDK [pcgeos-1-sdk-v2](https://github.com/dnesbitt142/pcgeos-1-sdk-v2)
und Open Watcom 2.0. Getestet wurde laufend in einer echten
GeoWorks-1.2-Installation in einem Emulator, mit 16-Farben- und
64K-Farben-Treibern (Basebox BB72_64 / BB96_64 / BB120_64).

Der Betrachter besteht aus zwei Dateien:

| Datei | Ort | Inhalt |
|---|---|---|
| `PDFCORE.GEO` | `GEOWORKS\SYSTEM` | Bibliothek: PDF lesen und Seiten in eine Anzeigeliste übersetzen (C, Open Watcom) |
| `PDFVIEW.GEO` | `GEOWORKS\WORLD` | Programm: Fenster, Menüs, Dateiliste, Zeichnen, Drucken (NASM) |

## 2. Was der Betrachter kann

### PDF-Format
- PDF 1.0 bis 1.7, auch Querverweis-Streams und Objekt-Streams (PDF 1.5+).
- Beschädigte Querverweistabellen werden durch Durchsuchen der Datei repariert.
- Bis zu 10.000 Objekte pro Datei.
- Filter: Flate (mit PNG-Prädiktoren), ASCII85, ASCIIHex, DCT (JPEG).
- Verschlüsselte PDFs werden nicht geöffnet.

### Text
- Die 14 Standardschriften werden auf GeoWorks-Schriften abgebildet, mit
  Breitentabellen für richtige Laufweiten.
- Umlaute und ß über WinAnsi und eigene Differences-Tabellen.
- Eingebettete Type3-Schriften, wie sie der GeoWorks-PDF-Druckertreiber
  erzeugt, werden als Umrisse gezeichnet, auch gedreht.

### Text kopieren
- Menü **Bearbeiten / Text der Seite**: ein Dialog mit dem GeoWorks-Textobjekt
  (wie im Notizblock) zeigt den Text der aktuellen Seite. Markieren und
  „Kopieren“ oder „Alles kopieren“ legt ihn in die Zwischenablage; einkleben
  z.B. in GeoWrite oder im Notizblock.
- Der Text wird aus der Anzeigeliste in der Reihenfolge der PDF gesammelt.
  Zeilenwechsel und Leerzeichen werden aus den Positionen geschätzt
  (Zeichenbreiten von Times/Helvetica); gedrehter Text, der Buchstabe für
  Buchstabe vorliegt, wird wieder zu Wörtern zusammengesetzt.

### Bedienung
- Dateiauswahl: Klick markiert, Doppelklick oder Eingabe öffnet.
- Mausrad bzw. Bild auf/ab blättern in Seite und Dateiauswahl.
- Name der offenen Datei in der Statuszeile.

### Grafik
- Pfade, Bézier-Kurven, Füllen (Nonzero/Even-Odd), Linien, Rechtecke, Clipping.
- Transformationen, Formular-XObjects mit eigener Matrix und eigenen Ressourcen.

### Farben
- Farbräume: DeviceGray/RGB/CMYK, Indexed, ICCBased, CalRGB, Separation.
- **16-Farben-Treiber:** Farben werden auf die 16 GeoWorks-Farben abgebildet,
  Bilder per geordnetem Raster.
- **64K/32K/TrueColor-Treiber:** Text, Linien und Flächen in echter RGB-Farbe,
  Bilder als 24-Bit-Bitmaps. Die Erkennung läuft automatisch über `GEOS.INI`,
  Taste `e` schaltet um.

### Bilder
- 1/2/4/8/16 Bit je Kanal, Inline-Bilder, Bildmasken (Stencil).
- JPEG (Baseline, Graustufen und YCbCr, Unterabtastung bis 2×2, Restart-Marker).
- Transparenz: SMask (Alpha), Stencil-/Mask und Farbschlüssel-Mask.
  Halbtransparente Flächen werden zu Weiß hin aufgehellt.
- Große Bilder werden in eigene Bildspeicher gelegt, siehe Abschnitt 4.3.
  Reicht der Platz nicht, zeichnet der Betrachter sie gröber (2×2 bis 8×8
  Punkte), statt sie wegzulassen.

### Bedienung
- Datei-Menü mit Öffnen, Schließen, Info, Drucken…, Faxen… und Verlassen,
  dazu ein eigenes Menü Hilfe.
- Dateiliste mit Ordnerwechsel. Sie beginnt in `DOCUMENT`, zeigt zuerst
  Ordner, dann PDFs, und hat eine Bildlaufleiste.
- Knöpfe Erste, Zurück, Weiter und Letzte, dazu Zoom −/+, Bildlaufleisten
  und Mausrad.
- Tasten:

  | Taste | Wirkung |
  |---|---|
  | `9` / `3` (Ziffernblock) | Bild auf / Bild ab |
  | `8` `2` `4` `6` | schieben |
  | `7` / `1` | erste / letzte Seite |
  | `+` / `−` | Zoom |
  | Leertaste, `n`/`p`, `j`/`k` | wie Bild ab, Seite vor/zurück, schieben |
  | `e` | Echtfarben ein/aus |

- Das Fenster lässt sich bildschirmfüllend vergrößern.
- PDF-Dateien öffnen sich per Doppelklick im GeoManager, wenn `GEOS.INI`
  wie in `dist/GEOSINI.TXT` ergänzt ist.

### Drucken
- Über den normalen GeoWorks-Druckdialog (PrintControl der Spool-Bibliothek),
  bis zu 70 Seiten, eingepasst in den bedruckbaren Bereich.
- Getestet mit dem eigenen PDF-Druckertreiber. Gedruckt wird immer in
  16 Farben.

## 3. Installation

1. GeoWorks beenden.
2. `dist/PDFCORE.GEO` nach `GEOWORKS\SYSTEM` kopieren,
   `dist/PDFVIEW.GEO` nach `GEOWORKS\WORLD`.
3. Für den Doppelklick im GeoManager in `GEOWORKS\GEOS.INI` ergänzen:
   ```
   [fileManager]
   filenameTokens = {
   *.PDF = "PDFV",0,"PDFV",0
   }
   ```
4. Testdateien aus `dist/` nach `GEOWORKS\DOCUMENT` kopieren.

## 4. Aufbau

### 4.1 Bibliothek `PDFCORE.GEO` (core/)

Der Kern ist portables C (`core/pdfcore.c`, `core/pdfrend.c`). Er läuft
unverändert auch auf dem PC (`host/hostmain.c`) und lässt sich dort mit
Sanitizern testen. Er ruft keine GeoWorks-Funktion auf. Dateizugriffe laufen
über einen Rückruf des Programms.

Schnittstelle (`core/pdfcore.h`, Exporte 0–5):

| Funktion | Zweck |
|---|---|
| `pdf_open(file, fsize, read, work, worksize, xref, xrefsize)` | Datei öffnen, Querverweise lesen |
| `pdf_page_count`, `pdf_page_size` | Seitenzahl und -größe |
| `pdf_render(work, page, scale256, dl, dlsize, &trunc)` | Seite in eine Anzeigeliste übersetzen; Bit 15 von `scale256` = Echtfarben |
| `pdf_errtext` | Fehlermeldung als Text |
| `pdf_set_ext(work, ptrs, n, size)` | externe Bildspeicher übergeben |

Speicher: Arbeitsblock 64 000 Byte (Zustand, Inflate-Fenster, Objektspeicher),
Querverweisblock 40 000 Byte, Anzeigeliste 64 000 Byte, optional ein
Bildspeicher von 64 000 Byte.

### 4.2 Anzeigeliste (Display List)

Der Kern erzeugt pro Seite eine kompakte Liste in Gerätepixeln. Das Programm
spielt sie beim Zeichnen ab, so schnell, dass Blättern im Bild nichts neu
berechnet.

| Code | Datensatz |
|---|---|
| 1 | Rechteck (Farbe, x0, y0, x1, y1), daraus entstehen auch Linien und Füllungen als Spannen |
| 2 | Text (Farbe, Schrift, Größe, x, y, Zeichen) |
| 4 | GEOS-Bitmap (mono, oder 4 Bit mit Maske; Farbbyte = Vergrößerung 2–4) |
| 5 | 24-Bit-Bild in Zeilen-„Läufen“ sichtbarer Punkte, Vergrößerung 1–8 |
| 6 | Verweis auf Datensätze in einem externen Bildspeicher |
| 0 | Ende |

Farben 0–15 sind die GeoWorks-Palette. Im Echtfarbmodus verweisen die Farben
16–255 auf eine Tabelle (r, g, b, nächste Palettenfarbe) hinter der
Anzeigeliste.

### 4.3 Bilder

1. Das Bild wird in ein Abtastraster dekodiert, höchstens so fein wie das
   Gerät:
   - 1 Bit je Punkt für Masken.
   - 1 Byte je Punkt für 16 Farben (Grau oder 6×6×6-Würfel).
   - 2 Byte je Punkt für Echtfarben (RGB565).
   - Ein Sonderwert steht jeweils für „durchsichtig“.
2. Eine SMask oder Maske wird zuerst ins selbe Raster dekodiert, danach das Bild.
3. Ausgabe:
   - 16 Farben: Bayer-Raster auf die EGA-Palette.
   - Echtfarben: 24-Bit-Läufe.
   - Die Ausgabe geht in Streifen in den externen Bildspeicher. Der Platz
     wird gerecht auf alle Bilder einer Seite verteilt. Reicht er nicht,
     wird das ganze Bild mit gröberen Punkten neu ausgegeben.
4. JPEG wird MCU-weise mit schnellen Huffman-Tabellen dekodiert, bei starker
   Verkleinerung nur über die DC-Werte.

### 4.4 Programm `PDFVIEW.GEO` (app/, icon/)

- NASM-Quelltext mit den Makros des SDK. Der Objektbaum (Menüs, PrintControl,
  Hilfe, GenView) wird von `icon/postbuild.pl` in die fertige GEO-Datei
  eingesetzt.
- Das Programmsymbol ist ein eigener Entwurf aus `icon/make_icon.py`.
- Die Hilfe (`app/help.inc`) steht als Text in `tools/help_de.txt`; `tools/gen_help.py` macht daraus `app/helptext.inc`. `test/help_preview.py` zeichnet die Hilfeseite im 8086-Emulator (Unicorn) als Bild.
- `test/tx_test.py` / `tx_test2.py` prüfen das Sammeln des Seitentexts aus echten Anzeigelisten (`host/pdfhost` erzeugt sie), `test/replay_test.py` das Zeichnen mit entsperrtem Bildspeicher.
- Die Echtfarbenerkennung liest `GEOS.INI`, Abschnitt `[screen 0]`, Zeile
  `device`. Zeilen mit `;` werden übersprungen. Die Statuszeile zeigt
  „Echtfarben“ oder „16 Farben (n)“ mit einem Prüfcode.

## 5. Erkenntnisse über GeoWorks 1 (Kernel 1.28)

Diese Punkte sind durch Tests in GeoWorks belegt. Sie sind nützlich für
weitere Programme.

### Kernel-Funktionen

| Bereich | Ordinalnummern |
|---|---|
| Speicher | MemAlloc 0, MemFree 2, MemLock 5, MemUnlock 6 |
| Dateien | FileOpen 52, FileClose 54, FileRead 58, FileEnum 65 |
| Verzeichnisse | FilePushDir 73, FilePopDir 74, FileSetStandardPath 76 (AX = 0: GEOWORKS-Ordner) |
| Geodes und Objekte | GeodeGetResourceHandle 150, ObjMessage 169 (DI = 0x8000 Aufruf, 0x4000 in die Warteschlange) |
| Zeichnen | GrApplyRotation 308, GrApplyTranslation 310, GrDrawBitmap 326, GrDrawText 330, GrFillRect 336 |
| Farben | GrSetAreaColor 355, GrSetTextColor 359 |
| Fensterbereich | GrGetWinBounds 440 |
| GStrings | GrCreateGString 281, GrEndGString 301, GrDestroyGString 279 |

- **Achtung:** Ordinal 72 ist `FileCreateDir`, nicht „Pfad setzen“.
- **RGB-Farben:** `GrSetAreaColor` und `GrSetTextColor` mit `AH = 80h`,
  `AL` = Rot, `BL` = Grün, `BH` = Blau. Mit einem 64K-Treiber erscheint die
  Farbe exakt, mit 16 Farben wird gerundet.

### Bitmaps
- 1 Bit (Typ 0) und 4 Bit mit Maske (Typ 11h), gepackt mit PackBits pro
  Zeile, funktionieren.
- 4 Bit **ohne** Maske und gepackt (Typ 01h) erscheint mit dem 64K-Treiber
  zerstückelt. Deshalb immer mit Maske packen oder ungepackt zeilenweise
  zeichnen.
- 24-Bit-Bitmaps (Typ 03h, R-G-B, ungepackt) funktionieren mit 64K-Treibern.
- 8-Bit-Bitmaps sind nicht brauchbar.

### Speicher
- Fest gesperrte Blöcke müssen in den DOS-Speicher passen. Zu viele gesperrte
  64-KB-Blöcke führen zum Systemdialog „Speicher voll“ statt zu einem
  Fehlercode.

### Motif-Hints im Menü
- 4047h bzw. 4049h hängen eine Gruppe ins Datei- bzw. Fenster-Menü.
- 4005h/4006h/4007h legen die Menüform fest.
- 4015h/4014h lassen die GenView mit dem Fenster wachsen.

### Drucken (PrintControl der Spool-Bibliothek)
- Attribute 2Ch, damit „Drucken…“ den Dialog öffnet.
- Das Dokumentobjekt muss die Nachricht 259h beantworten: Antwort 6013h mit
  einem Zeiger auf den Dokumentnamen. Sonst kommt nie ein Druckauftrag.
- Die Druckmeldung kommt mit CX:DX = PrintControl und BP = GString.
- Lange Dokumente: Dokumenthöhe = Papierhöhe × Seiten, jede Seite um eine
  Papierhöhe verschoben.
- Nach dem Drucken 25Ah senden.

### Open Watcom ohne C-Laufzeit
- Konstante Tabellen müssen ins eigene Segment `PDFTABS`, sonst liest der
  Code sie über DS aus dem Datensegment des Aufrufers. `tools/checkseg.sh`
  prüft das jetzt automatisch.
- Keine `?:`-Ausdrücke zwischen basierten Zeichenketten.
- 32-Bit-Divisionen sind auf dem 8086 teuer. In Schleifen über Bildpunkte
  besser einfache Zählschleifen verwenden.

### Textobjekt, Zwischenablage, Maus (UI.GEO 1.2)

- Methodentabelle einer Klasse: Kopf mit Oberklasse (far), Anzahl der
  Methoden im Wort an +6, ab +18 die Nachrichtennummern, danach je ein
  Zeiger (Offset, Ressource). Die VisText-Tabelle hat 120 Einträge und
  beginnt mit 0x0102; wer um einen Eintrag verrutscht, ordnet jedem Code die
  Nachbar-Nachricht zu.
- Textobjekt: 0x2200 Text ersetzen (DX:BP Zeichenkette, CX = 0: bis NUL),
  0x2206 Bereich markieren (CX Anfang, DX Ende, wird auf die Länge
  begrenzt), 0x011f Kopieren (0x011e Ausschneiden, 0x0120 Einkleben).
  0x2200 geht auch, bevor der Dialog je geöffnet war (wie im GeoManager).
- Ein GenInteraction-Dialog (Attributbyte 0x62) als Kind eines Menüs wird
  von Motif als Menüpunkt dargestellt, der ihn öffnet. Ein GenTrigger mit
  Attribut 0x80 im Zustandswort (0xc080) und ohne Ausgabe schließt ihn.
- In `object_block` des SDK ist Chunk 0x22 immer ein Datenchunk: Objekte ab
  0x24 anlegen.
- MSG_META_START_SELECT: BP-Bit 0x40 = Doppelklick (so auch der GeoManager).
- Mausrad und Bild auf/ab verschieben das GenView selbst; das Programm sieht
  nur den neuen Ursprung beim nächsten Neuzeichnen.
- **Speicher:** dauernd gesperrte 64-KB-Blöcke lassen anderen Programmen
  (GeoWrite beim Einkleben) keinen Platz. Der Betrachter sperrt Seiten- und
  Bildspeicher nur noch, solange eine seiner Methoden läuft.

## 6. Bauen und Testen

```sh
git clone https://github.com/dnesbitt142/pcgeos-1-sdk-v2
SDK=$PWD/pcgeos-1-sdk-v2 ./build.sh     # -> out/geo/PDFCORE.GEO, PDFVIEW.GEO
```

Test auf dem PC, ohne GeoWorks:

```sh
tools/run.sh testdocs/*.pdf             # Anzeigeliste -> PNG in out/
TC=1 NEXT=1 DLSZ=58912 host/pdfhost datei.pdf out/x 300 0   # Echtfarben, 1 Bildspeicher
python3 tools/dl2png.py out/x-0.dl out/x.png
```

- `tools/compare.py` vergleicht mit pdftoppm.
- `testdocs/` enthält 20 gezielte Testdateien, `testdocs15/` dieselben als
  PDF 1.5 mit Objekt-Streams.

## 7. Versionsgeschichte (kurz)

| Version | Inhalt |
|---|---|
| 0.1–0.3 | Text, Vektorgrafik, Dateiliste, Zoom, Motif-Bildlaufleisten, Symbol |
| 0.4 | PDF 1.5–1.7, Reparatur beschädigter Querverweise |
| 0.5–0.6 | Ordnerwechsel, Start per Doppelklick im GeoManager |
| 0.7 | Bilder (schwarz-weiß), Type3-Schriften |
| 0.8–0.9 | Drucken über den Druckdialog, Datei-/Hilfe-Menü, Vollbild |
| 0.10 | Farbbilder (16 Farben), JPEG, Transparenz; Inflate-Fehler bei großen Abständen behoben |
| 0.11 | Echtfarben mit 64K-Treibern, Erkennung über GEOS.INI |
| 0.12 | Externe Bildspeicher (schärfere Bilder), schnelleres JPEG, faire Platzverteilung |
| 0.12d | Schneller bei Bildseiten, nur ein Bildspeicher („Speicher voll“ behoben) |
| 0.13 | Hilfe im Stil der GeoWorks-Hilfefenster (Titel, Textseite, Bildlaufleiste, Knöpfe) |
| 0.14 | Text kopieren (Bearbeiten / Text der Seite), weniger gesperrter Speicher, Dateiname in der Statuszeile, Doppelklick in der Dateiauswahl, Mausrad |

Die vollständige Liste steht in `dist/LIESMICH.TXT`.

## 8. Grenzen und offene Punkte

- **Bibliotheksgröße:** Der Code der Bibliothek ist fast am 64-KB-Limit
  eines Segments, es sind etwa 2 KB frei. Für weitere Funktionen muss sie in
  zwei Code-Ressourcen aufgeteilt werden.
- **Nicht unterstützt:**
  - progressive JPEG und CMYK-JPEG (erscheinen als grauer Platzhalter)
  - JPX/JBIG2/CCITT
  - Verläufe (Shadings)
  - eingebettete TrueType-/Type1-Schriften (es wird die nächste
    GeoWorks-Schrift genommen)
  - Verschlüsselung
- **Gedrehter Text** in Standardschriften erscheint aufrecht, ließe sich mit
  GrApplyRotation lösen.
- **Drucken** nutzt den Seitenbereich „von–bis“ des Druckdialogs noch nicht,
  gedruckt wird immer in 16 Farben.
- **Echtfarbenbilder** sind bei mehreren großen Fotos pro Seite gröber, weil
  nur ein Bildspeicher von 64 KB zur Verfügung steht.
- **Dateiname in der Titelzeile:** die Nachricht zum Ändern der Beschriftung
  des Hauptfensters ist für GeoWorks 1.2 noch nicht gefunden; der Name steht
  in der Statuszeile.
- **Arbeits- und Querverweisspeicher** (64 + 40 KB) bleiben gesperrt, weil
  die Bibliothek Zeiger hinein speichert.
- **Geschwindigkeit:** Seiten mit vielen Bildern brauchen im Emulator spürbar
  Zeit.

## 9. Herkunft und Rechte

- Alle Quelltexte sind für dieses Projekt neu geschrieben. Sie enthalten
  keinen Code aus GeoWorks- oder GEOS-Dateien. Systemdateien wie UI.GEO,
  MOTIF.GEO, SPOOL.GEO oder DESKTOP.GEO wurden nur angesehen, um Strukturen
  und Nummern zu verstehen.
- Die Breitentabellen der Standardschriften (`core/pdftabs.h`) stammen aus
  den frei verwendbaren Adobe-Core-14-AFM-Metriken.
- Die Testdokumente sind selbst erzeugt (reportlab bzw. von Hand).
- `core/lmath.c` trägt die Lizenz MIT. Eine Lizenz für das ganze Projekt ist
  noch festzulegen.
