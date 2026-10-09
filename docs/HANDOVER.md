# Übergabe: Stand und nächste Schritte

> Stand: 2026-10-07 · **D67 umgesetzt (Regeln und KI wie im Original, ungemergt, gestapelte PRs #153–#162)** · davor 2026-10-06 abends · **M0–M5 vollständig**, Polish-Runde, Playtest-Runden A–C, **Level 1 als Nachtkarte (D54–D58)**, **Playtest 2026-10-06 (D59–D63)** und **Level 1 auf 46×46 (D64)** · bis #148 · CI grün · `loc.bin` ~250 KB, RAM-Reserve 97 KB
> Für die nächste Person bzw. den nächsten Agenten. Zuerst `CLAUDE.md` lesen (Regeln, Befehle), dann dieses Dokument.

---

## 0. Stand 2026-10-08: Tempo und Überblick in Welt 1 (D69–D75)

Playtest des Nutzers: Das Spiel ist zu langsam, man findet den Gegner kaum, Angriffe und Tode gehen unter. Umgesetzt auf `feat/welt1-tempo` (über `feat/fragen-2026-10-08`, PR #163): Geräusche mit Richtung (D69), Skelette (D70), AP-Faktor 1,28 auf Level 1 (D71, Szenario v3), automatischer Rundenwechsel und kürzere Phasenbildschirme (D72), Sichtwarnung (D73), Nachrichten neben der Gesamtkarte (D74), Welt 2/3 ausgegraut (D75). Spielstand v12. **Nur die Anzeige ist im Emulator geprüft** (AP 51, `<man>`, Gesamtkarte, Menü); Warnzeilen, Geräusche und Auto-Rundenwechsel sind ungespielt. Auf der Hardware messen: Dauer eines Rundenwechsels.

## 0. Stand 2026-10-07: Regeln und KI wie im Original (D67)

Nutzerauftrag: „alles wie im Original“, autonom abarbeiten, Fragen notieren statt abbrechen. **Stand 2026-10-08: Die PRs #153–#162 sind am 2026-10-07 gemergt; der Nutzer hat angespielt (technisch ohne Befund, Game-Design-Fehler folgen als Liste), die KI-Laufzeit auf der Hardware ist in Ordnung. Die Fragen F1–F30 sind beantwortet (`docs/FRAGEN.md`); F3, F7, F8 (D68) und F15 liegen auf `feat/fragen-2026-10-08`.** Die PRs sind gestapelt (jeder hat den vorigen als Basis): #153 (0a Mana) → #154 (0b Zustände) → #155 (0c Nahkampf) → #156 (0d Zauber) → #157 (0e Flächen/Fernwaffen) → #158 (0f Beschwören/Designer) → #159 (0g Wertung, Kartenformat v5) → #160 (0h Sicht) → #161 (KI 1 Kreaturen) → #162 (KI 2 Zauberer, Szenario v2, dazu das Duell-Programm `host/duel.c`). **Mergen nur der Reihe nach mit Merge-Commits** (Fallstrick 21), Basis-Branches erst am Ende löschen.

- **Offene Annahmen und Fragen an den Nutzer:** `docs/FRAGEN.md` (F1–F30). Wichtigste: Zähigkeiten der Möbel (F5), Flieger-Nahkampf (F6/F23), Routen auf eigenen Karten neu entworfen (F27), keine schlafenden Wächter gesetzt (F28).
- **Zuerst tun:** (1) Im Emulator und auf der Hardware anspielen: Kampf (Gebunden, Rückschlag 4/4), Zauber mit neuer Reichweite (2L+7), Feuer/Blob/Flut, Sicht (Achteck, Flieger über Dächern), KI-Zauberer (Torquemada), Reiter mit eigenen AP. (2) **KI-Laufzeit auf der Hardware messen** (`loc --bench`): die Entscheidungsschleife baut je Durchgang eine Sichtliste und ruft bei Hindernissen die Breitensuche; der Bench aus Fallstrick-Liste misst es noch nicht. (3) Schwierigkeit: `build/host/duel` (Aufruf im Kopf von `host/duel.c`) zeigt, dass der Gegner mit Originalwerten den Standardzauberer meist schlägt.
- **Neu im Code:** `ai_creature.c`/`ai_items.c`/`ai_nav.c`/`ai_wizard.c`/`ai_priv.h` (K10), `sight_sees`/Achteck/`vis_*` in `sight.c` (K11), `area.c` neu (K5.3), Spielstand **v10**, Kartenformat **v5**, Szenariodatei **v2** (`data/scenarios`: `wizard`, `book … prio`, `route`, `plan`, `trigger`), `data/ai_items.csv`, `data/terrain_effects.csv`, `data/weapons.csv` mit Originalwerten. SD-Karte neu bespielen (Karten, Szenarien).
- **Fallstrick 22 (neu): Der eZ80-Selftest hat nur ~20 KB für Heap und Stack.** Große Strukturen (`SaveGame`, `World`) in Tests nie als lokale Variable; ein Absturz zeigt sich als endloser MOS-Neustart („Agon Console8 MOS Version“ wiederholt) und Timeout. `PYTHONIOENCODING=utf-8` setzen, wenn `test.py -v` bei Timeout druckt.

## 0a. Stand 2026-10-06 abends

### Was heute dazukam

| PR | Inhalt |
|---|---|
| #132–#134 | **Level 1 als Nachtkarte (D54–D56):** Nacht, Fenster, Zaun mit Tor, Brücken, Biome, echte Sichtlinien durch Fenster und Türen. Details in §1a |
| #136, #137 | **D57/D58:** 16 Gelände-Varianten von Level 1, Menüpunkt „Zufaellige Karte“ |
| #138 | **D59:** Wildtiere schlagen im Vorbeigehen nur mit Groll, beim Angriff oder im Revier zu; eigene Figuren fangen Geworfenes (auch Phiolen). RAM-Arena (Speichern ⟷ Hilfe/Zauber/Lexikon) |
| #139 | **D60:** Reiter handeln aus dem Sattel (zaubern, Türen, Truhen, aufheben). Werte und Gepäck des Reiters bleiben erhalten, beim Tod des Reittiers wird er abgeworfen |
| #140 | **D63:** Panel-Labels `AP AU LE KA VE MA` in einer Zeile, darunter die Tasten, die gerade wirken |
| #144 (= #141) | **D61:** Offene Türen schwenken ihr Blatt aufs Nachbarfeld (blockiert), sonst klemmt die Tür; offener Rahmen innen dunkel |
| #142 | **Selftest als eigenes Programm `loctest.bin`** (`tests/`): `loc.bin` 370 → 241 KB, RAM-Reserve 2 → 129 KB. `build.py` bricht unter 16 KB Reserve ab |
| #143 | **D62:** KI-Zauberer defensiv: beschwört bis 5, plündert sein Haus, geht erst bei ≤ 1 Kreatur oder Wutanfall raus; Kreaturen bewachen bzw. marschieren, rüsten sich aus; Wegsuche per Breitensuche |
| #145 | Plan `docs/PLAN-KARTE-46.md` (D64) |
| #147 | **Fix:** Der eZ80-Selftest war seit #142 blind (Emulator endete bei EOF mit 0). Jetzt zählt nur „shutdown triggered by writing 0x0“ |
| #146, #148 | **D64:** Karten bis 46×46; Level 1 neu mit zwei Vier-Raum-Häusern (15×11), 16 Varianten auf 46×46, Truhen/Funde mit der Fläche |
| D65 | **D65:** Das Blatt offener Türen in waagerechten Wänden sitzt im Rahmen am Pfosten (vier Rahmenkacheln), das Blattfeld blockiert nur noch. Senkrechte Wände unverändert. Im Emulator angespielt (Zauberer-Haus), auf der Hardware nicht |
| D66 | **D66:** Abgleich mit dem Spectrum-Original. Kleine Werte angeglichen (AP-Kosten, Gewichte, 5 Kreaturenwerte, Essen, Trankflug), große Unterschiede stehen als Vorschläge R1–R39 in `docs/REGELVERGLEICH-SPECTRUM.md`. **Zuerst klären:** Mana-Tabelle (R1, O4 in WinUAE) und das Kampfmodell (R5, Weg A/B/C) |
| D67 | **D67:** Entscheidung (2026-10-06/07): **alles wie im Original** (Kampfmodell, Mana, Rückschlag, Beschwören, Designer, Zauberer-KI mit Routen). Plan für Regeln (Phase 0, 8 PRs) und schlauere KI (Phasen 1–5): `docs/PLAN-KI.md`. Alle Fragen an den Spectrum-Quellcode beantwortet; zwei eigene Entscheidungen offen (§9 des Plans: MAX_UNITS, Startplätze von Level 1) |

Die Rückmeldungen des Playtests stehen mit Befund und Entscheidung in `docs/PLAYTEST-2026-10-06.md`.

### Wichtig für die Weiterarbeit

- **Selftest:** `tests/selftest.c` (nicht mehr `src/core/`). Auf dem Agon ist es das Programm `loctest` (`build/loctest/bin/loctest.bin`, wird mit auf die SD gelegt); `loc --selftest` gibt es nicht mehr. Der Host-Build braucht `-Isrc/core`.
- **RAM:** `uv run tools/build.py` meldet die Reserve (`RAM reserve (heap + stack)`) und bricht unter 16 KB ab. Stand 97 KB. Größte Posten: statischer Sicht-Cache (~42 KB bei 46×46), drei `World`-Kopien, die Arena in `screens.c`. Merkposten: `gen/maps.c` (alle Karten als C-Arrays) braucht nur der Selftest, landet aber auch in `loc.bin`.
- **Spielstand v8.** Alte Stände werden abgelehnt.
- **Level 1 (46×46):** Zauberer starten bei (6,6) und (40,30), Portal (33,3), Gegner-Truhe (42,34). Selftests hängen an diesen Koordinaten (d62, d46, Pickup, Szenario). Die Karte entstand aus einem Entwurfsskript (hochskaliertes Gelände + Häuser nach Skizze); seitdem ist `data/maps/many_coloured_land.txt` die Quelle.
- **Varianten:** `tools/gen_variants.py` hat feste Kästen für Häuser, Garten, Portal, Türstummel (`HOUSE1/2`, `CLEAR_BOXES`, `STUBS`, …). Wer die Häuser verschiebt, muss sie dort nachziehen.
- **Türen (D61, D65):** Das Türblatt (freies Nachbarfeld, nur Zeichenmarke) **blockiert nicht mehr** (D76), die Tür klemmt nie. Check `d61` prüft weiter, dass jede Tür ein Blattfeld hat. Auch die senkrechten Blätter werden seit D84 in der Türkachel gezeichnet (`door_v_open_*`). Gezeichnet wird das Blatt seit D65 bei waagerechten Wänden im Rahmen (`door_h_open_tile`), bei senkrechten auf dem Blattfeld. Möbel nicht diagonal neben Türen stellen.

### Nicht im Spiel geprüft

D59–D64 sind durch Selftests (Host und eZ80) und Vorschaubilder (`tools/art/map_preview.py`, `docs/design/mockups/level1-46*.png`, `door-leaf-d61.png`) belegt, aber **nicht angespielt**. Als Nächstes im Emulator und auf der Hardware anspielen: Reiten mit Zauber, Türblätter, KI-Verhalten über mehrere Runden, Panel-Tastenzeile, große Karte (Gesamtkarte `m`).

### Offen

- **Bench 46×46 (erledigt):** Auf der Hardware am 2026-10-06 gemessen, Werte und Vergleich in `docs/AGON-QUIRKS.md` (Hardware-Abschnitt). Fenster-Kosten unverändert, KI-Phase 20 → 80 ms. Offen bleibt ein Worst Case (KI-Zauberer mit 5 Beschwörungen über mehrere Runden). Die Werte schreibt das Spiel erst nach Esc in `loc.log`; `send_keys.py` wählt das Fenster nur über den Titel (siehe Fallstrick 19).
- **D33 Schriftrollen lehren Zauber:** Schriftrollen geben bisher nur einen Hinweistext. Die KI sammelt sie (D62), liest sie aber erst, wenn D33 steht. Nutzer gefragt, noch keine Antwort.
- In engen Häusern können sich Nachbartüren einen Blattplatz teilen; in Level 1 ist das durch die neuen Häuser behoben, in anderen Karten möglich (die zweite Tür klemmt dann, bis die erste zu ist).
- Aus der Milestone-Liste unverändert: Original-Zauberpreise, Spinne/Vampir-Rebalance, VP-Vorschläge.

### Stand 2026-10-05 (davor)

Playtest-Runden A–C (`docs/PLAYTEST-2026-10-05.md`, D40–D50) sind erledigt: Türen schließen/abschließen, Truhen, Ertrinken, Waten, eigene Figuren überlagern sich, Dach-Anzeige, Rundenwechsel-Redraw. Einzelheiten im CHANGELOG.

---

## 1. Kurzstatus

| Milestone | Status |
|---|---|
| **M0 Fundament** | ✅ Toolchain, Tests, CI, Docs |
| **M1 Grafik und Eingabe** | ✅ Software-seitig fertig (#1, #4, #5, #6). Offen: #2 (optional), #3 und #7 (brauchen echte Hardware) |
| **M2 Core-Skelett** | ✅ #13–#18 |
| **M3 Classic spielbar** | ✅ #27–#33, #41 |
| **M4 Classic komplett (v1.0)** | ✅ 10 von 10 Teilen (#43–#52) |
| **M5 Präsentationsrunde** | ✅ #88–#92: Endbildschirm mit Menü-Rücksprung/Kampagne, Hilfeseiten (SD), geführtes Tutorial, Lexikon (persistent), Ereignis-Ring mit Kampf-/Todesanimation, 16 Sound-Effekte mit Wellenformen/ADSR, KI sichtbar, Titelbild (Streaming) + Titelmusik (3 Kanäle) |
| **Polish-Runde** | ✅ #113 Bildschirmreste/Titelbild-Loader · #114 VDP-Spike (ADR 0012) · #115 Audio (Samples, Musik, Jingles) · #116 Titelbild, Zierschrift, Menü-/Endbilder, Tränke · #117 Sprite-Effekte · #118 Beschwörungsstufen (D34) · #119 Rundenende, Zufallswelt, Wildtiere, Phasenbildschirm, Aufheben-Auswahl (D35–D37). Plan: `C:\Users\cadex\.claude\plans\schau-mal-das-spiel-soft-dongarra.md` (Nutzer-Entscheide dort) |
| M6+ Chaos | geplant, siehe `docs/ROADMAP.md` |

**Was heute läuft (Emulator, Stand Polish-Runde):**
- **Start:** Titelbild (Schlachtgetümmel) + Titelmusik (4 Stimmen, Samples), die Musik läuft im Menü weiter; Menü mit Titelbild-Hintergrund; Überschriften in eigener 8×16-Zierschrift.
- **Klang:** 16 eigene Samples (`tools/gen_sfx.py`, `/loc/sfx.bin`), Effekt-Sequenzer auf Kanal 0/4, Musik auf 1–3/5–9 (je zwei Kanäle pro Stimme), Jingles am Spielende; Setup schaltet Musik (M), Effekte (T), Gleiten (G). Vorhören: `uv run tools/audio_preview.py`.
- **Effekte:** Projektile/Zauber/Schadenszahlen als VDP-Sprites, gleitende Schritte.
- **Werkzeug:** `uv run tools/run.py --vdptest [n]` startet das separate Messprogramm `vdptest` (ADR 0012).
- **Spielende:** Endbildschirm (Sieg/Niederlage) mit Runden/Kills/Beute/VP, Kampagne verbucht XP/Level; Enter zurück ins Menü, Esc beendet.
- **Tutorial:** kleine Karte, 7 Schritte (Bewegen → Wechseln → Schlüssel → Truhe → Kampf → Zauber → Portal), Hinweiszeile unten; Runde-1-Sperre aufgehoben.
- **Lexikon (Taste `k`, vorher `i`; `i` ist das Inventar):** entdeckte Kreaturen/Objekte, Detailseite mit Porträt und Text; persistent in `/loc/lexicon.dat`.
- **Kampf sichtbar und hörbar:** Ereignis-Ring im Core (Schwung/Treffer/Wunde/Verfehlt/Tod/Zauber/Zerschmettern), Frontend spielt Overlay-Animationen und 16 Sounds (Wellenform + ADSR, Kanal 0); KI-Phasen werden animiert (`Turns.on_ai`).
- (M4-Bestand unverändert: Runden/AP, Kampf D16/D21/D26, Magie inkl. Tränke/Brauen, Objekte, KI, Hidden Map, Speichern, Kontextmenü/Big Map/Log.)

**Was heute läuft (Emulator) — M4-Bestand:**
- **Spielstart:** `loc` startet Szenario 1 „The Many Coloured Land“ (36×36, Wrap-around, Kartenformat v3 mit Portal). Die Zauberbücher kommen aus der Szenario-Datei (`data/scenarios/`). Eine ganze Partie gegen einen KI-Zauberer ist durchspielbar.
- **Rundenablauf:**
  - `Tab`/`Shift+Tab` wählt eine Einheit, `Leertaste` beendet sie, `Shift+E` beendet den Zug sofort; sind alle Einheiten fertig, auch `Leertaste`.
  - Runde 1 erlaubt nur Zaubern `[PM 7]`.
  - Hat der Mensch keine Einheiten mehr, spielt die KI bis zu 40 Runden zu Ende; danach folgt die Abrechnung.
- **Bewegung:**
  - Pfeile, Akkorde und Tastenwiederholung.
  - Fliegen mit `<` und `>`.
  - Bump öffnet Türen und Truhen, greift Gegner oder Terrain an; `x` startet den Look-Modus.
- **Kampf (D16, D21):**
  - Eine Trefferformel für alle Angriffe: 10–90 %, Verteidigung inklusive eines Schildes.
  - Rückschlag auch nach einem Fehlschlag; tödliche Wunden; Gebundenheit neben Gegnern.
  - Untote nehmen nur Schaden von Untoten, magischen Waffen und Zaubern; unter 50 % Constitution gibt es einen Malus.
  - Wer stirbt, lässt alles fallen; Kill-VP richten sich nach dem Opfer.
- **Magie:**
  - Beschwörungen und Bolt/Lightning.
  - Die 7 sonstigen Zauber: Shield, Eye, Teleport, Curse, Subversion, Magic Attack, Enchant.
  - Zeitlich begrenzte Wirkungen mit Panel-Icons.
  - Tränke: brauen im Kessel, `q` trinken, `v` abfüllen, die Bombenphiole werfen; Drachen nur mit Drachenkraut im Kessel.
- **Objekte:**
  - `g` aufheben, `d` fallen lassen, `w` wechseln, `t` werfen, `f` Bogen.
  - `e` essen, `r` Schriftrolle lesen.
  - Schlüssel und Truhen; Schätze bringen am Portal VP.
- **KI:** Unabhängige jagen sichtbare Ziele, unsichtbare Einheiten sieht sie nicht. Der KI-Zauberer lässt seine Kreaturen jagen, kämpft, beschwört und flieht durchs Portal.
- **Darstellung:** Hidden Map (unerforscht schwarz, erinnert abgedunkelt), animierte Kerzen, Wasser und Portal, Cursor als VDP-Sprite.

---

## 1a. Level 1: Nachtkarte (D54–D56), seit D64 auf 46×46

Auftrag des Nutzers (2026-10-06): Level 1 soll wie die Amiga-Karte glaubwürdig wirken — Nacht, verschlungene Wege, Haus mit mehreren Zimmern, Fenster mit Sichtlinie, Biome mit natürlichen Grenzen, Brücken, Blumengarten mit Zaun und Tor. Die Größe war zuerst 36×36; seit D64 (Selftest ausgelagert, RAM frei) ist sie **46×46**. Alles Weitere steht im GDD (D54–D58, D64) und im CHANGELOG.

- **Nacht (D54):** `tools/art/night.py` färbt Außenkacheln beim Bauen um (`build_tiles.collect`): schwarzer Grund, gedämpfte Grastupfen, graue Wege, dunkler Sumpf. **Die PNGs in `assets/tiles` bleiben die Tag-Quelle**, Lichtquellen und Innenräume bleiben unberührt. Regeln sind pro Namenspräfix in `RULES`.
- **Neue Elemente:** Fenster (`FE_WINDOW`, `W`), Zaun (`FE_FENCE`, `F`, 16er-Auto-Tile) mit **Tor** (Tür zwischen Zaunpfosten, `world_is_gate`; klappt flach, kein Türblatt), Brücke (`FL_BRIDGE`, `b`), Blumen (`DE_FLOWERS`, `f`), Glühpilze (`DE_MUSHROOMS`, `o`), Sumpfblasen (automatisch). Kacheln aus `tools/art/make_night_set.py` (`--only NAME`; die PNGs sind danach Quelle), Türblätter aus `tools/art/make_door_leaf.py`.
- **Wälder (D55):** Totenwald (`FL_SHADOW_WOOD`) und Zauberwald (`FL_MAGIC_WOOD`), je 3 Varianten per Positions-Hash.
- **Wildtiere und Funde nach Biom (D55):** `data/habitats.csv` → `HABITAT[][]`/`HABITAT_SHORE[]`. Das Biom bestimmt nur **welches** Tier, nicht wie viele (so gewollt). Brücken sind keine Startfelder (#134). Truhen, Schlüssel und Funde wachsen seit D64 mit der Fläche (`by_area`).
- **Sicht (D56, ersetzt D44):** Dächer sind nur Anzeige. Das Dach öffnet sich auf den Feldern, die die aktive Figur sieht (`sight_look`). Fenster sind vom Dach ausgenommen; Fenster und offene Türen geben echte Sichtkeile, auch für KI, Zauber und Fernwaffen.
- **Vorschau:** `uv run tools/art/map_preview.py 0 [x0 y0 w h] [--viewer X Y]` bzw. `map_preview.py build/maps/mcl_v00.map` (Host-Build nötig; Karte 0 ist einkompiliert, also nach Kartenänderungen den Host neu bauen).
- **Offen (Optik):** offene Tor-Kachel noch grob; Dach nachts evtl. dunkler; VDP-RAM mit allen Kacheln auf Hardware prüfen (QUIRK S2).

---

## 2. Zusammenarbeit mit dem Nutzer (wichtig)

- **Sprache:** Deutsch im Chat und in den Docs, Code und Kommentare auf Englisch.
- **Design vor Code:** Neue Phasen bzw. Features erst im GDD (`docs/design/GDD.md`, Entscheidungen in §14) vorschlagen und abstimmen, dann bauen. Bei echten Wahlmöglichkeiten kurz fragen (2–4 Optionen, mit Empfehlung).
- **Regelantworten als Recherche:** Der Nutzer beantwortet Designfragen oft mit Auszügen aus seinen Quellen (mit Fußnoten). Originalwerte daraus sind nur Anker (D7). Unser eigener Wert bleibt, z. B. Schild +4 statt +13.
- **Keine exakte Kopie des Originals (D7):** Werte und Formeln sind eigenes Design. Ausnahme: die Kreaturtabelle `[PM 34]` als Startwerte (D12).
- **Optik:** eigene Pixelart, 24×24, 3/4-Frontansicht wie auf dem Amiga (D9–D11). Möbel, Teppiche, Türen sollen sichtbar sein, nicht abstrakt.
- **Steuerung:** Tastatur im Stil von Caves of Qud (D5), Zieltastatur **Cherry G84-4100, deutsch, ohne Ziffernblock** (D6).
- **Fokus Einzelspieler** mit Kampagne gegen KI-Zauberer (D4). Hotseat und Maus kommen nach v1.0.
- **Review vor dem Merge:** Zweimal hat erst das Review echte Fehler gefunden, obwohl CI grün war: ein Hänger, falsche VP, kaputte Panel-Icons, eine wirkungslose Bombe. Größere PRs deshalb vor dem Merge gegen das GDD prüfen und im Emulator anspielen.
- **Mergen:**
  - Der Nutzer gibt das Mergen frei. Hat er es ausdrücklich verlangt, mit `gh pr merge <n> --merge` mergen bzw. mit `--auto` nach grünem CI.
  - **Gestapelte PRs immer mit Merge-Commits**, nicht Rebase oder Squash, sonst tauchen die Commits doppelt auf.
  - Das Auto-Merge-Werkzeug der App wurde vom Freigabesystem abgelehnt.
- **Belege zeigen:** Nach sichtbaren Änderungen einen Emulator-Screenshot machen (`tools/run.py ... --screenshot`) und ansehen. Der Nutzer reagiert auf Bilder.
- **Mega Drive:** Die Mega-Drive-Fassung entsteht im eigenen Repo `cadextcp/lords-of-chaos-md` (Fork dieses Repos, Core-Spike fertig). Der Nutzer stellt **zuerst das Agon-Spiel fertig**; die Agon-Steuerung bleibt, wie sie ist. Dieses Repo ist das Original für den gemeinsamen Core (`src/core`, `data/`, `assets/`): Das MD-Repo zieht von hier nach, Core-Verbesserungen von dort kommen als eigener PR zurück (zuerst 2026-10-05: `world_wrap`, `roof_refresh`, `VIEW_STATIC_CACHE`). Core-Code deshalb weiter plattformfrei halten. Der alte Branch `docs/mega-drive` (PR #55) ist im MD-Repo aufgegangen.
- **Amiga-Referenz:** WinUAE ist installiert (`C:\Program Files\WinUAE\winuae64.exe`), Kickstart und ADF liegen in `Desktop\amiga\`. Beobachtungen kommen nach `docs/design/amiga-observations.md`.

---

## 3. Umgebung

- **Windows 11** mit **WSL Ubuntu** (agondev und gcc laufen darin), dazu **uv** und **gh** (eingeloggt als `cadextcp`).
- Repo: `C:\Users\cadex\projekte\lords-of-chaos-agon` → GitHub `cadextcp/lords-of-chaos-agon` (public).
- **Nur lokal (gitignored):**
  - `reference/`: Handbuch-PDFs, Screenshots, Spectrum-Kartenbogen; urheberrechtlich geschützt, **nie committen**
  - `emulator/`, `toolchain/`, `sdcard/`, `.cache/`, `build/`, `bin/`, `obj/`, `src/core/gen/`
- Frischer Clone: `uv run tools/setup.py` (lädt Emulator 1.2.5 und agondev v0.22, SHA-geprüft).
- **macOS:** `setup.py` unterstützt nur Windows und Linux. Der **Host-Build** läuft mit dem System-`cc`; damit gehen Host-Selftest, `map_preview.py` und die Python-Werkzeuge. eZ80-Selftest und Emulator prüft die CI.
  ```bash
  for s in build_tiles gen_data gen_maps gen_variants gen_scenarios; do uv run tools/$s.py; done
  cc -std=c99 -O1 -Isrc/core -o build/host/loc_host $(find src/core -name '*.c') tests/selftest.c host/main.c
  build/host/loc_host --selftest
  ```
- Alte Projekte, nur als Archiv: `C:\Users\cadex\projekte\LordsOfChaos` (gescheiterter BASIC-Versuch), `AgonBasics`, `AgonPipeline`.

---

## 4. Befehle

```bash
uv run tools/test.py                       # Host + eZ80-Selftest (vor jedem Commit)
uv run tools/run.py                        # Spiel im GUI-Emulator (Szenario 1)
uv run tools/run.py --dump --time 35 --list --keys "shift+e" --screenshot   # Rauchtest
uv run tools/run.py --bench --time 20      # Redraw-Messung -> loc.log
uv run tools/run.py --keytest              # Tastatur-Events anzeigen
uv run tools/mockup.py --sheet             # Mockup und Kachelübersicht
uv run tools/art/creature_sheet.py         # Kreaturen-Übersicht
```

- **Spiel-Optionen:**
  - `loc` startet Szenario 1, `loc --testland` die Entwicklungskarte, `loc --house` das Zauberer-Haus.
  - `--dump` schreibt `loc.log` mit ASCII-Karte und AP pro Frame.
  - Außerdem: `--bench`, `--keytest`, `--free-round1` (keine Bewegungssperre in Runde 1) und `--fly` (eigene Flieger starten in der Luft). Der Selftest ist das eigene Programm `loctest`.
- **`send_keys`-Syntax:**
  - Benannte Tasten nur mit `--list`. Ohne `--list` wird jedes Zeichen einzeln gesendet; aus `shift+e` würden dann s, h, i, f, t …
  - `up+right` ist ein Akkord, `hold=right=800` hält die Taste 800 ms.
- **Spieltasten:**
  - Zauber `c`, aufheben `g`, fallen lassen `d`, wechseln `w`, werfen `t`, Bogen `f`.
  - Essen `e`, lesen `r`, trinken `q`, Phiole füllen `v`.
  - Look `x`, fliegen `<` `>`, Einheit `Tab`, fertig `Leertaste`, Zugende `Shift+E`.

---

## 5. Architektur in Kürze

```
src/core/  plattformfrei (Host + eZ80):
  world.[ch]    Karte, Einheiten (stabile id), Objekte, Bewegung, AP/Stamina,
                Kill-Protokoll (world_kill_unit), Kessel-Datensätze, Laden (.map v3)
  turn.[ch]     Rundenablauf, aktive Einheit per id, Runden-Hook, KI-Callback
  combat.[ch]   Nahkampf, combat_damage (alle Schadensquellen), Terrain-Angriff
  items.[ch]    Inventar, Waffen-/Schild-Werte, Werfen, Bogen, Essen, Lesen, Truhen
  spells.[ch]   Mana, Zauberbücher (aus .scn), Beschwörung, Bolt/Lightning, 7 sonstige Zauber
  effect.[ch]   Wirkungen mit Laufzeit pro Einheit (Tick am Rundenende)
  brew.[ch]     Kessel, Zutaten, Phiolen, Bombe, Drachenkraut
  game.[ch]     Portal, VP, Kill-Gutschrift, Spielende (outcome), Magic Eye
  events.[ch]   Darstellungs-Ereignis-Ring (Beobachtung ohne Nebenwirkung, M5c)
  tutorial.[ch] Schritt-Engine des geführten Tutorials (M5b)
  lexicon.[ch]  entdeckte Kreaturen/Objekte als Bitmasken (M5b)
  ai.[ch]       Jäger und Zauberer-KI
  sight.[ch]    Sichtlinie (Bresenham) und Hidden Map
  view.[ch]     9x9-Fenster: Ebenen pro Feld, Auto-Tiling, Dirty-Felder, Cache, Animation
  chord.[ch]    Pfeil-Akkorde und Tastenwiederholung
  names.[ch]    alle Anzeigetexte (deutsch, ohne Umlaute)
  populate.[ch] Wildtiere nach Biom, Truhen, Funde (mit der Fläche), Herden (D35, D55, D64)
  ride.[ch]     Reiten; ride_actor_kind = wer handelt (D60)
  gen/          GENERIERT: tiles.h, data.[ch], creatures.h, maps.[ch], scenarios.[ch]
src/agon/  main.c (Hauptschleife, settle()), render.c (VDP, Panel, Titel-Streaming),
           screens.c (Endbildschirm, Hilfe-Viewer, Lexikon, Titel, M5),
           fx.c (Ereignis-Animation), music.c (Titelmusik-Sequencer),
           sound.c (16 Effekte, Wellenform+ADSR), input.c, umfont.c,
           mapfile.c (.map, .scn, wizards/lexicon/save von SD), keytest.c, log.c, emu.asm
host/      PC-Frontend (--selftest, --dump, --layers, --map-file)
tests/     selftest.[ch] (Host UND eZ80), loctest_main.c + loctest.mk -> build/loctest/bin/loctest.bin
```

**Pipelines (laufen automatisch in `build.py` und `test.py`):**

| Quelle | Werkzeug | Ergebnis |
|---|---|---|
| `assets/tiles/*.png`, `assets/icons/*.png` | `build_tiles.py` | `build/tiles.bin` (291 Kacheln, 162 KB), `gen/tiles.h` |
| `data/*.csv` | `gen_data.py` | `gen/data.[ch]`, `gen/creatures.h` |
| `data/maps/*.txt` | `gen_maps.py` | `build/maps/*.map` (Format v4) und `gen/maps.[ch]` |
| `data/scenarios/*.txt` | `gen_scenarios.py` | `build/scenarios/*.scn` (Zauberbücher, „LOCS“ v1) und `gen/scenarios.[ch]` |
| `data/help/*.txt` | `gen_help.py` | `build/help/*.hlp` (Seiten, Umlaut-Codes; Lexikon-Seitenzahl = Kreaturen+Objekte) |
| `data/music/*.txt` | `gen_music.py` | `build/music/*.bin` (Noten, „LOCM“) |
| `assets/title/title.png` | `build_title.py` (Quelle: `tools/art/make_title.py`) | `build/title.bin` (320×240 RGBA2222, „LOCB“) |

- Reihenfolge: Kacheln → Daten → Karten → Szenarien → Hilfe → Musik → Titel. `gen_maps` liest Enum-Werte direkt aus den C-Headern.
- **View-Hash `HOUSE_VIEW_HASH` (tests/selftest.c):** Er ändert sich mit Kacheln, Karte oder Kompositionsregeln. Host und eZ80 müssen denselben Wert ausgeben; den Wert bewusst übernehmen.
- **Budget:** `loc.bin` ~250 KB (ohne Selftest); Texte/Titel/Musik kommen von der SD (ADR 0011). Maßgeblich ist die RAM-Reserve, die `build.py` prüft.

---

## 6. Fallstricke

Die vollständige Liste steht in `docs/AGON-QUIRKS.md`. Die wichtigsten:

1. **Das agondev-Makefile kennt keine Header-Abhängigkeiten.** `build.py` baut deshalb immer clean.
2. **`int` ist auf dem eZ80 24 Bit.** Im Core nur `stdint`-Typen. `1u << i` ist ab `i = 24` undefiniert; dann `(uint32_t)1 << i` schreiben. **Divisionen sind langsam**; Hot-Paths ohne Division schreiben.
3. **ez80-clang-Bug:** `x == A || x == B || …` über Enums kann abstürzen. Lookup-Tabellen verwenden (T7).
4. **`/*` in C-Kommentaren**, z. B. „data/*.csv“, ergibt den Fehler „/* within comment“.
5. **Tastatur:** Bewegung nur per VKey. Die Hauptschleife muss die **Event-Queue vollständig leeren**, bevor sie `chord_poll` aufruft (K1–K5).
6. **Der CLI-Emulator führt `autoexec.txt` aus.** `test.py` und `run.py` schreiben es jeweils neu.
7. **Bash-Tool unter Windows:** Heredocs mit Sonderzeichen brechen; Skripte lieber per Datei schreiben (`.cache/*.py`). Python unter Windows schreibt CRLF; Dateien deshalb mit `write_bytes` schreiben. Für `wsl.exe` `MSYS_NO_PATHCONV=1` setzen.
8. **Der Agon-Systemfont hat keine Umlaute.** UI-Texte ohne ä/ö/ü („Tuer“).
9. **Tile-IDs sind 16 Bit** (291 Kacheln, Icons über 255). Tile-Tabellen nie als `uint8_t` anlegen; die Panel-Icons sind daran schon einmal gescheitert.
10. **Kachel-PNGs sind Quelle.** `tools/art/make_tiles.py` überschreibt sie, also nur mit `--only NAME` neu erzeugen.
11. **Unit-Indizes sind instabil.**
    - `world_remove_unit` tauscht mit der letzten Einheit. Einheiten über Aktionen hinweg per `Unit.id` und `world_find_unit` halten.
    - Tode immer über `world_kill_unit` bzw. `combat_damage`, damit die VP stimmen.
    - Wer in einer Schleife töten kann, liest Killer-Art und -Besitzer vorher aus.
    - Im Frontend nach jeder Aktion `settle()` aufrufen.
12. **Zielzauber brauchen Reichweite 6 und Sichtlinie (D17).** Tests, die durch die Haustür im Testland zielen, öffnen sie vorher (`feature[5][8] = FE_DOOR_OPEN`).
13. **Der Kessel-Zustand folgt dem Kessel-Objekt** (`brew_cauldron_at`). Volle Kessel lassen sich nicht tragen; Phiolen wirken vorerst mit Stufe 2.
14. **Generierte Dateien nach Kachel- oder Datenänderungen neu erzeugen** (`build_tiles`, `gen_data`, `gen_maps`), bevor man den Host-Build startet. Veraltete `gen/maps.c` oder `gen/tiles.h` ergaben schon scheinbar sinnlose Selftest-Fehler.
15. **Tag-PNG und Nacht:** `assets/tiles/*.png` sind die Tagfassung; was nachts anders aussieht, steht in `tools/art/night.py`. Neue Außenkacheln brauchen dort eine Regel.
16. **Dächer blockieren keine Sicht (D56).** Wer ein Dach „öffnen“ will, tut das in `view.c` über `sight_look`.
17. **Code zählt zum RAM.** Programm und Daten teilen sich `0x40000`–`0xB0000`; Heap und Stack bekommen den Rest. Zu wenig Reserve zeigt sich nur als seltsames Verhalten (z. B. „water animates“ im eZ80-Selftest). `build.py` prüft die Reserve; große Puffer, die nie gleichzeitig gebraucht werden, in die Arena (`screens_borrow_save`).
18. **Der eZ80-Selftest gilt nur mit der Emulatorzeile „shutdown triggered by writing 0x0“.** Ohne offene Eingabe beendet sich der CLI-Emulator bei EOF selbst mit 0 (#147). Wer `test.py` ändert: diese Bedingung nicht aufweichen.
19. **Fremde Emulatorfenster:** `send_keys.py` und `screenshot.py` suchen das Fenster nur über den Titel „Fab Agon Emulator“. Läuft ein zweiter Emulator (der Nutzer hat ein ZX-Agon-Projekt), landen Tasten dort. Vorher `Get-Process fab-agon-emulator` prüfen; im Zweifel nur Host-Werkzeuge nutzen.
20. **Bash-Heredocs verwandeln `\n` in C-Strings in echte Zeilenumbrüche.** C-Patches mit `printf("...\n")` per Write-Werkzeug in eine `.py`-Datei schreiben, nicht inline.
21. **Gestapelte PRs mergen:** GitHub hängt den nächsten PR erst nach dem Löschen des Basis-Branches um; wird zu schnell gelöscht, schließt es den PR (#141). Nacheinander mergen und Branches erst am Ende löschen. Nie `git branch | xargs git branch -D` – das löscht auch die Branches des Nutzers.

---

## 7. Nächste Schritte

**Polish-Runde (Stand 2026-10-04, siehe §0 für 2026-10-05):** #113–#119 sind alle gemergt, `main` ist grün (Host + eZ80). Als Nächstes auf Hardware prüfen (SD-Paket `bin/loc-sd.zip` neu): `vdptest` (Log mit dem Emulator vergleichen, ADR 0012), Klang/Musik, Schrift, Sprites, Ladezeit (`sfx.bin` 110 KB zusätzlich), VDP-RAM. Offen aus dem Plan: Copper/Doppelpuffer verworfen (ADR 0012); KI-Bewegungen gleiten noch nicht (nur eigene Schritte). Grafik-Restposten: Lexikon-Vorschaubilder, Zauberer-Porträt, Gelände auf der Großkarte; optional Partikel und Status-Symbole für neue Effekte.

**Fallstricke aus der Polish-Runde:** Der eZ80-RAM ist knapp (QUIRK S6) – große Puffer nur streamen, Werkzeuge als eigene Programme (`spikes/`). Audio: VDP queued nicht (A1), Kanal 3+ erst freischalten (A8), stimmbar = Flag 16 (A9) – sonst landen Befehlsbytes als Text auf dem Schirm. Python-Patches unter Windows immer mit `encoding="utf-8"` lesen.


**M5 Präsentationsrunde ist fertig** (alle vier PRs gemergt, CI grün): #88 Endbildschirm + Menü-Rücksprung + Kampagnenergebnis, #90 Hilfeseiten/Tutorial/Lexikon, #91 Ereignis-Ring/Kampf-/Todesanimation/Sound/KI-sichtbar, #92 Titelbild/Titelmusik. Plan war `docs/PLAN-M5.md` (4 PRs nach Nutzerentscheid).

**Als Nächstes: Hardware-Abnahme von M5 durch den Nutzer** (der CLI-Emulator hat weder VDP-Bild fein noch Audio — QUIRKS E1/A4):
1. **SD-Paket `bin/loc-sd.zip`** (liegt bereit; entpacken nach `/loc` auf der Karte): `loc.bin`, `tiles.bin`, `title.bin`, `maps/`, `scenarios/`, `help/`, `music/`.
2. Prüfen: Titelbild + Menü, Titelmusik (Klang!), Effekt-Klang (Kampf), Animations-Timing, `loctest`, `loc --bench`, Ladezeit (tiles 162 KB + title 75 KB).
3. **VDP-RAM messen** (QUIRKS S2): Kacheln + Titel-Bitmap zusammen — im Emulator ok, Hardware offen.
4. **Titel-Motiv abstimmen:** Das Bild ist ein eigener Vorschlag (Platzhalter). Quelle: `assets/title/title.png`, Generator `tools/art/make_title.py`. Änderungswünsche gerne — anderes Motiv, anderer Schriftzug-Stil.
5. **Spielstand-Format v3** (M5c): alte v2-Spielstände werden abgelehnt („Kein Spielstand“) — einmal löschen.

Danach: M6/Chaos laut `docs/ROADMAP.md` (GDD §12), oder Politur aus §8.

**Workflow:** pro Issue ein Branch, Selftest-Checks, Emulator-Screenshot ansehen, CHANGELOG, PR. Vor dem Merge ein Review (siehe §2).

---

## 8. Offene Punkte

- **Hardware-Test des Nutzers (#3, #7):** Er wird mit jedem Teil wichtiger; KI-Runden und Sicht kosten auf dem Emulator schon spürbar Zeit.
  - Auf die SD-Karte nach `/loc`: Inhalt von `bin/loc-sd.zip` (siehe §7).
  - Dann `SET KEYBOARD 2`, `cd /loc`, `loc --keytest`, `loc --bench`, `loc`.
  - **Runde 1 ohne Bewegung:** `loc` startet mit der Original-Regel `[PM 7]` — in Runde 1 ist nur Zaubern moeglich. `Shift+E` beendet den Zug; `loc --free-round1` hebt die Sperre auf. Im Tutorial ist sie ohnehin aufgehoben.
  - **Stand 2026-10-03 (Hardware, Stand `037521f`):** Upload nach `/loc` per USB, `loc --selftest` PASS und `loc --bench` sind gelaufen (Werte in `docs/AGON-QUIRKS.md`, Ablauf in `docs/TESTING.md`); lange Dateinamen auf FAT sind geklärt. Das Spiel selbst, `--keytest` und die Eingabe am Gerät stehen noch aus. Der Lumagon-Autostart ist auf der Karte abgeschaltet (Sicherung `/autoexec.lum`).
  - Zu klären: Akkorde ohne Ghosting? Codes für `<`/`>`? MOS-/VDP-Version? Ladezeiten (tiles.bin 162 KB, title.bin 75 KB)? Dauer einer KI-Runde? VDP-RAM mit Titel (S2)? Klang?
- **Titel-Motiv:** Platzhalter-Vorschlag, wartet auf Abstimmung (siehe §7).
- **Tags:** `v0.2.0` bis `v0.4.0` sind noch nicht gesetzt; vorgesehen nach dem Hardware-Test. `v1.0.0` (M4) und ein M5-Tag sind des Nutzers Sache.
- **#2 Buffered Commands:** nur nötig, falls die Geschwindigkeit auf Hardware nicht reicht.
- **Bekannte Vereinfachungen, die später nachzuziehen sind:**
  - Phiolen wirken mit Stufe 2, weil Objekte keine Zusatzdaten tragen.
  - Enchant wirkt pro Einheit statt pro Waffe; Waffen am Boden werden nicht verzaubert.
  - Subversion verbietet alle Reittiere; die Ausnahme „nur Reittiere mit Zauberer“ kommt mit dem Reiten.
  - Höchstens 4 Kessel, 64 Bodenobjekte und 16 noch nicht abgerechnete Kills; Ereignis-Ring 16 Einträge (volle Ring verwirft Neues — Darstellung only).
  - Lexikon: geteilte Phiole-Kacheln markieren den ersten passenden Objekt-Typ.
- `tools/mockup.py` rendert nur das Zauberer-Haus (9×9).

---

## 9. Wo steht was

| Thema | Datei |
|---|---|
| Offene Annahmen der Umsetzung von D67 | `docs/FRAGEN.md` |
| Spieldesign, Entscheidungen D1–D67, M4-Plan | `docs/design/GDD.md` (§14, §16) |
| **Originalregeln (Spectrum)**, Quelle der Wahrheit für D66/D67 | `docs/REGELN-ORIGINAL-SPECTRUM.md` (Kopie aus `lords-of-chaos-zx-agon`) |
| Vergleich unserer Regeln mit dem Original, Vorschläge R1–R39 | `docs/REGELVERGLEICH-SPECTRUM.md` |
| Plan: Regeln des Originals und schlauere KI | `docs/PLAN-KI.md` |
| Playtest 2026-10-06 (Befund, Entscheidung, Stand) | `docs/PLAYTEST-2026-10-06.md` |
| Plan Level 1 auf 46×46 | `docs/PLAN-KARTE-46.md` |
| Mockups, Kartenvorschauen | `docs/design/mockups/` |
| Amiga-Beobachtungen | `docs/design/amiga-observations.md` |
| Roadmap und Arbeitsweise | `docs/ROADMAP.md` |
| Architektur | `docs/ARCHITECTURE.md` |
| Architekturentscheidungen | `docs/adr/0001` bis `0011` (Rendering 0006, Eingabe 0007, Daten 0008, Sicht 0009, SD-Daten 0011) |
| Plattform-Quirks | `docs/AGON-QUIRKS.md` |
| Testen und Debuggen | `docs/TESTING.md` |
| Änderungen | `CHANGELOG.md` |
