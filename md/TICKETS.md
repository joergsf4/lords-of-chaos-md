# Tickets Mega-Drive-Port

Stand: 2026-10-08. Zeiten in V-Int-Frames (1 Frame = 16,7 ms bei NTSC), gemessen im BlastEm mit
`tools/emushot.py` (Szenario 1, Zauberer im Haus). Aufwand: S = Stunden, M = ein Tag, L = mehrere Tage.

## Epic P: Performance

Ziel: Gehen, Scrollen und Zugwechsel fühlen sich flüssig an. Jetzt hat ein Schritt mit Scroll rund
6 bis 9 Frames Pause, ein Schritt ohne Scroll etwa 5. Das reicht für die Schrittwiederholung des
D-Pads (12 Frames), ist aber nicht flüssig. Die Tickets stehen bewusst hinten an.

### Messwerte (Stand nach der Scroll-Optimierung)

| Posten | Kosten pro Schritt mit Scroll |
|---|---|
| `view_update` (Core, 81 Felder zusammensetzen) | 3 Frames |
| `sight_compute` (Core) | 2 Frames |
| `render_fields` (neue Felder zeichnen, ca. 12 Felder) | 5 Frames |
| davon Vergleich aller 81 Felder | ca. 1,5 |
| davon Farbwahl und Zusammensetzen | ca. 2,5 |
| davon VRAM-Upload und Tilemap | ca. 1,5 |
| Panel und Text | 1 bis 2 Frames |
| Animationstick alle 24 Frames (4 Felder) | 3 Frames |
| Start bis zum ersten Bild | ca. 80 Frames |

Bereits erledigt: Hardware-Scrolling mit Torus-Plane, schneller Blockpfad mit 4-bpp-Daten je Palettenzeile,
Cursor als Sprite, Cursor-Ebene im Core abgeschaltet (spart 4 Frames), schrittweise Reste statt
32-Bit-Division, exakter Farbpfad aus (`EXACT_MISS`).

### P-1 Weicher Schiebeübergang beim Scrollen
- **Nutzen:** Das Bild springt beim Scrollen um 24 Pixel. Ein kurzes Gleiten (z. B. 4 Frames à 6 Pixel) wirkt flüssiger, auch wenn die Rechenzeit gleich bleibt.
- **Idee:** Die neuen Felder sind schon gezeichnet, bevor sie sichtbar werden (die Ring-Plane liegt hinter dem Panel und der Statuszeile). Danach `hpx`/`vpx` in Schritten ändern und den Cursor-Sprite mitziehen. Eingaben währenddessen puffern. Option „Schieben aus“ im späteren Optionsmenü.
- **Aufwand:** S bis M. **Abhängigkeit:** keine.

### P-2 `view_update` inkrementell (Core)
- **Nutzen:** spart bis zu 2 von 3 Frames pro Schritt.
- **Idee:** Beim Scrollen nur die neue Zeile oder Spalte neu zusammensetzen und die Felder-Arrays verschieben. Sonst nur Felder neu berechnen, deren Eingaben sich geändert haben (Einheit alt/neu, Sicht, Gegenstände, Dach).
- **Risiko:** Der Core ist Upstream-Code, Parität zu Host und Agon muss durch den Selftest belegt sein. Eher als Option im Core (`VIEW_STATIC_CACHE=2` ist schon so ein Eingriff) mit eigenem Selftest umsetzen und Upstream anbieten.
- **Aufwand:** L.

### P-3 `sight_compute` inkrementell
- **Nutzen:** spart bis zu 2 Frames pro Schritt.
- **Idee:** Nur neu berechnen, wenn sich Position, Türen oder Hindernisse geändert haben, und nur den Bereich um die Einheit. Alternativ die Berechnung auf den nächsten Frame verschieben, wenn das Bild schon steht.
- **Aufwand:** M bis L (Core).

### P-4 Dach- und Sichtregeln pro Feld billiger machen
- **Nutzen:** Im Einzeltest kostete das Dach etwa 2 Frames, die Sichtschicht etwa 1 Frame von `view_update`.
- **Idee:** `viewer_under_roof` einmal pro Frame statt pro Feld, `world_wrap` im Cursor-/Dachpfad vermeiden, `roof_covered` mit Bitkarte statt Nachbarsuche.
- **Aufwand:** S bis M (Core).

### P-5 Farbwahl und Zusammensetzen schneller (Renderer)
- **Nutzen:** ca. 2,5 Frames pro 12 Felder.
- **Idee:** `pick_line` mit Tabelle statt Popcount-Schleifen, Zeilenzusammensetzung in Assembler (`and/or` auf 8 Longs), Ergebnis je Ebenenkombination in einem kleinen Cache merken (gleiche Felder tauchen oft auf).
- **Aufwand:** M.

### P-6 Vergleichsschleife in `render_fields`
- **Nutzen:** ca. 1,5 Frames pro Scroll.
- **Idee:** Nach einem Scroll nur Randzeile/-spalte plus Felder prüfen, deren Eingaben der Core als geändert meldet (Hinweis aus P-2). Struktur `Painted` kleiner machen, Vergleich über 32-Bit-Wörter.
- **Aufwand:** S bis M. **Abhängigkeit:** P-2 für den größten Gewinn.

### P-7 VRAM-Upload per DMA-Queue
- **Nutzen:** ca. 1,5 Frames pro 12 Felder, dazu ruhigere Frames.
- **Idee:** `DMA_queueDma` statt `VDP_loadTileData(CPU)`, dafür Zwischenpuffer, die bis zum V-Blank bestehen bleiben (ca. 3,5 KB RAM). Tilemap-Zeilen ebenfalls einreihen.
- **Aufwand:** M.

### P-8 Animationstick ohne Ruckler
- **Nutzen:** Alle 24 Frames stehen 3 Frames still (4 animierte Felder).
- **Idee:** Animierte Felder auf mehrere Frames verteilen (ein Feld pro Frame), nur Felder im Sichtbereich zeichnen, Taktrate einstellbar.
- **Aufwand:** S.

### P-9 Langsamer Pfad für Flieger, Reiter und Hüpfer
- **Nutzen:** Felder mit Flieger, Reittier, Wasserstand oder Idle-Hüpfen laufen über den Chunky-Pfad (ca. 3- bis 4-mal langsamer).
- **Idee:** Vorverschobene 4-bpp-Varianten (−3 px) für Flieger, oder den Chunky-Pfad auf ganzzahlige Blöcke mit Verschiebung umstellen. Die Idle-Animation notfalls abschaltbar machen.
- **Aufwand:** M.

### P-10 Panel und Text
- **Nutzen:** 1 bis 2 Frames pro Schritt.
- **Idee:** Panelteile nur neu zeichnen, wenn sich ihr Inhalt ändert (Name, Hand, AP-Zahl, Bodenliste); Textzellen schreibt `ui_text` schon nur bei Änderung, aber `sprintf` und Glyph-Suche laufen jedes Mal. Kleine Wertcache pro Zeile.
- **Aufwand:** S.

### P-11 Zugphase der KI
- **Nutzen:** Länge der Wartezeit nach „Zug beenden“ ist nicht gemessen.
- **Idee:** Erst messen (Frames pro `turn_end_phase` bei vielen Einheiten), dann eine Anzeige „KI denkt …“ und gegebenenfalls Aufteilen auf mehrere Frames. Die Eingabe-Warteschlange fängt Tasten schon ab.
- **Aufwand:** S zum Messen, M zum Optimieren.

### P-12 Startzeit
- **Nutzen:** ca. 80 Frames bis zum ersten Bild.
- **Idee:** Anteile messen (Karten laden, `populate_scenario`, Sicht, erster Aufbau). Titelbild als Wartebild nutzen (hängt an E-1).
- **Aufwand:** S.

### P-13 Palettenqualität und Geschwindigkeit
- **Nutzen:** Die Zeilenwahl deckt 75,9 % der Blöcke vollständig ab, der Rest nutzt die nächste Farbe (auf den Testkarten < 150 von 69.000 Pixeln anders). Mit besserem Verfahren könnte auch der schnelle Pfad bessere Bilder liefern.
- **Idee:** Optimierer in `tools/png2md.py` gezielt auf die häufigsten Kombinationen aus echten Karten trainieren statt auf alle Kachelpaare. Option: `EXACT_MISS` für Standbilder (Titel, Screenshots) wieder einschalten.
- **Aufwand:** M.

### P-14 Textvorrat und VRAM-Budget
- **Nutzen:** Der Glyph-Vorrat hat 440 Kacheln. Bei sehr vielen verschiedenen Zeichen/Farben werden alte überschrieben, das ist unschön.
- **Idee:** Verdrängung nach Alter statt Überlauf, oder Farbvielfalt im Panel begrenzen. Ring auf 10×9 Slots verkleinern, falls die Karte weniger Zeilen braucht.
- **Aufwand:** S.

### P-15 PAL-Betrieb
- **Nutzen:** Auf 50-Hz-Geräten ist ein Frame 20 ms, `clock_cs()` rechnet das schon um. Die Messwerte gelten für NTSC.
- **Idee:** Einmal mit BlastEm im PAL-Modus messen, Wiederholraten (`DELAY_CS`, `REPEAT_CS`) prüfen.
- **Aufwand:** S.

### P-18 Ein Spielframe kann mehrere V-Blanks dauern
- **Befund:** Mit der neuen Sicht (R 19, Shadowcasting) braucht ein Frame nach einer Aktion mehrere V-Blanks (Sicht neu berechnen). Der Debug-Block wird erst am Ende des Frames aktualisiert, e2e-Tests müssen darauf warten (`Game.cmd` und `wait_for`).
- **Idee:** Sicht nur bei Bewegung neu rechnen oder inkrementell (siehe P-Tickets zur Sicht).

### P-17 KI-Phase dauert länger (neu seit dem Upstream-Stand 2026-10-09)
- **Befund:** Mit der neuen KI (ADR 0015, Sicht mit Shadowcasting R 19/23) braucht der Gegnerzug im Emulator rund 120–180 Frames statt weniger als 120. Der e2e-Test wartet deshalb auf die Runde statt feste Frames.
- **Idee:** Phasenbildschirm „Zauberer-2 ist am Zug“ (E-8) zeigen, dann fällt die Wartezeit nicht mehr als Hänger auf; Sicht der KI zwischenspeichern.

### P-16 Messwerkzeug im Repo
- **Nutzen:** Die Messläufe liegen bisher nur als Wegwerfskripte. Für Vorher/Nachher-Vergleiche gehört das ins Repo.
- **Idee:** `tools/mdbench.py`: Szenario laden, feste Eingabefolge, Zähler aus dem Debugger lesen (Frames für `view_update`, Sicht, Zeichnen, Panel), Ergebnis als Tabelle. Dazu `LOC_MD_FIXED_SEED` und einen Bench-Schalter in den Build.
- **Aufwand:** S bis M. **Empfehlung:** vor allen anderen P-Tickets.

## Epic E: Spielumfang

Der Port ist ein Skirmish auf Szenario 1. Das fehlt gegenüber dem Agon-Spiel:

- **E-1 Titelbild und Hauptmenü** (erledigt: Titelbild aus `assets/title/title.png`, Menü mit den drei Szenarien und einer Tastenseite). Auch erledigt: Optionen, Spielstand laden, Zauberer gestalten/zurücksetzen, Hilfe, Lexikon, Tutorial; die Endseite zeigt `win.png`/`lose.png` (96×96, `tools/md_assets.py`).
- **E-2 Weitere Karten und Szenarien** (Slayer's Dungeon, Ragaril's Domain, Tutorial, Testland, Zufallsvarianten von Level 1). Karten und Szenarien sind schon in `src/core/gen`.
- **E-3 Zauberer-Designer** und Kampagne (erledigt: Attribute, Zauberbücher, Laden/Speichern der Zauberer, XP und Stufen nach gewonnenem Szenario).
- **E-4 Speichern und Laden** (erledigt): ein Slot im Cartridge-SRAM (32 KB laut ROM-Header). Der Zustand wird ohne Zwischenkopie in den SRAM gestreamt (Welt, Runden, Spiel, Zauberbücher, erforschte Karte, Flächeneffekte, Zauberer, KI-Profile, Prüfsumme; die Kennung wird zuletzt geschrieben). Gespeichert wird am Rundenende und über „Speichern“ im Aktionsmenü, „Spielstand laden“ steht im Hauptmenü. Die Core-Save-Tests (`SaveGame`, `save_serialize`) laufen auf dem MD weiterhin nicht, der MD benutzt sie auch nicht. Offen: mehrere Slots, begrenzte Ladungen wie auf dem Agon (F8).
- **E-5 Sound und Musik** (zum Teil erledigt): die 16 Effektsamples laufen über die PCM-Kanäle des XGM2-Treibers (`tools/md_sound.py`, `md/src/sound_md.c`), reine Tonsignale (Runde, Sieg, Niederlage, Menü, Fehler) auf dem PSG. Die Lieder (Titel, Sieg, Niederlage) spielt ein kleiner Player auf den PSG-Kanälen (drei Töne und ein Rauschkanal, Hüllkurven sind nur angenähert). Im Spiel ist es wie auf dem Agon still. Offen: Ton/Musik an/aus in den Optionen, bessere Klangfarben (YM2612), Effektlautstärken, Prioritäten bei mehreren Effekten gleichzeitig.
- **E-6 Effekte** (Schläge, Treffer, Tod, Zauberanimationen, Einheiten gleiten).
- **E-7 Lexikon, Tutorial, Hilfeseiten, Spielprotokoll, Gesamtkarte** (erledigt; Gesamtkarte über „Karte“ im Aktionsmenü, 4×4 Pixel je Feld).
- **E-8 Phasen-Bildschirm der KI** („Zauberer-2 ist am Zug“), Fußschritte. Offen. Schon da aus dem Upstream-Stand (D69, D73): Warnzeile mit Himmelsrichtung, wenn ein Gegner ins Bild kommt, und für Kampflärm, Magie und Todesschrei in 16 Feldern; die Gesamtkarte markiert die Herkunft mit türkisen 3×3-Blöcken.
- **E-9 Optionen** (Rundenwechsel auto/von Hand ist da, D72): Schiebeübergang an/aus, Tastenbelegung (zweite Variante), Textgeschwindigkeit.

## Epic S: Steuerung

- **S-1 Ebenen-Variante (C halten + D-Pad)** als zuschaltbare Zweitbelegung für Geübte.
- **S-2 Tastenhilfe im Spiel** (eine Seite mit der Belegung, aus START-Menü oder Optionen).
- **S-3 Haltedauer von START einstellbar** (`HOLD_CS`) und Rückmeldung während des Haltens.
- **S-4 Sechs-Tasten-Pad:** X/Y/Z-Belegung prüfen und anzeigen, Mode-Taste.

## Epic T: Technik und Pflege

- **T-1 Upstream-Pflege:** Stand 2026-10-09 eingearbeitet (Fast-Forward auf `upstream/main`, Core-API: keine kritischen Treffer mehr, Zauberhöhe Luft/Boden mit A im Zielmodus (F8), Skelette (D70), AP-Faktor 1,28 (D71), Spielstand-Version 2; Welt 2 und 3 im Menü „(bald)“ (D75), mit `-DLOC_MD_ALL_WORLDS` offen). Tests laufen mit `uv run tools/gen_*.py` neu erzeugtem `src/core/gen`. Änderungen in `src/core/view.c` (`VIEW_STATIC_CACHE=2`) und `tests/selftest.c` (`LOC_SELFTEST_NO_SAVE`) als Pull Request an cadextcp anbieten oder gepflegt halten. `git merge upstream/main` regelmäßig testen.
- **T-2 CI:** (die Tests laufen jetzt lokal mit `md/test/all.sh`, siehe `md/test/README.md`)  Docker-Build und `tools/mdtest.py` (Selftest im BlastEm) in GitHub Actions.
- **T-3 Build-Skript:** `md/build.sh` erkennt Flag-Wechsel nicht (Objekte bleiben stehen), auch gelöschte Dateien in `md/res` hinterlassen veraltete Abhängigkeitsdateien in `out/md` (dann `rm -rf out/md/out out/md/res`). Flags in `out/md/.flags` ablegen wie im Starfox-Projekt und dann sauber bauen.
- **T-4 README und Lizenzhinweise** für den Fork (MIT, Upstream-Urheber nennen).
