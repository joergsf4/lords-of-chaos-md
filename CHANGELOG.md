# Changelog

Format nach [Keep a Changelog](https://keepachangelog.com/de/1.1.0/), Versionen nach Milestones (siehe `docs/ROADMAP.md`).

## [Unreleased] – Standard-Zauberer mit starken Kreaturen (2026-10-08)

### Kamera und Karte (D85, D86)
- **Ziffernblock (D88):** Mit NumLock an bewegen 1 bis 9 (ohne 5) wie die Pfeile samt Diagonalen und Wiederholung (`input_arrow`, `chord_keys`); mit NumLock aus ging es schon über Pfeile, Pos1, Ende, Bild↑/↓. Die Codes (0x0C + Ziffer) folgen der FabGL-Reihenfolge und sind im Emulator noch nicht gemessen.
- **Kamera zentriert nur alle zwei Schritte (D85):** Das Fenster bleibt stehen, solange die Einheit höchstens ein Feld von der Mitte entfernt ist, und zentriert neu, wenn sie zwei Felder entfernt ist (`VIEW_SLACK` in `src/core/view.c`). Ersetzt D78 (jeder Schritt).
- **Neutrale und Feinde unterscheiden (D86):** Gesamtkarte: du weiß, Feind rot, Neutral hellcyan, mit Legende unter der Karte. Fenster dafür dunkelcyan, Sumpf magenta.

### Türen in senkrechten Wänden (D84)
- **Kein Extra-Objekt mehr:** Das Blatt einer offenen Tür in einer senkrechten Wand steht nicht mehr als eigene Kachel auf dem Nachbarfeld, sondern wird in der Türkachel neben dem Rahmen gezeichnet, wie bei den waagerechten Wänden. Vier neue Kacheln `door_v_open_{e,w}{n,s}` (Seite, Angel), die vier `door_leaf_*` entfallen. `view.c`: `door_v_open_tile`. Die Markierung auf dem Nachbarfeld bleibt als unsichtbare Zeichenmarke. View-Hash neu.

### Log und Karte (D83)
- **Log mit allen Ereignissen außer Laufen:** Treffer, Wunden, Fehlschläge, Tode, Zauber und zerstörtes Gelände kommen aus dem Ereignisring (`fx_set_event_hook`, `log_event`), Funde, Türen, Essen und Ähnliches aus den Meldezeilen (`render_set_message_hook`, `log_message`). 20 statt 10 Einträge.
- **Log neben der Gesamtkarte:** beginnt oben, neuester Eintrag zuerst, direkt am Kartenrand (erste freie Textspalte) statt ab Spalte 27, Umbruch an Leerzeichen.
- **Gesamtkarte lesbarer:** Wände hellgrau, Fenster cyan, Türen gelb, Wege und Brücken gelb, Holzboden braun, Gras grün, Wald hellgrün, Wasser blau, Sumpf cyan; Felder ohne Lücke. Die aktive Figur blinkt weiß/grün.

### Neu
- **Inventar (D77):** Taste `i` (D87; das Lexikon liegt jetzt auf `k`). Die Einträge des Rucksacks mit Pfeilen wählen; unten stehen Kachel, Zahlen und die Beschreibung aus dem Lexikon (`screen_inventory`). Enter oder `w` nimmt den Gegenstand in die Hand (`items_wield`), nochmal Enter legt ihn weg. Das Schild wird nur getragen.
- **Bonus im Balken (D77):** Waffe in der Hand (Kampf) und bestes getragenes Schild oder Waffe (Verteidigung) erscheinen im Balken als Bonus-Segment (`items_combat_bonus`, `items_defence_bonus`).
- **Lexikon:** Alle 40 Objektbeschreibungen neu geschrieben, jede beginnt mit „Wozu:" und nennt die Zahlen (Apfel heilt 10, nicht 4, wie bisher dort stand). Puffer 7168 Byte.
- **Weiche Dachkanten (D80):** Kacheln `roof_half` und `roof_faint`, Regel `push_roof_fringe` in `src/core/view.c`.
- **Kamera zentriert (D78):** Die Einheit steht in der Fensterhälfte (Rand 4 statt 2).

### Geändert
- **Tür höher als Fenster (D81):** `door_h_closed`, `door_h_open` und die Blatt-Kacheln zwei Reihen höher, `window_h` und `window_v` niedriger (`tools/art/make_door_leaf.py`, `make_night_set.py`, Aseprite-Quellen abgeglichen).
- **Schneller reagieren (D82):** Das Gleiten startet vor Sicht und Neuzeichnen, Akkord-Fenster 40 statt 80 ms.

### Behoben
- **Zweimal schräg (D79):** Die Wiederholungsverzögerung läuft ab Schrittende (`chord_done`).

### Behoben
- **Türblätter blockieren nicht mehr (D76, ersetzt das Blockieren aus D61):** Neben offenen Türen kam die Meldung „Unzerstörbar", weil das unsichtbare Blattfeld blockierte (bei waagerechten Wänden zeichnet D65 das Blatt im Rahmen). Das Blatt ist jetzt nur noch eine Zeichenmarke. Die Tür kann nicht mehr klemmen (`world_door_jammed` entfällt); ohne freies Nachbarfeld öffnet sie als bloßer Rahmen. Tests `d61`/`d76` angepasst. **Offen:** die senkrechten Türblätter müssen neu gezeichnet werden.

### Geändert
- **Standardvorlage** (`wizard_apply_standard_set`) enthält zusätzlich **Vampir 1** (29 XP, Mana 28) und **Riese 1** (17 XP, Mana 16): zwei starke Kreaturen, zusammen 46 XP, die von den Attributen abgehen. Das 600-XP-Budget bleibt eingehalten und wird bis auf höchstens 1 XP ausgegeben (Test verschärft). Test in `tests/selftest.c` ergänzt.

## [Unreleased] – ADRs zu Dateiformaten und KI, Aufräumen (2026-10-08)

### Dokumentation
- **ADR 0014 Dateiformate und Versionierung:** alle zwölf eigenen Dateien mit Magic, Version, Erzeuger, Lader und Verhalten bei Fehlern; Regel für Versionswechsel (Spielstand: jede Strukturänderung erhöht `SAVE_VERSION`, alte Stände werden abgelehnt); Szenario-Format v3; Spielstand-Reserve (11,5 von 12 KB).
- **ADR 0015 Aufbau der Computer-Gegner:** Module, Phasenablauf, Entscheidungsschleife K10.3, Wissen nur aus eigener Sicht, Wege, Pläne und Profile, Laufzeit.
- **Aufgeräumt:** ADR 0003 (Kachel-IDs statt `screen.h`, Testpfad), ADR 0008 (Nachtrag mit heutigen Zahlen), `ARCHITECTURE.md` (Modulübersicht, `HOUSE_VIEW_HASH`), `TESTING.md`. Folge des Architektur-Reviews 2026-10-08.

## [Unreleased] – ADR 0009 überarbeitet (2026-10-08)

### Dokumentation
- **ADR 0009 (Sicht)** beschreibt jetzt den gebauten Stand: Achteck-Reichweite und Höhenpaare nach K11 (D67), Shadowcasting (D39), Deckung, Feuerwand, Schuss- und Zauberlinien (D68), fünf Bitfelder bis 46×46, Zwischenspeicher an `World.generation`. Messreihe ergänzt (Emulator 22 ms auf Level 1); die Hardware-Messung nach K11 steht aus. Anlass: Architektur-Review 2026-10-08 (Konflikt K1).

## [Unreleased] – Aseprite als Grafikquelle (ADR 0013, 2026-10-08)

### Hinzugefügt
- **Aseprite-Quellen:** Alle 321 Bilder (Kacheln und Icons) liegen als 16 Familien in `assets/aseprite/*.aseprite`, jede Kachel als Slice. `tools/art/aseprite.py import|export|check|list` wandelt hin und zurück; `check` vergleicht pixelgenau, `export` schreibt nur geänderte PNGs.
- **MCP-Server `aseprite`** ([diivi/aseprite-mcp](https://github.com/diivi/aseprite-mcp), gepinnt): `tools/setup.py` richtet ihn in `toolchain/aseprite-mcp/` ein (`--no-aseprite` überspringt), `.mcp.json` meldet ihn für Claude Code an.

## [Unreleased] – Tempo und Überblick in Welt 1 (Playtest 2026-10-08)

### Hinzugefügt
- **Geräusche mit Richtung (D69):** Kampf, Zauber und Tod, die eigene Figuren in 16 Feldern hören, aber nicht sehen, erscheinen zu Beginn der eigenen Runde als Warnzeile („Kampflaerm im Nordosten, 12 Felder“). Die Gesamtkarte markiert die groben Herkunftsorte gelb.
- **Überreste (D70):** Wo eine Kreatur stirbt, bleibt ein Skelett; `x` zeigt, was es war. Neue Kachel `remains`.
- **Sichtwarnung (D73):** Kommt eine fremde Kreatur ins Blickfeld, steht unten „Goblin im Osten!“, bis man handelt.
- **Automatischer Rundenwechsel (D72):** Beim ersten Rundenwechsel fragt das Spiel; `<auto>`/`<man>` oben rechts, Shift+A schaltet um.
- **Nachrichten neben der Gesamtkarte (D74).**

### Geändert
- **AP-Faktor je Szenario (D71):** Level 1 gibt allen Einheiten ×1,28 AP (Zauberer 51 statt 40), passend zur 46×46-Karte. Szenariodatei **v3** (`ap_scale`).
- **Schnellere Phasenbildschirme (D72)**, dafür bleibt die Karte nach einem sichtbaren Gegnerangriff 1,2 s stehen.
- **Welt 2 und 3 ausgegraut (D75).**
- **Spielstand v12.**

### Intern
- `world_noise`/`world_noise_clear`, `world_add_remains`/`world_remains_at`, `world_scale_ap`/`world_set_ap_scale`; `fx_drain_play` meldet gezeigte Kämpfe; `render_side_clear`, `render_status_tag`. Selftest `test_d69_d71`; View-Hash wegen der neuen Kachel neu.

## [Unreleased] – Offene Fragen F3, F7, F8, F15, F31 (2026-10-08)

### Hinzugefügt
- **Zaubern aus der Luft, Zielhöhe CAST-A/G (D68, F8):** Ein fliegender Zauberer zaubert jetzt, nur Beschwörungen und Tränke gehen weiter allein vom Boden. Im Zielmodus wählt `<` die Luft (blauer Cursor) und `>` den Boden (gelb); voreingestellt ist die eigene Höhe. Bolt, Blitz, Fluch, Unterwerfung, Magic Attack und Enchant treffen nur, was auf dieser Höhe steht. Der Blitz zerschlägt Gelände nur am Boden, Flächenzauber gehen nur auf den Boden. Teleport in die Luft braucht einen Flying-Trank. Die KI zielt auf die Höhe ihres Ziels.
- **Setup:** `Z` würfelt den Zufallszauberer neu.

### Geändert
- **Reiterwunden bleiben (F3) und bluten im Sattel (F31):** Wer verwundet aufsitzt, verliert auch im Sattel (auch im Flug) 2 Con je Wunde und Runde und steigt mit denselben Wunden wieder ab. Verblutet er, stirbt er im Sattel; sein Gepäck fällt aufs Feld, das Reittier bleibt. **Spielstand v11** (alte Stände werden abgelehnt).

### Entfernt
- **Zufalls-Zauberer-Stärke im Setup (F15):** Sie wirkte seit D67 nicht mehr.
- **Reste der kritischen Treffer (F7):** Felder `crit`/`return_crit`, „KRIT!“-Meldungen, Krit-Klang und Ausrufezeichen an der Schadenszahl, Spalte „crits“ der Arena.

### Intern
- `spell_line_clear`, `spell_needs_ground`; neuer Parameter `air` in `spell_bolt`, `spell_lightning` und `spell_apply`; `combat_damage` ohne Krit-Parameter; `wizard_slot_random` ohne Stärke. Neue Selftests für F3, F8 und F31; der Versionstest des Spielstands hängt nicht mehr an der Nummer.

## [Unreleased] – Regeln wie im Original, Schritt 0b: Zustände (D67) (2026-10-07)

### Geändert
- **Wunden als Zähler 0–7 (R9):** Jede Wunde kostet 2 Constitution je Runde, Curse setzt sofort 7. `UF_WOUNDED` zeigt nur noch an, ob der Zähler über 0 steht. Eine neue Wunde gibt es bei Schaden über einem Viertel von ConMax.
- **Constitution-Faktor (R10):** Die AP der Runde werden durch `⌊ConMax/Con⌋` geteilt (halb unter 50 %, ein Drittel unter 33 %). Combat und Defence folgen in 0c.
- **Ausdauer (R11):** Erschöpft ist, wer unter einem Sechstel des Maximums liegt (halbe AP); Erholung je Runde ein Sechstel, mit Speed die Hälfte. Nahkampf kostet 8 AP **und 8 Ausdauer**, Zaubern, Werfen, Schießen, Aufheben, Essen, Trinken, Lesen nur AP. Bewegung kostet weiter die Hälfte der AP als Ausdauer. Die Spalte `stamina` in `data/actions.csv` wird jetzt gelesen (`world_pay`).
- **Schweben kostet Ausdauer (R12):** Wer fliegt, zahlt je Runde die Hälfte seiner nicht ausgegebenen AP als Ausdauer.
- **Abheben und Landen (R20):** Abheben braucht 6 AP, darf nicht gebunden sein, kein anderes Wesen auf dem Feld, kein Dach, nicht auf Vine oder Blob. Landen ist kostenlos, nicht auf Feuer oder Blob, nicht mit einem anderen Wesen auf dem Feld. Die AP werden beim Wechsel **anteilig** umgerechnet (z. B. Giant Bat: 18 von 24 am Boden werden 46 von 62 in der Luft). Das Budget je Ebene (D15) entfällt.
- **Ertrinken (D48)** rechnet mit der neuen Erholung: Das Treten im Wasser kostet 5/12 des Maximums, netto weiter etwa ein Viertel je Runde.
- **Spielstand v9** (alte Stände werden abgelehnt).

### Intern
- Neue Hilfen `world_pay`, `world_can_pay`, `world_spend_ap`, `world_set_wounds`, `world_con_factor`. Test `test_0b`, mehrere ältere Selftests auf die neuen Zahlen umgestellt.

## [Unreleased] – Regeln wie im Original, Schritt 0c: Nahkampf (D67) (2026-10-07)

### Geändert
- **Nahkampf wie im Original (K6.2):** Schaden = `RND(min(255, 2·(Combat_eff+1))) − Defence_eff`, bei 0 ein Fehlschlag. Die Trefferchance 10–90 % (D16), die Waffenwürfel (D28) und die kritischen Treffer (D30) entfallen.
- **Waffen (K7):** `data/weapons.csv` trägt die Originalwerte (Combat, Defence, Wurfwert). Die Waffe in der Hand gibt Combat, die **beste** Verteidigung aller getragenen Gegenstände zählt zur Defence (nicht addiert). Verzauberte Waffen zählen doppelt. Kreaturen ohne Waffenflag (Drachen u. a.) bekommen keine Bonuswerte.
- **Effektive Werte (K6.1):** Combat und Defence werden durch den Constitution-Faktor `⌊ConMax/Con⌋` geteilt, mindestens 1 (löst den Malus −2 ab).
- **Rückschlag (K6.2):** Der Verteidiger schlägt **jedes Mal** zurück, wenn er noch 4 AP und 4 Ausdauer hat, und zahlt beides. Die einmalige Reaktion je Runde (D29) und der kostenlose Gegenschlag (D27) entfallen.
- **Gebunden (K11.7), ersetzt D26:** Wer sich neben einen sichtbaren Gegner derselben Höhe bewegt oder angreift, ist gebunden und **zieht nicht mehr** (auch kein Abheben oder Landen); nur der Ziehende bzw. Schlagende, nicht der Gegner, außer er antwortet. Der freie Schlag beim Wegziehen entfällt, die KI flieht nicht mehr aus der Bindung. Unsichtbare werden nicht gebunden. Neue Meldung „Gebunden: Gegner nebenan“.
- **Gelände angreifen (K6.4):** 6 AP und 6 Ausdauer; versucht wird es nur bei `1,5·Combat_eff ≥ Zähigkeit`, es gelingt bei `RND(2·Combat_eff) ≥ Zähigkeit`. Die Zähigkeiten in `data/features.csv` stehen jetzt auf der Skala des Originals (Tür 60, verschlossen 80, Baum 80, Fels 200, …). Normale Kämpfer brechen damit keine Türen mehr auf, dafür gibt es Schlüssel.
- Bogen und Wurf nutzen dieselbe Formel (Bogen Angriffswert 15, Wurfwert aus der Waffentabelle).

### Intern
- `combat_roll`, neue `combat_hit_chance(A, Def)` (für die KI); `combat_free_swing`, `combat_disengage_swings`, `items_attack_damage` entfallen. Die Kampf-Selftests sind neu geschrieben.

## [Unreleased] – Regeln wie im Original, Schritt 0d: Zauber, Tränke, Schild (D67) (2026-10-07)

### Geändert
- **Magic Bolt und Lightning (K5.3):** Angriffswert `4L+25` bzw. `4L+30`, Schaden wie im Nahkampf gegen die Defence des Ziels (Schild zählt, Untote werden getroffen). Der Widerstandswurf gegen MR (D40) und die Würfel (D28/D29) entfallen. Lightning wirkt auf das Zielfeld und die 8 Nachbarfelder; Gelände dort bricht bei `RND(2A) ≥ Zähigkeit`.
- **Reichweite (K1, K5.1):** Entfernung ist `2·max(dx,dy) + min(dx,dy)` mit Umbruch; Zauber reichen `2L+7`, Magic Eye `3L+10`, Teleport `2L+30` Einheiten (Stufe 1: 4 Felder gerade, 3 diagonal). Das Zielkreuz färbt sich rot außerhalb.
- **Magic Shield:** Defence `+4(L+1)+12` für `L+1` Runden, fest. **Enchant** hält `L+3` Runden.
- **Curse, Subversion, Magic Attack:** Erfolg bei `RND(8L+55) + Bonus ≥ MR` (Curse +10). Curse setzt 7 Wunden. Subversion wirkt jetzt auch auf **Reittiere** (das höhere MR von Tier und Reiter zählt), Wizards und Tiere mit Wizard im Sattel sind immun. Magic Attack trifft alle Wesen derselben Art im Abstand `D < 2L+1` einzeln (auch eigene).
- **Teleport (K5.3):** Landepunkt streut ab `D ≥ 2L` um `RND(n) − ⌊n/2⌋` je Achse (`n = ⌊(D−2L)/2⌋+1`); ist das Feld belegt oder fest, scheitert der Zauber (Mana verbraucht). Danach 0 AP.
- **Tränke (K8.2):** Strength +20 Combat, Protection +25 Defence (fest), Dauer `⌊(3(L−1)+10)/Verbrauch⌋` Runden. Der Super-Trank (Stärke+Schutz+Tempo) ist implementiert, aber noch ohne Zutat zum Brauen. Magic Resistance ist der reine Kreaturenwert; Schilde und Tränke wirken auf Defence.
- Entfallen: Spalten `dice_n`, `die`, `splash_*` in `data/spells.csv`, `combat_spell_hit_chance`, `COMBAT_CRIT_PERCENT`.

### Intern
- `world_range`, `spell_range`, `spell_in_range`, `spell_attack_value`. Test `test_0d`.

## [Unreleased] – Regeln wie im Original, Schritt 0e: Flächen und Fernwaffen (D67) (2026-10-07)

### Geändert
- **Magic Fire und Gooey Blob (K5.3):** Der Zauber entzündet das Zielfeld, wenn `RND(10−L) < Entzündbarkeit` (Gelände und Möbel, neue Tabelle `data/terrain_effects.csv` und Spalte `fire` in `features.csv`). Jede Runde breitet sich das Feld auf Nachbarn aus (`RND(70−3·Lv)+1 ≤ f`) und überlebt mit `RND(T) < ⌊S/4⌋+40` (Tabellen T je Stufe). Verbranntes Gelände wird zu festem Boden, Bäume und Möbel in erloschenem Feuer sind Asche. Schaden je Runde: Feuer `25+2F`, Blob `16+2(B−1)`, ohne Defence; eigene Kreaturen sind verschont, Flieger über dem Feuer auch.
- **Tangle Vine und Flood:** füllen ein 9×9-Feld um das Ziel (`D ≤ L+3`, `RND(18−2L+2D) < Empfänglichkeit`). Vine verwundet mit 8 je Runde und bleibt. Flood ist Wasser: Betreten kostet 12 AP, Treten im Wasser ermüdet wie D48 (der Sofort-Tod mit 50 % entfällt).
- **Eine Fläche je Art und Zauberer;** ihre Stufe ist die des letzten Zaubers (ein schwächerer späterer Zauber schwächt das ganze Feuer). Feuer-Felder kosten 16 AP beim Betreten.
- **Gegenstände am Boden:** Feuer vernichtet alles außer Schätzen, Schlüsseln, Kesseln und dem Magie-Slayer; Flood spült alles weg.
- **Blob und Vine** sperren immer, lassen sich aber zerreißen (Zähigkeit 40/50, Angriff 6 AP + 6 Ausdauer, `1,5·Combat_eff ≥ Zähigkeit`).
- Flächenzauber brauchen keine Sichtlinie, nur Reichweite; der Zauber kann wirkungslos verpuffen (Mana ist weg).
- **Wurfweite (K6.4):** `min(36, ⌊2·Combat_eff/Gewicht⌋+5)` Einheiten, auch für Phiolen. **Bogen:** 16 Einheiten, verzaubert 22 (Angriff 15 bzw. 30). **Drachenfeuer** als Aktion (`f`): 12 Einheiten, Angriff 35, trifft auch Untote, entzündet das Zielfeld mit `RND(20) < Entzündbarkeit`, ohne Bogen.

### Intern
- `area.c` neu geschrieben (`area_set`, `area_cast` mit Zufall, `area_level`, `area_toughness`, `area_remove_field`); `items_can_fire`, `items_fire_range`, `items_throw_range`; `test_m4d` und `test_0e`.

## [Unreleased] – Regeln wie im Original, Schritt 0f: Beschwören und Designer (D67) (2026-10-07)

### Geändert
- **Beschwören (K5.3), ersetzt D34:** Ein Zauber auf Stufe `L` ruft **L Kreaturen** (je bis zu 40 Versuche für ein zufälliges freies Nachbarfeld), kostet `Mana × (L+1)` und **verbraucht eine Stufe**. Gibt es kein freies Feld, sind Mana und Stufe weg. Beschworene haben die Tabellenwerte (kein +15 % je Stufe mehr). `spell_summon` bekommt den Zufallsgenerator.
- **Designer-Preise (K3.3):** Ein Attributpunkt kostet `⌊Wert/Teiler⌋` XP (Combat/Defence 2, MR 16, Constitution 10, Ausdauer 8, Mana 10, AP 4). Höchstwerte: Combat/Defence 30, MR 100, Constitution/Ausdauer 90, Mana 200, AP 40. Ein Zauberlevel von `L` auf `L+1` kostet `xp_base + xp_step × L` (Tabelle K5.2, alle Zauber, bis Stufe 8; die Bombe ist nicht käuflich). Die Spalte `design_cost` entfällt.
- **Standardvorlage** kauft die Grundzauber und acht Kreaturen und gibt den Rest reihum in die Attribute, immer innerhalb der 600 XP.
- **Zufallszauberer (K3.2):** Combat 6, Defence 6, MR 90, jeder Zauber `RND(3)` (0–2), keine XP; die Stärke aus dem Setup wirkt nicht mehr.
- Beschwörungsliste im Designer: Spalte „Anz“.

### Intern
- Die KI beschwört weiter bis zu 5 Kreaturen (D62), zählt aber jetzt die tatsächlich erschienenen.

## [Unreleased] – Regeln wie im Original, Schritt 0g: Wertung und Sonderfälle (D67) (2026-10-07)

### Geändert
- **Siegpunkte (K6.5):** Ein Zauberer, der etwas besiegt, bekommt den doppelten Tabellenwert des Opfers (egal ob im Nahkampf oder mit Fernwaffe), eine beschworene Kreatur den einfachen. Ein getöteter Zauberer zählt `4·Level+15` (für Kreaturen als Töter die Hälfte). Der Punktestand stoppt bei 255. Der Level des menschlichen Zauberers kommt aus dem Designer-Slot.
- **Portal schließt (K4):** Karten (Format v5) tragen die Offen-Dauer: Level 1 12 Runden, Slayer's Dungeon 15, Ragaril's Domain 10 (Tutorial: bleibt offen). Danach ist niemand mehr zu retten und das Spiel endet; wer nicht entkommen ist, verliert.
- **Pixies sind immer unsichtbar** (auch wenn kein Trank wirkt).
- **Reiter mit eigenen AP (K6.6):** Wer im Sattel sitzt, behält seine AP und seine Ausdauer. Zaubern, Werfen, Schießen, Aufheben, Ablegen, Essen, Trinken, Lesen, Türen und Truhen kosten den **Reiter**, Bewegen, Kämpfen, Abheben und Landen das **Reittier**. Der Reiter bekommt zur Runde seine eigenen AP zurück (mit seiner Constitution und Ausdauer). Beim Absteigen oder Abwerfen behält er den Stand.
- Kartenformat **v5** (Portalblock mit fünf Bytes); die SD-Karte muss neu bespielt werden.

### Intern
- `world_pool_ap`, `world_spend_ap_for`, `world_has_ap`; `game_set_portal_span`, `game_set_wizard_level`; Tests `test_0g` und neue Wertungstests.

## [Unreleased] – Regeln wie im Original, Schritt 0h: Sicht (D67) (2026-10-07)

### Geändert
- **Reichweite als Achteck (K11.1):** `2·dx < R`, `2·dy < R`, `D < R` mit `R` = 19 am Boden (9 Felder gerade, 6 diagonal) und 23 in der Luft (11 und 7). Die Eckfelder des alten Quadrats entfallen. Der Algorithmus bleibt Shadowcasting (D39).
- **Nachbarn immer sichtbar:** Die acht Nachbarfelder sieht jeder Beobachter, auch hinter einer Wandecke.
- **Höhen (K11.2):** Flieger sieht man am Boden immer (über Wände), außer der Beobachter steht unter einem Dach. Flieger sehen Bodenziele nicht unter Dach und nicht unter Blätterdach (Wald, hohes Gras, Bäume), außer im Abstand `D < 4`; Gegenstände unter Dach/Blätterdach nie. Neue Bitmaps `vis_ground`, `vis_air`, `vis_obj` in `Sight`; `sight_unit_visible` ersetzt die Feldabfrage bei Einheiten (Anzeige, Namen, Lexikon, KI-Zauberer).
- **Magic Eye** sieht `D ≤ 3L+10` Einheiten weit, durch Wände und Deckung, und zeigt Unsichtbare.
- **Feuer und Blob blockieren die Sicht** (K11.4); die Sichtkarte folgt den Flammen (Kartenänderung bei jeder Flächenänderung).
- **Schusslinie (K11.6):** `sight_shot_clear`: Boden-Boden braucht eine freie Linie; ist die Luft beteiligt, stoppt nur ein Dach (Schütze unter Dach nach oben, Ziel unter Dach von oben). Der Bogen nutzt das.

### Intern
- `sight_in_reach`, `sight_object_visible`, `sight_shot_clear`; `sight_add_eye` bekommt die Reichweite. Test `test_0h`, der Sichtfeld-Test zählt jetzt das Achteck.

## [Unreleased] – Schlauere KI, Schritt 1: Kreaturen nach K10 (D67) (2026-10-07)

### Geändert
- **`ai.c` aufgeteilt:** `ai.c` (Wildtiere, Verteilung), `ai_creature.c` (Entscheidungsschleife), `ai_items.c` (Gegenstände), `ai_nav.c` (Schritte, Wegsuche, Türen), `ai_wizard.c` (KI-Zauberer), gemeinsame Typen in `ai_priv.h`.
- **Entscheidungsschleife je Kreatur (K10.3):** höchstens 50 Durchgänge. Reihenfolge: Schlaf, Sofortaktionen (Kessel trinken, aufheben, Phiole, Essen, Beute dem Zauberer zuwerfen, Waffe wechseln, Abheben), Fernangriff, Flucht, Nahkampfziel, Gegenstandsziel, Portal, Leibwache, Posten, Umherstreifen.
- **Sichtliste je Kreatur (K11.5):** bis 20 sichtbare Gegner (mit `C_eff`, `Def_eff`, Entfernung) und 24 Gegenstände (plus Truhen für Kreaturen, die sie öffnen können), nach Entfernung. Neu: `sight_sees` für einen einzelnen Beobachter nach K11.
- **Ziel und Flucht (K10.4):** Nahkampfziel nur bei `2·C ≥ 1,5·Def` des Gegners (und Untoten-Regel); Bedrohung bei `2·Def < 1,5·C`; Flieger ignorieren Bedrohungen am Boden, Untote fürchten nur Untote und Magie, **Aggressive und Gebundene fliehen nie**. Fluchtfeld: erstes Nachbarfeld ohne Schusslinie eines Gegners, sonst das mit der größten Abstandssumme; danach endet der Zug.
- **Fernangriff:** Bogen und Drachenfeuer gegen Ziele mit `2·A ≥ 1,5·Def`, in Reichweite und Schusslinie.
- **Gegenstände (K10.6):** Wunschwerte in `data/ai_items.csv`; aufgehoben wird, was `⌊Wert/(D+1)⌋` am höchsten hat (Tragkraft, Waffenhände, Schlüssel nur mit „Use“); die beste Waffe wird geführt, bei `1,5·Con < ConMax` oder Ausdauer ≤ ¼ gegessen, Phiolen getrunken. **Beute (Schätze, Zutaten, Nahrung) wirft die Kreatur ihrem Zauberer zu** (landet auf seinem Feld) und läuft zu ihm.
- **Schritt:** Wegsuche (Breitensuche) zuerst, sonst die Nachbarn nach Entfernung zum Ziel sortiert, ohne die **letzten 8 besuchten Felder**; Türen öffnen („Use“), Wände nur bei `1,5·C ≥ Zähigkeit`.
- **Abheben und Landen:** Wer Flug-AP hat, hebt sofort ab; zum Kämpfen und Aufheben landet er neben dem Ziel und handelt im selben Zug.
- **Leibwache:** Kreaturen ohne Plan (Plan-Route 255) gelten als aggressiv und bleiben unter Entfernung 5 beim eigenen Zauberer; ab der Portalrunde ziehen alle KI-Kreaturen zum Portal. Neutrale Monster ohne Posten streifen weiter umher.
- **Kreaturentabelle** bekommt `aggr` (K2). Einheiten tragen Plan (`plan_route`, `plan_step`, `plan_flags`) und den Ring der besuchten Felder. **Spielstand v10.**

### Intern
- Tests `test_ki1`; der Wächter-Test nimmt einen Spectre (ein Zombie greift einen Zauberer nach K10.4 nicht an).

## [Unreleased] – Schlauere KI, Schritt 2: Zauberer, Routen, Schlaf (D67) (2026-10-07)

### Geändert
- **Szenariodatei v2 (`LOCS`):** trägt jetzt die KI-Daten: Werte und Name des Gegner-Zauberers, Zauberlevel **und Prioritäten**, Routen mit Wegpunkten, Pläne der Karteneinheiten und Auslöser. Die Texte `data/scenarios/*.txt` bekommen die Zeilen `wizard`, `book … spell level prio`, `route`, `summon_routes`, `plan`, `trigger`. Die drei Szenarien haben die Tabellen des Originals (K10.2): **Torquemada** (Mana 120, Con 63, Combat 9, Defence 10), **Elbo Smogg** (140) und **Ragaril** (Mana 200, Stamina 78, Con 50, Combat 13, Defence 18) mit ihren Büchern und Prioritäten.
- **Zauberwahl (K10.5):** Der KI-Zauberer wirkt nach Priorität; nach jedem Wurf wird die Priorität **dauerhaft halbiert**, ungerade Prioritäten bleiben zurück, bis ein Durchgang ohne Wurf sie abrundet. Bedingungen: Beschwören behält 40 Mana Reserve (außer Gegner sind sichtbar) und braucht `L` freie Nachbarfelder; Bolt und Lightning nur gegen Ziele mit `2·A ≥ 1,5·Def` im Abstand `D ≤ 2L+6` (Lightning nicht neben eigenen Kreaturen); Feuer, Blob, Vine, Flood gegen Gegner auf empfänglichem Gelände (Vine und Flood nicht nahe der eigenen Truppe); Shield nur ohne aktives; Tränke im leeren Kessel mit Zutat (legt sie vorher ab); Drachen mit Kessel und Drachenkraut. Enchant, Subversion, Curse, Magic Attack, Teleport und Magic Eye wirkt er nie.
- **Der Zauberer läuft Routen** wie jede KI-Kreatur, ist nie aggressiv, flieht vor Stärkeren, sammelt Schätze und Schlüssel im Blick (ersetzt „bleibt im Haus“, D62), öffnet Truhen mit Schlüssel und geht ab der Portalrunde zum Portal und hinaus. Die Obergrenze von 5 Beschwörungen entfällt: Mana, Reserve und freie Nachbarfelder begrenzen.
- **Pläne (K10.7):** Beschworene würfeln beim Erscheinen: mit `aggr` Prozent Leibwache (bleibt unter Entfernung 5 beim Zauberer), sonst eine zufällige der ersten `summon_routes` Routen, die zur Kreatur passt (Flieger, „Use“, Tragkraft, Wood/Water/Rock). Zauberer nehmen nur Routen mit Wizard-Flag. Acht Routen je Karte (Struktur des Originals, Wegpunkte auf erreichbaren Feldern der eigenen Karten).
- **Schlaf und Auslöser:** Schlafende Kreaturen (Plan-Bit 6) tun nichts, bis sie einen Gegner sehen oder ein Auslöser feuert: Türen öffnen, Truhen öffnen, Wände brechen, Blitz auf Gelände, Flächenzauber ändern das Feld (`world_poke`). Die Szenarien haben noch keine schlafenden Wächter.
- **Schritt:** zuerst die Nachbarn nach Entfernung zum Ziel (die Regel des Originals), die Breitensuche nur wenn alle blockiert sind; befreundete Wesen auf dem Zielfeld sind kein Hindernis.
- Ohne Szenariotabelle (Tests, Karten ohne KI-Daten) bekommt der Zauberer eine Standard-Priorität aus seinem Buch.

### Intern
- `ai_scenario_load`, `ai_profile_apply`, `ai_plan_new`, `ai_wizard_cast`; die Frontend-Dateien laden das Profil (`scnfile_ai`). Spielstand-Daten: Plan und Besuchsring der Einheiten (v10). Neue Tests `test_ki2`; alte D62-Tests entfallen.

## [Unreleased] – Schlauere KI, Schritt 3: Duell-Programm (D67, Phase 5) (2026-10-07)

### Neu
- **`host/duel.c`:** Zwei KI-Zauberer auf einer Szenariokarte, beide durch die echte KI (Entscheidungsschleife, Zauberwahl, Routen) und die echten Kernregeln. Spieler 2 ist der Zauberer des Szenarios (Werte und Prioritäten nach K10.2), Spieler 1 der Standardzauberer des Designers. Ausgabe: Siege, Runden, Kills, Einheiten am Ende und die gewirkten Zauber des Gegners. Bauen und Aufruf stehen im Kopf der Datei.
- **Läufe (10 Partien, Seed 3, höchstens 60 Runden):** Many Coloured Land: Torquemada 9 Siege, 1 unentschieden, 19 Runden im Mittel; Slayer's Dungeon: Elbo Smogg 10 von 10, 10 Runden; Ragaril's Domain: Ragaril 9 von 10, 19 Runden. Der Gegner beschwört bis zu 12–18 Kreaturen, wirkt Shield, Bolt, Blob/Feuer und viele Beschwörungen. **Mit den Originalwerten ist der Gegner deutlich härter als der Standardzauberer** (Con 63 gegen 34, Mana 120 gegen 80), wie im Plan erwartet; ein menschlicher Spieler mit voll ausgebautem Zauberer muss sich also Mühe geben.

### Intern
- Der eZ80-Selftest hat nur rund 20 KB für Heap und Stack (`loctest.bin` wuchs mit der KI um 40 KB): die beiden großen Spielstände der Tests liegen jetzt als eine statische Instanz vor, sonst stürzte das Testprogramm im Emulator ab (MOS startete es endlos neu). Neue Tests, die große Strukturen brauchen, dürfen sie nicht auf den Stack legen.

## [Unreleased] – Regeln wie im Original, Schritt 0a: Mana (D67) (2026-10-07)

### Geändert
- **Mana-Kosten `Basis × (L+1)`** nach der Tabelle K5.2 des Originals (Giant Bat Stufe 1 = 4, Magic Bolt Stufe 0 = 1). Die bisherigen Werte in `data/spells.csv` hießen `mana_base`/`mana_step`, waren aber die Designer-XP-Preise; sie heißen jetzt `xp_base`/`xp_step`, die neue Spalte `mana` trägt die Basis.
- Zauberer-Info im Designer zeigt `Mana L1:<Basis> x(Stufe+1)`.

### Intern
- Selftests (d34, Brauen, Beschwören ohne Platz) auf die neuen Kosten umgestellt.

## [Unreleased] – Abgleich mit dem Spectrum-Original (D66) (2026-10-06)

### Geändert
- **Aktionskosten (AP) wie im Original:** Zaubern, Nahkampf, Fernwaffe und Werfen 8; Aufheben 8; Fallen lassen 0; Essen und Füllen 4; Aufsitzen 10; Abfliegen 6; Landen 0. Rückschlag, Tür und Truhe bleiben eigene Regeln.
- **Kreaturen:** Dwarf, Goblin und Troll sind Wood-Typ, Giant Bat hat Trank-Verbrauch 2, der Ghost hat Use; der Zauberer trägt 36.
- **Gewichte:** Schwert 10, Messer 3, Schild 8, Bogen 4, Keule 9, Axt 7, Wurfstern 4, Slayer 9, leerer Kessel 15, leere Phiole 2, volle Phiole 4, Drachenkraut 2.
- **Essen:** Apfel heilt 10 Con; jede Nahrung gibt zusätzlich das Vierfache ihrer Heilung als Ausdauer.
- **Fliegen per Trank** gibt 2 × Boden-AP je Runde (vorher 1 ×).
- **Zauberer-Designer:** Start-Constitution 34 (vorher 25) und Start-Mana 80 (vorher 90) wie im Original; ein Punkt Constitution kostet 3 XP, ein Punkt Mana 8 XP (vorher 2 und 9). Die Standardvorlage kostet 531 XP und lässt 69 übrig.

### Neu
- `docs/REGELVERGLEICH-SPECTRUM.md`: Vergleich aller Regeln mit dem Spectrum-Original, gerechnete Beispiele und eine Vorschlagsliste R1–R39, mit dem Vergleich der KI (K10). Wichtigster Befund: Unsere Mana-Tabelle sind die Designer-XP-Preise (R1).

### Intern
- Selftests auf die neuen Zahlen umgestellt, eine neue Erwartung für Essen und Trankflug.

## [Unreleased] – Türblatt am Rahmen (D65) (2026-10-06)

### Geändert
- **Offene Türen in waagerechten Wänden zeigen ihr Blatt im Rahmen,** am Pfosten angeschlagen, statt als freies Brett auf dem Nachbarfeld. Vier neue Rahmenkacheln (`door_h_open_e/_w`, `door_h_far_e/_w`: Blatt zum Betrachter bzw. weggeschwenkt, Scharnier links bzw. rechts). Die Kacheln `door_leaf_e/_w` entfallen.
- Das Blattfeld (D61) blockiert und klemmt wie bisher, wird aber nicht mehr gezeichnet. Türen in senkrechten Wänden bleiben unverändert.
- Bild: `docs/design/mockups/door-leaf-d65.png`.

### Intern
- `tools/art/make_door_leaf.py` erzeugt die Rahmenkacheln (das Blatt wird gespiegelt, das Mauerwerk nicht). `view.c`: `door_h_open_tile`.
- Selftest d65 (alle Karten): Scharnierseite und Schwung folgen dem Blattfeld. `HOUSE_VIEW_HASH` neu, weil sich die Kachel-IDs verschoben haben.

## [Unreleased] – Level 1 auf 46×46 mit geräumigen Häusern (D64, Teil 2) (2026-10-06)

### Geändert
- **Level 1 ist 46×46 groß.** Das Gelände der Basiskarte ist aus der 36er-Karte hochskaliert, die 16 Varianten würfelt `gen_variants.py` jetzt auf 46×46 (Biom-Startpunkte, Fluss, Brücken und Wege skaliert, das Rauschen wiederholt sich nahtlos über den Rand).
- **Beide Häuser haben vier große Räume** (15×11 statt 11×9, Räume innen mindestens 6×5 bzw. 6×3). Jede Tür hat Platz für ihr Blatt. Das Gegnerhaus ist gespiegelt und hat eine Tür nach Westen und eine nach Süden. Der Blumengarten ist so breit wie das Haus.
- Beide Häuser haben dieselbe Ausstattung (Kessel, Apfel, Schwert, Schild, Schriftrolle, Truhe).
- Portal bei (33,3), Zauberer starten bei (6,6) und (40,30).
- **Mehr Truhen, Schlüssel und Funde** auf Karten über 36×36, im Verhältnis der Fläche; Wildtiere wie bisher.
- Bilder: `docs/design/mockups/level1-46*.png`.

### Intern
- Selftests auf die neuen Koordinaten umgestellt; der D36-Hochgras-Test setzt sein Gelände selbst. `loc_host --map-file` liest Karten bis 16 KB.
- RAM-Reserve 97 KB.

## [Unreleased] – Motor für 46×46-Karten (D64, Teil 1) (2026-10-06)

### Geändert
- **Karten bis 46×46** (`MAP_MAX_W/H`). Inhalt noch unverändert; Level 1 folgt in Teil 2.
- Speicherpuffer 8 → 12 KB, Spielstand v8 (alte Stände werden abgelehnt).
- Die Gesamtkarte (`m`) zeichnet große Karten mit 4 px je Feld, damit sie ins Fenster passt.

### Intern
- RAM-Reserve 129 → 100 KB (vor allem der statische Sicht-Cache). Check `d64`: volle 46×46-Welt mit 32 Einheiten, Wrap, Sicht, Speichern/Laden.
- Merkposten: `gen/maps.c` (die einkompilierten Karten, ~30 KB) braucht nur der Selftest, landet aber auch in `loc.bin`.

## [Unreleased] – eZ80-Selftest prüft wieder (2026-10-06)

### Behoben
- **Der eZ80-Selftest war seit #142 blind:** `test.py` gab dem Emulator eine geschlossene Eingabe; bei EOF beendet sich der CLI-Emulator mit Code 0, bevor das große `loctest.bin` überhaupt anlief, und das galt als PASS. Jetzt bleibt die Eingabe offen, und PASS zählt nur mit der Emulator-Meldung „shutdown triggered by writing 0x0“ (Fehlerzahl 0 über Port 0). Nachgeprüft: `main` besteht auf dem eZ80 wirklich.

## [Unreleased] – Defensiver KI-Zauberer (D62) (2026-10-06)

### Geändert
- **Der gegnerische Zauberer bleibt zunächst zu Hause:** Er beschwört bis zu 5 Kreaturen (ein Viertel Mana bleibt Reserve), plündert sein Haus und geht erst raus, wenn höchstens eine Kreatur übrig ist oder ihn ein seltener Wutanfall packt. Zum offenen Portal geht er immer.
- **Behoben:** Er lief schon in Runde 1 zum Portal-*Standort* und damit aus dem Haus.
- **Seine Kreaturen:** Zwei bewachen das Haus, die übrigen ziehen zum Haus des Spielers und sammeln Schätze. Wer Waffen tragen kann, nimmt Schwert oder Schild aus dem Haus. Schriftrollen nimmt der Zauberer.
- KI-Figuren finden Wege durch Türen (Breitensuche im 21×21-Fenster) statt an Wänden hängen zu bleiben.
- Level 1: Das Gegnerhaus hat jetzt Schwert, Schild und Schriftrolle wie das eigene.

### Intern
- `Game.home_x/y`, `rage`, `rage_round`; Spielstand v7. Checks `d62`.

## [Unreleased] – Selftest als eigenes Programm (2026-10-06)

### Geändert
- **Der Core-Selftest ist ein eigenes Agon-Programm `loctest.bin`** (`tests/selftest.c`, `tests/loctest_main.c`, gebaut in `build/loctest/`). `loc --selftest` entfällt; auf der Karte heißt es jetzt `loctest`. Host-Test unverändert (`loc_host --selftest`).
- `loc.bin` 370 → 241 KB, die RAM-Reserve für Heap und Stack steigt von 2,3 auf ~137 KB (QUIRK S6). Mit D59–D62 war sie fast aufgebraucht.
- `tools/build.py` prüft die Reserve (`.bss`-Ende in `bin/loc.map`) und bricht unter 16 KB ab.

## [Unreleased] – Türblätter (D61) (2026-10-06)

### Geändert
- **Offene Türen schwenken ihr Blatt auf das Feld neben dem Durchgang** und blockieren es: in den Raum, sonst nach außen. Ohne Platz klemmt die Tür. Schließen klappt das Blatt zurück, Tore im Zaun bleiben flach.
- **Der offene Rahmen ist innen dunkel.** Vorher schienen die Dielen durch, und die offene Tür sah aus wie eine geschlossene.
- Level 1: In beiden Häusern sind Kerze, Regal und Kommoden gerückt, damit jede Tür Platz für ihr Blatt hat.

### Intern
- `FE_LEAF_N/E/S/W`, `world_leaf_spot`, `world_door_jammed`, `world_is_gate` (aus view.c). 6 neue Kacheln (`tools/art/make_door_leaf.py`), View-Hash neu. Checks `d61`. Vorschau: `docs/design/mockups/door-leaf-d61.png`.

## [Unreleased] – Panel: Tastenzeile (D63) (2026-10-06)

### Geändert
- **Balken-Labels mit zwei Buchstaben in einer Zeile** (`AP AU LE KA VE MA`), die Balken sind 8 px länger.
- **Neue Zeile darunter mit den Tasten, die gerade wirken** (z. B. `g c`: Aufheben, Zaubern). Sie wird nur neu berechnet, wenn sich an der Figur oder der Karte etwas ändert.

## [Unreleased] – Handeln vom Reittier aus (D60) (2026-10-06)

### Neu
- **Reiter handeln aus dem Sattel:** Der Zauberer zaubert vom Reittier aus, Reiter öffnen Türen und Truhen und heben auf. Es zählt die Art des Reiters, nicht die des Reittiers.

### Behoben
- **Aufsitzen kostete das Mana des Reiters, Absteigen heilte ihn voll** und gab nur den ersten Gegenstand zurück. Leben, Ausdauer, Werte, Mana und das ganze Gepäck bleiben jetzt erhalten.
- **Stirbt das Reittier, wird der Reiter abgeworfen** statt mit ihm zu sterben.
- Ein berittener Zauberer zählt als anwesend (Rundenablauf, Endabrechnung, Portal, Panel „Stufe“).

### Intern
- `Unit` trägt die Werte des Reiters (7 Byte), Spielstand v6 (alte Stände werden abgelehnt). Checks `d60`.

## [Unreleased] – Friedliche Tiere, Fangen, RAM-Arena (D59) (2026-10-06)

### Geändert
- **Grasende Wildtiere schlagen nicht mehr im Vorbeigehen zu:** Den freien Schlag beim Wegziehen bekommt ein Wildtier nur noch, wenn es einen Groll hegt, gerade angreift oder (Revier-Tiere) man in seinem Revier steht. Kreaturen der Zauberer und Monster schlagen wie bisher.
- **Werfen auf eigene Figuren:** Sie fangen den Gegenstand (auch Phiolen und die Bombe) und haben ihn im Gepäck, statt verletzt zu werden. Ohne Platz fällt er ihnen vor die Füße.

### Intern
- RAM: Speichern/Laden und die Hilfe-, Zauber- und Lexikontexte teilen sich eine Arena (−15 KB, Reserve ~24 KB, QUIRK S6). Der eZ80-Selftest war mit wenigen neuen Checks wieder an „water animates“ gescheitert.
- Checks `D59` (Gorilla, Bär im/außerhalb des Reviers, Monster, Apfel und Bombe gefangen).

## [Unreleased] – Menüoption „Zufällige Karte" (D58) (2026-10-06)

### Neu
- **Hauptmenü hat einen Eintrag `Zufaellige Karte`:** startet sofort eine der 16 generierten Level-1-Varianten (D57) mit dem Bücher-Set von Szenario 1. Die übrigen Menüpunkte rücken um eine Position nach unten.

## [Unreleased] – Level 1 in 16 Geländevarianten (D57) (2026-10-06)

### Neu
- **Jedes neue Spiel von Level 1 hat anderes Gelände:** `tools/gen_variants.py` erzeugt beim Bauen 16 Varianten (`build/maps/mcl_v00..15.map`). Fluss, Brücken, Biome (Zauberwald und Totenwald tauschen die Seiten), Wege, Bäume und Felsen werden gewürfelt; Häuser, Garten, Zauberer, Objekte und Portal kommen unverändert aus der handgemachten Karte. Jede Variante wird geprüft (Wege zusammenhängend, keine Sackgassen außer am Portal, alles erreichbar) und sonst neu gewürfelt. Das Spiel wählt eine zufällig, nie zweimal dieselbe hintereinander; ohne Varianten auf der SD gilt die Basiskarte.
- `loc_host --map-file` und `map_preview.py file.map` zeigen eine `.map`-Datei.

## [Unreleased] – Keine Starts auf Brücken (2026-10-06)

### Behoben
- **Krokodile (und alles andere) konnten auf Brücken starten:** Brücken liegen neben Wasser, der Ufer-Zuschlag der Krokodile griff dort. `open_field` schließt Brücken jetzt aus (Tiere, Truhen, Funde, Herden-Eintritt). Check `d55` prüft es über 60 Seeds.

## [Unreleased] – Echte Sichtlinien durch Fenster und Türen (D56) (2026-10-06)

### Geändert
- **Dächer sind wieder nur Anzeige (ersetzt D44):** Sie blockieren keine Sicht mehr. Das Dach öffnet sich auf den Feldern, die die aktive Figur sieht (`sight_look`, derselbe Schattenwurf wie die Sichtregel). Durch Fenster und offene Türen blickt man als Keil in den Raum; was nicht gesehen wird, ist wieder überdacht. Fenster bleiben vom Dach frei. Das Dachloch hinter den Fenstern aus #132 entfällt, die Karte ist entsprechend angepasst.
- KI, Zauber und Fernwaffen zielen durch Fenster und offene Türen entlang der echten Linie.
- RAM: eine Blockier-Bitmap statt zwei (−360 B), 180 B Cache für die Figur-Sicht.
- `map_preview.py --viewer X Y` zeigt, was eine Figur sieht (Dach geöffnet); `docs/design/mockups/window-sightline.png`.
- Checks `d56`; der D44-Test (Dach versteckt Inneres) kehrt sich um.

## [Unreleased] – Level 1: Nachtkarte, Entwurf (D54) (2026-10-06)

### Geändert
- Selftests ziehen auf offenes Gelände der neuen Karte um (Löwe d35, Hochgras d36, Bogen, Dach d46), der Dach-Test d46 nutzt eine eigene Wand.
- GDD D54: Kartengröße 36×36 bleibt, alle Szenarien nachts, Fenster (zwei Felder Einblick), Brücken, Zaun und Tor, Biome mit natürlichen Grenzen. 
### Neu
- **Wege hören nirgends mehr auf:** Der Weg führt gerade durch den Zauberwald, weiter durch den Südwest-Wald zur Brücke; der Pfad durch den Totenwald läuft über den Kartenrand zurück zur Westwiese; die Nord-Abzweigung zur Brücke liegt außerhalb des Zauns. Wegnubsen sind entfernt; einziges Wegende ist das Portal.
- **Wildtiere und Funde nach Biom (D55):** neue Tabelle `data/habitats.csv` (generiert nach `HABITAT[][]`): Krokodile im Sumpf und am Ufer, Bären im Wald, Spinnen und Fledermäuse im Totenwald, Löwen auf Wiesen, Einhörner im Zauberwald, Greife im Geröll. Funde passen zum Wald (Zauberwald: Feenflügel, Zauberpilz; Totenwald: Schwefel, Nitro, Runenstein). Checks `d55` über 60 Seeds.
- **Erkennbare Wälder:** Totenwald (früher Schattenwald) mit kahlen Baumleichen, Stümpfen und Knochen, Zauberwald mit verdrehten violetten Stämmen, türkis leuchtenden Kronen und Glühpilzen; je 3 Varianten. Der Weg im Südwesten führt durch den Wald.
- **Level 1 neu gezeichnet (Entwurf, `data/maps/many_coloured_land.txt`):** Fluss mit Windungen und drei Brücken, organische Biome (Wald, Magic Wood, Schattenwald, Sumpf mit Pilzen am Fluss, Hochgras, Geröll) statt Rechtecke, verschlungene Wege, Portal-Lichtung mit Steinen. **Haus 1** hat vier Räume mit Innentüren und sechs Fenstern, davor ein eingezäunter Blumengarten mit Tor; **Haus 2** hat zwei Räume und drei Fenster. Alle Koordinaten der alten Häuser (Zauberer, Kessel, Truhen, Türen) bleiben. Vorschau: `docs/design/mockups/level1-night-draft.png`.
- **Fenster-Einblick:** in #132 zunächst als Dachloch zwei Felder tief gelöst, ab D56 echte Sichtlinie (siehe oben).
- **Neue Elemente (D54), 31 Kacheln (~18 KB VDP-RAM, `tools/art/make_night_set.py`):** `FE_WINDOW` (beleuchtetes Fenster in der Wand, blockiert Bewegung, nicht die Sicht), `FE_FENCE` (Zaun mit 16er-Auto-Tile) mit **Tor** (eine Tür zwischen Zaunpfosten, öffnet/schließt/schließt ab wie jede Tür), `FL_BRIDGE` (begehbar wie Gras, kein Ertrinken, Ausrichtung nach dem Wasser), Blumenbeet (`DE_FLOWERS`, 3 Varianten), leuchtende Pilze (`DE_MUSHROOMS`, 2 Frames) und aufsteigende Blasen im Sumpf (automatisch auf einigen Sumpffeldern). Kartenzeichen: `W` Fenster, `F` Zaun, `b` Brücke, Dekor `f` Blumen, `o` Pilze. `HOUSE_VIEW_HASH` neu (Kachel-IDs verschoben), Checks `d54`.
- **Nacht-Kacheln (erster Wurf):** `tools/art/night.py` färbt Außenkacheln beim Bauen um (schwarzer Grund mit grünen Tupfen wie im Amiga-Original, graue Wege, dunkler Sumpf, dunklere Bäume); die PNGs bleiben die Tag-Quelle. `map_preview.py` läuft auch ohne WSL.

## [Unreleased] – Gelände-Politur 3: Kreaturen-Idle (D53) (2026-10-06)

### Neu
- **Kreaturen bewegen sich hin und wieder:** Fledermaus, Harpyie, Pixie, Greif, Pegasus und die Drachen schlagen mit den Flügeln, Geist und Spectre wiegen sich (gezeichnete Frames, 100 neue Kacheln), alle anderen Kreaturen heben sich kurz um 1–2 Pixel. Jede Figur ist ein Viertel der Zeit aktiv, gegeneinander versetzt. Reittiere mit Reiter, Watende und Figuren unter Dächern bleiben ruhig.
- `tools/art/creature_frames.py` erzeugt die Frames aus den Basis-PNGs; `gen_data.py` erzeugt `CREATURE_FRAME`; `build_tiles.py` färbt die Frames wie die Basis ein.
- Core: `view_bob()` (Lift außerhalb von `FieldLayers`), `view_set_idle()` (im Core aus, das Spiel schaltet ein); `view_animate()` berechnet Felder mit Figuren neu und reicht den Überhang nach oben durch. Neue Checks `d53`.

## [Unreleased] – Gelände-Politur 2: Übergänge (D52) (2026-10-06)

### Neu
- **Geländeübergänge:** Flussufer mit Sandlippe und Schaum, Wegränder mit Grasbüscheln, Halmfransen zwischen Wiese und Hochgras (57 neue Kacheln, `tools/art/make_terrain.py`, Auswahl per Nachbarmaske in `view.c`).
- **Kartenvorschau:** `uv run tools/art/map_preview.py [Karte] [x0 y0 w h]` zeichnet eine ganze Karte nach `build/map_preview.png` (neuer Host-Schalter `loc_host --map-layers`).

### Behoben
- **Feuer verbrannte Schriftrollen u. Ä. nicht mehr zuverlässig:** `object_burns` kürzte die 16-Bit-Kachel-ID auf 8 Bit; seit IDs über 255 liegen, passte keine mehr (Selftest `m4d` schlug an).

### Geändert
- **−18 KB RAM:** der statische Sicht-Cache hält nur noch die statischen Ebenen (`StaticField`, ≤ 8) statt einer vollen `FieldLayers` je Feld. Heap und Stack haben nun ~30 KB statt ~15 KB (QUIRKS S6).

## [Unreleased] – Gelände-Politur 1: Variation, fließendes Wasser, Seerosen (D51) (2026-10-06)

### Neu
- **Abwechslungsreiche Flächen:** Gras (3 Varianten mit Blümchen, Halmen, Steinchen), Weg (2), Hochgras und Sumpf (je 1), per Positions-Hash gewählt (`tools/art/make_terrain.py`).
- **Fließendes Wasser:** 4 Frames statt 2, Wellenzeilen mit unterschiedlicher Geschwindigkeit, nahtlos über Feldgrenzen; zwei Muster-Varianten.
- **Seerosen** auf Wasserfeldern (2 Frames).
- Animationsphase mit 2 Bit; Tabelle `ANIM_F` mit 4 Frames je Gruppe. `HOUSE_VIEW_HASH` neu (Kachel-IDs verschoben), neue Checks `d51`.

## [Unreleased] – Dach-Anzeige (D46) (2026-10-05)

### Behoben
- **Dächer poppen nicht mehr im Haus.** Steht die aktive Figur unter einem Dach, wird gar kein Dach gezeichnet. Mauern und Türen mit überdachtem Nachbarn tragen das Dach (die per-Feld-Hebung greift dort nie, das Dach endet also nicht mehr vor der Außenwand), und das „verdeckt“-Raster liegt nie über Dachfeldern. Grund: Shadowcasting und Bresenham-Strahl fallen an Wandkanten auseinander. Entscheidung D46 im GDD, fünf neue Checks (`d46`) im Selftest.

## [Unreleased] – Türen und Truhen (C1/C2, D47) (2026-10-05)

### Neu
- **Türen schließen und abschließen:** `a` + Richtung schließt offene Türen, mit einem Schlüssel auch ab- und aufschließen. Abgeschlossene Türen sperren Weg und Sicht; Gegner ohne Schlüssel schlagen sie kaputt.
- **Truhen mit und ohne Schloss:** neues Feld `FE_CHEST_FREE` (Karte `x`) öffnet ohne Schlüssel; zufällig verteilte Truhen sind je zur Hälfte frei oder verschlossen.

## [Unreleased] – Ertrinken (C5, D48) (2026-10-05)

### Neu
- **Wasser kostet Kraft:** Wer eine Runde in tiefem Wasser endet, verliert die halbe Ausdauer; bei 0 schwindet pro Runde ein Fünftel der Con, bei 0 ertrinkt die Figur. Flieger und Wasserwesen sind ausgenommen.

## [Unreleased] – Waten (C4, D49) (2026-10-05)

### Neu
- **Figuren im tiefen Wasser stehen bis zur Hüfte darin:** 8 Pixel tiefer gezeichnet, die Beine verschwinden unter dem Feld darunter. Im Emulator geprüft.

## [Unreleased] – Überlagern (C3, D50) (2026-10-05)

### Neu
- **Eigene Figuren dürfen auf demselben Feld stehen** (Boden und Luft getrennt), wie im Original. Gezeichnet wird die aktive Einheit. Gegner und wilde Tiere blockieren wie bisher.

## [Unreleased] – Messen und Fernsteuern vom PC (2026-10-05)

### Gemessen
Vollständiger `loc --bench` auf dem echten Agon Light 2, Selftest grün: **Sicht 298 → 12 ms (25×)**, **KI-Phase 160 → 20 ms (8×)**, Komposition 90 → 38 ms, voller Redraw 152 → 100 ms. Ein eigener Schritt kostete vorher 0,4–0,5 s und liegt jetzt unter 60 ms. Werte in `docs/AGON-QUIRKS.md`.

### Behoben
- **Esc wirkt jetzt auch über die USB-Konsole (Quirk H5).** Dort kommen Zeichen ohne VKey an; das Spiel prüfte nur `vkey == VK_ESC`. `input_poll()` normalisiert das an einer Stelle. Damit lässt sich eine Sitzung komplett vom PC fahren — einschließlich Beenden, und erst das schreibt `loc.log`.
- **Der Benchmark gab die Hälfte seiner Zahlen nicht aus.** `compose`, `full` und `candles` riefen `render_message` und waren über USB zu sehen; Cursor, Sicht, Flächen-Ticks und KI-Phase nur `log_line` — und das Log erreicht die Karte erst beim Schließen, das ohne Esc nicht ging. Die Zeilen werden jetzt gesammelt und am Ende des Laufs auf die Konsole gegeben, also außerhalb jedes gemessenen Abschnitts.

## [Unreleased] – KI: Sicht, sichtbare Phasen, erkennbare Ziele (2026-10-05)

### Geändert
- **Phasenbildschirm nur noch für Unsichtbares (D45, C8).** Steht beim Beginn einer fremden Phase etwas dieser Seite in deiner Sicht, bleibt die Karte stehen und der Zug wird offen gespielt. D38 bleibt im Kern erhalten — verdeckt wird nur, was du ohnehin nicht sehen könntest.
- **Freier Schlag ist jetzt für beide Seiten an die Sicht gebunden.** Ohne Sichtkarte — die KI führt pro Zug keine — entscheidet die direkte Sichtlinie. Mit D44 zählt die das Dach mit, also schlägt niemand mehr aus einem geschlossenen Haus nach einem Vorbeigehenden.
- **Die KI geht zum Portal, bevor sie sammelt.** Steht der Ausgang offen, gehen die AP dorthin statt zum nächsten Schatz.

### Behoben
- **Der KI-Zauberer kroch zu Schätzen.** Die Gehschleife brach beim *ersten erfolgreichen* Schritt ab — ein Feld pro Runde, bei 40 AP (zehn Schritten). Die Portalschleife machte es immer richtig. Das ist der Hauptgrund, warum in seinem Verhalten keine Logik zu erkennen war; die Logik (Nahkampf → Bolt → Truhe → Schatz → Beschwören → Portal) war längst da.

### Neu
- **Indikator für den gegnerischen Zauberer (C9):** Die Gesamtkarte markiert gelb, wo du ihn zuletzt **wirklich gesehen** hast, mit der Runde dazu. Verdeckte Bewegung bleibt unangetastet — angezeigt wird nur eigenes Wissen.

## [Unreleased] – Dächer blockieren die Sicht (D44, 2026-10-05)

Entscheidungsvorlage: `docs/design/VORLAGE-daecher-und-sicht.md`.

### Geändert
- **Ein überdachtes Feld ist von außen nicht mehr einsehbar.** Wer selbst darunter steht, sieht normal; durch eine offene Tür reicht die Sichtlinie hinein. Die Blockierkarte gibt es dafür in zwei Fassungen (mit und ohne Dächer), gewählt nach dem Standort des Betrachters.
- **Das Dach wird zuletzt gezeichnet statt mit den statischen Ebenen.** Vorher lag es unter den Einheiten; da der Renderer von unten nach oben zeichnet, stand eine Figur unter geschlossenem Dach optisch *darauf*. Das fiel erst mit D41 auf, weil das Dach seither meist geschlossen ist.

### Behoben
- **Zwei Sichtregeln im Code liefen auseinander:** `world_blocks_sight()` zählte das Dach seit M2d mit, die Bitmap aus `world_sight_byte()` — über die `sight.c` tatsächlich läuft — nicht. Die Absicht ging beim Bitmap-Umbau (ADR 0009, Stufe 3) verloren und ist jetzt wiederhergestellt.

## [Unreleased] – Playtest-Runde C (Teil 1): Waffen und Geister (2026-10-05)

### Geändert
- **Waffen beeinflussen nur noch den Schaden (D42).** Ohne Waffe traf man bisher sehr schlecht, weil der Waffenbonus in den Kampfwert und damit in die Trefferchance floss — die Axt hob den Zauberer gegen einen Goblin von 55 % auf den 90-%-Deckel. Die Zauberwaffe verdoppelt jetzt die Schadenswürfel statt eines Kampfbonus, sonst wäre das Flag wirkungslos geworden.
- **Geist und Gespenst gehen durch Wände (D43).** Neues Flag `CF_PHASE` aus `creatures.csv`. Einheiten halten sie weiterhin auf.

## [Unreleased] – Playtest-Runde B: Bedienung und Anzeige (2026-10-05)

### Geändert
- **Balken zeigen jetzt Stärke *und* Zustand (B5):** Die Höhe eines Balkens misst den Maximalwert der Figur am **besten Maximalwert unter den eigenen Figuren**. Wer halb so viele Maximal-AP hat wie der Beste, bekommt einen halb hohen Balken; Verbrauch leert ihn, verkleinert ihn aber nicht. Buffs sitzen als pulsierendes Zusatzkontingent obendrauf. Kampf und Verteidigung messen sich nicht mehr an einer festen 50, sondern an den eigenen Besten.
- **Buchstaben statt Symbolen unter den Balken (B6):** `AP`, `AUS`, `LEB`, `KAM`, `VER`, `MAN`, senkrecht gestapelt. Dafür sind die Balken 24 px kürzer — „Am Boden:" beginnt in Textzeile 23 und kann nicht weichen.
- **Feindliche Einheiten bekommen einen dünnen roten Rahmen (B4).** Wildtiere bleiben unmarkiert, sie gehören niemandem. Die Rahmen werden nach dem gebündelten Kachelstrom gezeichnet, damit Rechtecke den Bitmap-Puffer nicht durchbrechen.
- **`Esc` beendet nicht mehr sofort (B3):** Rückfrage mit `J` speichern und beenden, `B` ohne speichern, `N` weiter.
- **`m` schließt die Gesamtkarte wieder (B1).**

### Hinweis
- **B2 erledigt sich mit dem A2-Fix:** Die ganze 36×36-Welt passt mit 177×191 px auf den Schirm, es gibt nichts zu verschieben. Die Pfeiltasten richten in der Karte nur keinen Schaden mehr an.

## [Unreleased] – Playtest-Runde A: Magie, Dächer, freier Schlag (2026-10-05)

Rückmeldungen und Einordnung: `docs/PLAYTEST-2026-10-05.md`.

### Geändert
- **Magie ignoriert Verteidigung (D40):** Schadenszauber würfeln allein gegen die Magieresistenz (`100 − MR`, 5–95 %). Vorher stand der Nahkampfwert des Zauberers gegen die physische Verteidigung — gegen eine Riesenspinne ergab das bei *jedem* Wurf den 10-%-Boden, jetzt 45 %. `magic_res` stand bis dahin in der Kreaturtabelle, wurde auf die Einheit übernommen und war im Zauberer-Designer **mit Punkten steigerbar — ohne jede Wirkung**.
- **Dächer heben nur für die aktive Figur (D41):** entlang ihrer Sichtlinie statt per Flutfüllung über das ganze Gebäude. Von außen sieht man nicht mehr hinein, durch eine offene Tür so weit, wie die Sichtlinie reicht. Spart 3,9 KB eZ80-RAM.
- **Freier Schlag nur von Gesehenen:** ein Gegner, den die weglaufende Seite nicht sieht, bekommt keinen Gelegenheitsschlag mehr. Für die KI noch unverändert (sie führt keine eigene Sichtkarte je Zug) — die Asymmetrie ist bewusst und offen.

### Behoben
- **Aufsteigen meldete Unsinn:** ein zweites „fliegen“ fiel durch die `else`-Kette auf „Da fliegt schon einer.“ Jetzt „Du fliegst schon.“, und mit Flugtrank nicht mehr „Diese Kreatur fliegt nicht.“
- **Laufen unter offener Gesamtkarte:** Pfeil- und Diagonaltasten wurden vor dem Overlay-Zweig abgearbeitet, die Figur lief weiter und das Detailfenster zeichnete sich über die Karte. Der Schritt entfällt jetzt, die Taste wird aber nicht verschluckt (Quirk K6).

## [Unreleased] – Plattform-Audit: Sicht, LOS und Zeichenstrom (2026-10-05)

Hintergrund und Begründung: `docs/AUDIT-PLATTFORM.md`.

### Geändert
- **Sicht per Shadowcasting (D39, ADR 0009):** acht Oktanten statt eines Bresenham-Strahls je Zielfeld, O(r²) statt O(r³). Reichweite, Chebyshev-Distanz, Endpunkt-Regel und Flieger bleiben unverändert; in verwinkeltem Gelände deckt die Sicht einzelne Felder anders auf. Steigungen als exakte Brüche, ohne Division und ohne Gleitkommazahlen.
- **Blocking-Bitmaps zwischengespeichert:** jede LOS-Abfrage baute die Bitmap der ganzen Karte neu (1440 Durchläufe), und die KI fragt eine pro Kandidat in einer Schleife. Beide Bitmaps hängen jetzt an `World.generation`. Die Zauber-Variante (D36) hat ihre eigene und scannt nicht mehr pro Aufruf die ganze Karte.
- **Zeichenstrom gebündelt:** `render_fields()` sammelt die Bitmap-Befehle eines Bildes und schickt sie über `mos_puts` (MOS RST 18h mit Länge) statt Byte für Byte durch die libagon. Wiederholte Kacheln sparen zusätzlich ihr `select_bitmap`.
- **Animationspartner als Tabelle** statt Linearsuche — `view_update()` rief die Suche für jede Ebene jedes Feldes auf (~13 600 Vergleiche je Komposition).
- **Panel- und Menütext** über `mos_putstring` statt `printf("%s")`. Das Binary wird dadurch kleiner, nicht größer: 308 400 → 307 912 Byte.

### Behoben
- **Ein Tür-Test im Selftest hatte still seine Aussage verloren:** er schrieb `feature[][]` direkt, ohne `world_map_changed()`. Mit dem neuen Cache fiel das auf. Alle 14 direkten Geländeschreibzugriffe im Selftest ziehen die Invariante jetzt nach, und `world.h` sagt, dass sie Pflicht ist.

### Offen
- Messwerte auf echter Hardware stehen noch aus (das Gerät lief beim Playtest). Erwartet: Sicht deutlich unter 100 ms, KI-Phase überwiegend erledigt.

## [Unreleased] – Reiter verschwinden nicht mehr (2026-10-05)

### Behoben
- **Reiten:** Die Kampfreaktion (D29) teilte sich ein Statusbit mit „wird geritten“. Der Rundenwechsel warf Reiter dadurch aus der Welt, und der Zauberer galt als tot. Die Reaktion hat jetzt ein eigenes Feld.
- Spielstand-Format v5 (alte Spielstände werden abgelehnt).

## [Unreleased] – Core ohne Hardware-Division schneller (aus dem Mega-Drive-Repo, 2026-10-05)

### Geändert
- **`world_wrap`** rechnet im Normalfall (höchstens eine Kartengröße daneben) ohne Modulo; nur weit entfernte Koordinaten zahlen noch die Division. eZ80 und 68000 haben keine schnelle 32-Bit-Division.
- **`roof_refresh`** ohne Division und ohne 32-Bit-Multiplikation in der Schleife (Warteschlange als `y << 8 | x`, `* 31` als Schieben und Abziehen).
- **`VIEW_STATIC_CACHE`** (Standard 1, unverändert für den Agon) kann den 39-KB-Ebenen-Cache abschalten; die Mega-Drive-Fassung (`cadextcp/lords-of-chaos-md`, 64 KB RAM) setzt 0.
- `lexicon_import`: Compiler-Warnung zu `1ul << OBJ_COUNT` bei 32-Bit-`long` beseitigt (toter Zweig, kein Laufzeitunterschied).
- Ergebnisse unverändert: Selftest, `HOUSE_VIEW_HASH` und der Benchmark-Hash des MD-Repos sind gleich. Gemessen auf dem 68000: Ansicht 580 → 164 ms, KI-Zug 253 → 179 ms. Auf dem Agon nicht gemessen (GUI-Emulator im CI ohne SDL3).

## [Unreleased] – Phasenbildschirm, aufgescheuchte Tiere (D37/D38, 2026-10-04)

### Geändert
- **Drei Phasen wie im Original:** Während der KI- und der Neutralen-Phase zeigt ein Phasenbildschirm (Rankenrahmen, wer am Zug ist, Runde, Siegpunkte) statt der Karte; man hört nur Schritte, Kampf und Zauber.
- **Aufgescheuchte Tiere:** Kampf und Zauber in der Nähe (4 Felder) scheuchen friedliche Tiere und Herden auf – pro Herde ein Wurf am Leittier: 20 % Angriff auf den Verursacher, sonst Flucht; alle Tiere der Herde gleich.
- **Elefanten trampeln** in Panik durch kleinere Einheiten (2w6) und walzen hohes Gras platt.

## [Unreleased] – Aufheben mit Auswahl, Zauber durch hohes Gras (2026-10-04)

### Geändert
- **`g` mit Auswahl:** Liegen mehrere Gegenstände auf dem eigenen Feld oder auf Nachbarfeldern, fragt ein Menü, welcher (Buchstabe) oder alle (Leertaste). Ein einzelner Gegenstand auf dem eigenen Feld wird wie bisher direkt genommen. Nachbarfelder sind jetzt erreichbar.
- **Zauber durch hohes Gras (D36):** Hohes Gras versperrt die Sicht, aber nicht mehr den Zauber.

## [Unreleased] – Zufällige Welt, Wildtiere und Herden (D35, 2026-10-04)

### Geändert
- **Jede Partie ist anders:** Zufallsstartwert aus der Uhr.
- **Der KI-Zauberer startet allein** und beschwört seine Kreaturen selbst – kein Goblin mehr vor der Tür.
- **Wildtiere:** 5–8 an Zufallsorten. Friedliche streifen umher und wehren sich nur gegen Angreifer; territoriale verteidigen ihr Revier (3 Felder).
- **Herden** (Elefanten, Einhörner, Pegasi) ziehen ab Runde 4 gelegentlich über die Karte.
- **Beute zufällig:** 5–7 Truhen (Schätze, Waffen, Tränke), 2 Truhenschlüssel, 6–9 lose Fundstücke passend zum Boden. Fest bleibt nur die Hausausstattung.
- Spielstand-Format v4 (alte Spielstände werden abgelehnt).

## [Unreleased] – Rundenende ohne Umwege (2026-10-04)

### Geändert
- **Shift+E beendet die Runde sofort** (keine Rückfrage mehr).
- **Leertaste beendet die Runde**, wenn alle Einheiten fertig sind.

### Behoben
- Panel: Nach dem Wechsel von einer Einheit mit Waffe blieben Reste des Waffennamens hinter „Hand: -“ stehen.

## [Unreleased] – Beschwörungen: Stufe = Stärke (D34, 2026-10-04)

### Geändert
- **Ein Wurf, eine Kreatur:** Die Stufe einer Beschwörung bestimmt jetzt die Stärke (+15 % Kampf/Verteidigung/Konstitution pro Stufe über 1, Deckel 8) statt der Anzahl.
- **Beschwörungen verbrauchen sich nicht** und kosten festes Mana (Stufe-1-Preis) plus 10 AP. Andere Zauber behalten Ladungen.
- Zauberliste und Designer zeigen bei Kreaturen „Stf“ (Stufe) statt „Anz“; die KI rechnet mit dem neuen Manapreis.

## [Unreleased] – Sprite-Effekte und gleitende Schritte (2026-10-04)

### Hinzugefügt
- **Fliegende Projektile** als VDP-Sprites: Bolt-Kugel, Blitz mit Funkenschweif, Pfeil (8 Richtungen), rotierende Wurfwaffe, Bombenphiole – auch bei KI-Angriffen. Neues Core-Ereignis `EV_PROJECTILE` (Wurf-Schleife umgebaut, Würfelfolge unverändert).
- **Zauber-Effekte am Ziel:** Beschwörungswirbel, Teleport-Funken, Schildkuppel, Fluchschädel, Trankblasen, Funkeln.
- **Aufsteigende Schadenszahlen** als Sprites (Krit mit „!“) statt Text über der Karte.
- **Gleitende Schritte** der eigenen Einheiten (80 ms, im Setup mit G abschaltbar; Reiter springen weiter).
- `--fxdemo` zeigt alle Sprite-Effekte in Schleife.

### Behoben
- **Einheit lief nach kurzem Tippen von selbst bis an die Wand:** Die Gleit-/Effektpause verschluckte das Loslassen der Pfeiltaste. Loslassen wird jetzt aufgehoben und an die Akkord-Logik gegeben (QUIRK K6).
- Bogen-/Wurfgeräusch kommt jetzt aus dem Ereignis – auch für KI-Schüsse, ohne Doppelung.
- **Stimmbare Samples** nutzen Flag 16 (agondev-Konstante 8 ist „Abtastrate folgt“ und ließ Bytes als Text erscheinen, QUIRK A9).
- `HOUSE_VIEW_HASH` 0x632E5581 (neue Effekt-Kacheln verschieben die IDs).

## [Unreleased] – Neues Titelbild, Zierschrift, Menü- und Endbilder, eigene Tränke (2026-10-04)

### Hinzugefügt
- **Titelbild neu:** Schlachtgetümmel im Stil der 8-Bit-Ladebilder, eigene Komposition aus den vergrößerten Spielkreaturen (Scale2x/Scale3x), Blitz, Magiewirbel, Gold-Schriftzug.
- **Hauptmenü mit Bild:** Titelbild als Hintergrund, Menü im Rahmen.
- **Endbildschirm mit Bild:** Sieg (Portal, Schätze) bzw. Niederlage (Grab); Level-up-Klang.
- **Eigene Zierschrift 8×16** für Überschriften (Menü, Designer, Setup, Hilfe, Lexikon, Ende, Overlays), mit Schatten; Fallback Systemschrift.
- **Eigene Kachel für jeden Trank** (Flüssigkeitsfarbe + Symbol): Stärke, Schutz, Unsichtbarkeit, Schnelligkeit, Fliegen, Heilung.

### Behoben
- **Hilfeseiten fehlten:** `keys.hlp` (3,2 KB) passte nicht mehr in den 3-KB-Puffer und wurde als ungültig verworfen. Puffer 4 KB, `gen_help.py` prüft jetzt die Größen.
- **Lexikon- und Zaubertexte fehlten:** Der Code suchte `lexicon.hlp`/`spells.hlp`, die Dateien heißen `*_de.hlp`.
- **Lexikon:** Geteilte Phiolen-Kachel markierte immer den ersten Trank als entdeckt.
- `HOUSE_VIEW_HASH` 0xC1C0D595 (neue Kacheln verschieben die IDs).

## [Unreleased] – Neuer Klang: Samples, neue Musik, mehr Sounds (2026-10-04)

### Behoben
- **Titelmusik lief 10× zu langsam** (ms auf die Zentisekunden-Uhr addiert): man hörte nur einzelne Piepser im Abstand von Sekunden.
- **Mehrton-Effekte spielten nur den ersten Ton** (der VDP verwirft Noten auf belegten Kanälen, ADR 0012).

### Geändert
- **Eigene Samples** (`tools/gen_sfx.py`, synthetisiert, 16 Stück, `/loc/sfx.bin`): Treffer, Klirren, Wisch, Stöhnen, Donner, Zisch, Knarren, Truhe, Funkeln, Beschwörung, Blubbern, Krachen u. a.; Wellenform-Ersatz, falls die Datei fehlt.
- **Effekt-Sequenzer** mit Prioritäten auf zwei Kanälen; jeder Zauber klingt nach seiner Art (Bolt, Blitz, Beschwörung, Teleport, Fluch, Trank).
- **Neue Musik:** eigenes vierstimmiges Stück (Zupfsaite, Bass, Begleitung, Trommel), läuft im Menü weiter; **Sieg- und Niederlage-Jingle** auf dem Endbildschirm. Musikformat LOCM v2 mit Instrumenten, Hüllkurven und Schleife.
- **Neue Sounds:** Menü (bewegen, bestätigen, zurück), verweigerte Aktion, Essen, Trinken/Brauen, Fliegen/Reiten/Landen.
- **Setup:** Musik und Toneffekte getrennt abschaltbar (gespeichert in `settings.dat`).
- **RAM:** Selftest, Bildschirme und Tastaturtest werden mit `-Oz` übersetzt; `loc.bin` 321 → 275 KB.

### Werkzeuge
- `tools/audio_preview.py` rendert Musik und Samples als WAV nach `build/sfx/preview/` – zum Probehören ohne Agon.
- `vdptest` misst zusätzlich Kanal-Reset, Sample-Längen und Tonhöhenbereich (A6–A11).

## [Unreleased] – VDP-Spike: was der Agon kann (2026-10-04)

### Hinzugefügt
- **`vdptest [n]`** – eigenes kleines Agon-Programm (`spikes/vdptest/`, von `build.py` gebaut, liegt als `/loc/vdptest.bin` auf der SD; `run.py --vdptest [n]`): misst Audio-Warteschlange, Samples, Zusatzkanäle, eigene Schriften, Palette in MODE 8, Sprites und Doppelpuffer; Ergebnisse in `vdptest.log`. Auf der Hardware laufen lassen und vergleichen.
- **ADR 0012**: Samples, eigene Schriften und Sprite-Animation werden genutzt; Doppelpuffer nicht fürs Spielbild, Paletten/Copper verworfen (wirken in MODE 8 nicht).

### Korrigiert (Doku)
- QUIRKS A1/A5: Der VDP queued Noten **nicht** – eine Note auf belegtem Kanal wird verworfen. Daher spielten die Mehrton-Effekte bisher nur ihren ersten Ton. Neue Einträge A6 (Samples), S3–S6 (Schrift, Palette, Doppelpuffer, RAM), V11 (Sprite-Kosten).

## [Unreleased] – Keine Bildschirmreste mehr, Titelbild sichtbar (2026-10-04)

### Behoben
- **Bildschirmwechsel ohne Reste:** Nach Overlays (Zauberliste, Kontextmenü, Karte, Log) und Vollbildseiten (Hilfe, Lexikon) wird das Spielbild komplett neu aufgebaut (`game_redraw`: ganzer Schirm schwarz, Karte, Panel, gemerkte Meldungszeilen). Vorher blieben Zeichen in Panel-Lücken, Spalte 39 und den Meldungszeilen stehen.
- **Menü und Designer** löschen beim Eintritt den ganzen Schirm und überschreiben Zeilen danach in voller Breite (`render_menu_line`); zu lange Texte (>40 Zeichen, Zeilenumbruch) sind gekürzt. Shop-Detailrahmen geht jetzt über die volle Breite.
- **Cursor-Sprite** verschwindet unter Overlays und Vollbildseiten; Animation und Blinken pausieren auch im Zaubermenü.
- **Zauberliste:** volle Namen („Invisibility Pot.“ statt „Invisibility Po“), Fußzeile bleibt links vom Panel, Abstand zur Panel-Spalte.
- **Schadenszahlen** („KRIT -12“) bleiben im Kartenfenster und alle überschriebenen Felder werden neu gezeichnet.
- **Beschwörungsliste:** Die Auswahl griff in die Zauberliste (Liste wurde vor der Suche zurückgesetzt).
- **Titelbild wird angezeigt:** Der Loader las den 9-Byte-Header als 8 Byte und fügte die gestreamten Blöcke nicht zusammen (`vdp_adv_consolidate`, QUIRK S1) – das Bild war bisher nie zu sehen.
- Lexikon-Detail zeigt bei Essen „+x Kons +y Mana“ (wurde berechnet, aber nicht gezeichnet).

### Werkzeuge
- `send_keys.py`: `wait=ms` pausiert ohne Tastendruck (für Skripte über Titel und Menü).

## [Unreleased] – Zauber kaufbar, Seiten sauber getrennt (2026-10-04)

### Geändert
- **Alle Zauber sind erlernbar:** Auch Nicht-Beschwörungen haben jetzt XP-Preise — Platzhalter = je 1× Mana auf Stufe 1 (z. B. Bolzen 9, Fluch 11, Teleport 18), **warten auf die Original-Preistabelle**. Weiter Stufen +50 % des Grundpreises, Deckel 8.
- **Seiten ohne Dopplung:** „Zauber erlernen" listet nur Zauber/Tränke/Flächen (21 Sprüche), „Kreaturen beschwören" nur die Beschwörungen (25) — die „Buch"-Spalte entfällt, alles hat einen Preis. Panels: Kreaturen-Seite Kreatur-Panel, Zauber-Seite Zauber-Panel.
- **XP-Brunnen-Stopp verstärkt:** `base_book` merkt sich die Startbuch-Stufen; Senken endet dort (Teleport 10 bleibt 10, kaufte Stufen darüber sind erstattbar).

## [Unreleased] – Designer mit Seiten-Auswahl (2026-10-04)

### Geändert
- **„Zauberer entwerfen" öffnet zuerst eine Übersicht** mit drei Einträgen: **Attribute verteilen / Zauber erlernen / Kreaturen beschwören** — Hoch/Runter + Enter wählt, Esc führt zurück zur Übersicht (und aus ihr heraus). Der versteckte z-Toggle entfällt (das `z` kam wegen QWERTZ-Mapping im Emulator nicht an). „Zauber erlernen" zeigt das Grimoire (alle 46 Sprüche mit Zauber-Panel), „Kreaturen beschwören" nur die kaufbaren Beschwörungen mit Kreatur-Panel.

## [Unreleased] – Standard-Template passt in die 600 XP (2026-10-04)

### Geändert
- **Das Standard-Template kostet jetzt 522 von 600 XP** (vorher ~1615 — Cheating). Aufteilung: Kernzauber Bolt 4/Schild 3/Heilung 4/Auge 3/Blitz 2/Fluch 2/Schnell 2/Flug 2 (144 XP), **8 Kreaturen auf Stufe 1–3** (Fledermaus 2, Goblin 2, Zwerg 3, Einhorn 1, Harpyie 1, Zombie 1, Gorilla 1, Greif 1 = 100 XP), Attribute +15 auf com/def/con, +15 sta, MR +10 (190 XP), Mana +6 (54), AP +5 (40). **Rest: 78 XP** zum eigenen Verteilen. Die Template-Attribute werden über `wizard_raise` gekauft (echte Kosten, exakte Abrechnung); das Template bleibt idempotent.

## [Unreleased] – Standardset mit 8 Kreaturen (2026-10-04)

### Geändert
- **Standardset enthält 8 verschiedene Kreaturen** (Nutzerwunsch): Riesenfledermaus 2, Goblin 2, Zwerg 2, Einhorn 2, Harpyie 2, Zombie 2, Gorilla 2, Greif 2 — aufgeteilt über die Preisklassen (Scouts, Mittelklasse, fliegender Schwerer). Damit deckt das J-Set beim Spielstart die volle Bandbreite ab: Zauber, Tränke und eine lebendige Truppe.

## [Unreleased] – c-Menü mit Zauber/Beschwörung-Auswahl (2026-10-04)

### Geändert
- **`c` fragt zuerst:** Im Spiel öffnet die Zauber-Taste jetzt eine Auswahl — **Z = Zauber** (Anzahl verfügbarer Sprünge) oder **B = Beschwoeren** (Anzahl Beschwoerungen); nur vorhandene Gruppen werden angeboten, bei nur einer Gruppe direkt zur Liste. Die Listen sind gefiltert (Zauber-Liste ohne Kreaturen und umgekehrt), Spalte „St“ heißt „Anz.“ (Anwendungen), Buchstabe wirkt wie gehabt.

## [Unreleased] – Zauber-Detail und Gesamt-Grimoire im Laden (2026-10-04)

### Hinzugefügt
- **Zauber-Detail-Panel:** Der Designer-Laden listet jetzt das **gesamte Grimoire** (alle 46 Sprüche, nicht nur kaufbare Beschwörungen). Beim Auswählen eines Zaubers zeigt das Panel **Kategorie (Beschwörung/Trank/Fläche/Zauber), Manakosten (L1 + Zuwachs pro Stufe), Schadenswürfel** — Bolt/Blitz mit Stufen-Skalierung — **und eine Kurzbeschreibung** aus der neuen `data/help/spells_de.txt` (46 Seiten, von gen_help auf SPELL-Reihenfolge geprüft). Beschwörungen zeigen weiterhin Porträt + Kreaturwerte.
- **Gefahren-Fix:** Nicht-kaufbare Startbuch-Zauber lassen sich nicht mehr „erstatten" (würde XP aus dem Nichts erzeugen — Selftest sichert das ab).

## [Unreleased] – Kreatur-Detail im Zauber-Laden (2026-10-04)

### Hinzugefügt
- **Detail-Panel beim Auswählen einer Beschwörung:** Im Designer-Zauber-Laden (`z`) zeigt die Auswahl sofort **Porträt, Kurzbeschreibung (aus dem Lexikon-Text) und die Hauptattribute** (Kampf/Verteidigung/Magieresistenz, Leben/Ausdauer/AP, VP) der Kreatur. Beim Blättern durch die Liste wandert das Panel mit — man weiß beim Freischalten, was man kauft.

## [Unreleased] – Designer nach Nutzertabellen korrigiert (2026-10-04)

### Geändert
- **Mindestverteilung + 600 XP (F6, korrigiert):** Ein frischer Zauberer startet bei Kampf 5, Abwehr 5, Magiewiderstand 70, Konstitution 25, Ausdauer 34, Mana 90, **AP 34** — und verteilt **600 XP** auf Attribute, Mana, AP und Zauber (zuvor fälschlich 20 XP und höhere Startwerte). **Kosten:** Kampf 2, Abwehr 2, Magiewiderstand 4, Konstitution 2, Ausdauer 4, Mana 9, AP 8 pro Punkt (Voll-Rückerstattung beim Senken; Maxima: Mana 250, AP 120).
- **AP ist jetzt ein Designer-Wert:** `wizard_apply_to_world` setzt die Aktionspunkte der Einheit aus dem Designer (Minimum 34) statt starr 40 aus der Kreaturtabelle.
- Der Designer zeigt die Kosten je Zeile; „Zauberer zurücksetzen" liefert Minima + 600 XP + Anker-Startbuch (alte Zauberer auf der SD werden wegen geändertem Layout zurückgewiesen → einmal zurücksetzen).

## [Unreleased] – Designer komplett: Zauber-Kauf, Anker-Kosten, Mana (2026-10-04)

### Hinzugefügt
- **Zauber-Kauf im Designer (F6-Anker, Erfahrung — kein Mana):** Taste `z` wechselt zwischen Attributen und Beschwörungs-Liste; Rechts kauft die nächste Stufe, Links erstattet. Stufe 1 kostet den Grundpreis (Zwerg/Fledermaus 4 … Spinne 28, Gespenst 44, Dämon 58, Drachen 38/50/62), **jede weitere Stufe +50 % des Grundpreises, Maximum Stufe 8** (Beispiel Anker: Harpyie 12/18/24…, Zwerg 4/6/8). Vampir (50) und Pixie (8) fehlten im Anker und sind interpoliert. Nicht-Beschwörungen sind nicht kaufbar (Startbuch, künftig Schriftrollen — D33).
- **Mana als steigerbarer Wert:** 9 XP pro Punkt (Original-Anker), Start 80, Maximum 250; fließt über `wizard_apply_to_world` in die Einheit.

### Geändert
- **Attribut-Kosten nach Anker (flach, ersetzt 5+Wert/4):** Kampf 6, Verteidigung 6, Magieresistenz 9, Konstitution 9, Ausdauer 3 XP pro Punkt.

## [Unreleased] – Original-Startzauberbuch (Nutzer-Anker 2026-10-04)

### Geändert
- **Startbuch des Stock-Zauberers = Original-Anker:** Magisches Auge 4; Schnelligkeit, Stärke, Schutz, Flug und Magischer Bolzen 6; Bombentrank, Unsichtbarkeit, Fluch, Magisches Schild und Magischer Blitz 8; Heilungstrank 9; Flut, Ranken, Gooey Blob, Enchant, Subversion und Teleport 10. Keine Beschwörungen im Startbuch (kommen aus den Szenario-Büchern). Vorher: Fledermaus 2 / Bolzen 1 / Zwerg 1 (eigener Platzhalter).
- **Buchstufen-Deckel 8 → 10** (Stufe = Ladungen und Kraft; das Original startet mit Stufen bis 10). Szenario-Dateien dürfen jetzt ebenfalls bis 10 vergeben.

## [Unreleased] – Wizard-Designer bedienbar (2026-10-04)

### Behoben
- **Attribute verteilen mit Links/Rechts:** Im Emulator kommt `+`/`-` nicht als ASCII an (FabGL-Layout) — der Designer nimmt jetzt **Rechts = erhöhen, Links = senken** (Pfeile hoch/runter wählen wie gehabt); `+`/`-` bleiben als Fallback. Dasselbe gilt für die Zufalls-Stärke im Setup-Panel.
- **Start-Budget für neue Zauberer:** Der Stock-Zauberer startete mit 0 XP — der Designer konnte nichts anheben, selbst mit richtigen Tasten. Jetzt gibt es **20 XP Schöpfungsbudget** (Kosten 5 + Wert/4 pro Punkt, wie das Original mit Punkte-Pool bei der Erstellung; eigener Wert, Amiga-Anker F6 folgt). „Zauberer zurücksetzen" stellt Budget und Werte wieder her.

## [Unreleased] – Zauber ignorieren Schilde (D32, Zauberer-Duell-Befund 2026-10-04)

### Geändert
- **Bolt und Blitz treffen gegen die Verteidigung ohne Schildbonus (D32):** Magie umgeht Rüstung (D&D-Vorbild: RK vs. Rettungswurf). Grund: Im Zauberer-Duell waren geschildete Zauberer für Bolts praktisch unhittbar (10-%-Boden der Trefferformel), Blaster/Evoker gewannen nur 6–7 % gegen den Nekromanten (34 %). Nahkampf, Bogen und Wurf treffen weiterhin auf die volle Verteidigung; Zauberschild und Protection-Trank zählen auch gegen Magie.

## [Unreleased] – Ausrüstung prägt den Kampf (D31, Arena-Befund 2026-10-04)

### Geändert
- **Waffenbonis in Original-Proportionen (D31):** Combat-Bonis erhöht (Schwert +4→+10, Speer +8, Keule +7, Axt +9, Slayer +13, Magie-Slayer +16, Messer +5, Wurfstern +4) und **Schild +4→+13 Verteidigung** (Anker: Original-Wert). Grund: Die Arena-Simulation (2000 Läufe) zeigte, dass waffenführende Kreaturen mit der alten Skala praktisch nie gewannen — Ausrüstung prägte den Kampf nicht. Mit D31 gewinnt der ausgerüstete Riese ~11 % der Mittelfeld-Schlachten, Magie-Slayer-Halter sind die erfolgreichste Waffe, 11 % der Sieger tragen ein Schild; Drachen bleiben Apex.
- **Arena:** Einheiten heben jetzt bis zu zwei Gegenstände auf (Waffe + Schild wird testbar), Korrelation/Stabilitätsausgaben unverändert.

## [Unreleased] – Kritische Treffer (D30, 2026-10-04)

### Hinzugefügt
- **Kritische Treffer (D30, D&D-orientiert):** Ein Angriffswurf unter 5 % ist kritisch — die **Schadenswürfel zählen doppelt**, der feste Kampf-Bonus nicht (Schwert 2w8 → 4w8, Stufe-1-Bolt 4w6 → 8w6). Etwa jeder zwanzigste Angriff; gilt für Nahkampf, Rückschlag, Freien Schlag, Wurf, Bogen und Bolt/Blitz; Flächen, Bomben und Möbel-Schlagen kritisieren nicht. Im Spiel: Meldung „KRIT! …“ in Rot, Overlay mit doppeltem Effekt und Crash-Sound; Krits öffnen leichter tödliche Wunden.

## [Unreleased] – Kampf-Rebalance II (D29, Playtest 2026-10-04)

### Geändert
- **Eine Reaktion pro Runde (D29):** Konter (D27) und freier Schlag beim Wegziehen (D26) teilen sich eine defensive Reaktion pro Runde und Einheit (D&D-5e-Vorbild) — wer sie verbraucht hat, wird im selben Rundenverlauf unbestraft weiter angegriffen; mit der Rundenregeneration ist sie zurück. Ein KI-Goblin, das 3× angreift, sieht danach also nur noch einen Konter.
- **Magic Bolt entschärft und stufig skaliert (D29):** jetzt **(3+Stufe)w6** statt 7w10 — D&D-Upcasting: die Buchstufe beim Wirken bestimmt die Würfel (Stufe 1: 4w6 Ø 14 … Stufe 8: 11w6 Ø 38,5). Magic Lightning **(5+Stufe)w6 + 2w6 Splash**. Ein Stufe-1-Bolt verwundet einen Goblin schwer, tötet ihn aber nicht mehr; volle Bücher schlagen deutlich härter als entladene.

## [Unreleased] – Kampf-Rebalance (D27/D28, Playtest 2026-10-04)

### Geändert
- **Rückschlag ist frei (D27):** Ein angegriffenes Ziel schlägt immer sofort zurück — ohne AP- oder Ausdauerkosten. Zuvor kostete der Gegenangriff 6 AP + 3 Ausdauer; wer oft attackiert wurde (KI-Goblin bis 3× pro Zug), wurde durch die erzwungenen Gegenangriffe ausgelaugt und konnte selbst nie mehr angreifen — Aussetzen half nicht, weil die AP-Auffüllung an der Ausdauer hängt. Angriffe kosten unverändert 10 AP + 4 Ausdauer.
- **Waffenschaden mit Würfeln (D28):** Schaden eines Treffers = Würfel der Waffe + Kampf/5 (waffenlos 1w4, Schwert 2w8, Axt/Slayer 2w10, Magie-Slayer 3w8, Bogen/Wurfspeer 2w6 …). Vorher rechnete jeder Treffer nur mit Kampf/4 — das Schwert brachte keinen spürbaren Schadensvorteil. Neue Spalten `dice_n,die` in `data/weapons.csv`; Treffer-bis-Sieg-Tabelle im GDD §6.1.
- **Zauber deutlich stärker (D28):** Magic Bolt **7w10** (Ø 38,5) und Magic Lightning **10w8 + 3w6 Splash** — ein treffender Bolt tötet einen Goblin meist sofort; Zauber verbrauchen Buchstufen und müssen sich lohnen. Würfel-Spalten in `data/spells.csv` (`dice_n,die,splash_n,splash_die`), Schadenstabelle im GDD §7.2.
- **Schild nie in der Hand (D28):** `w` überspringt Schilde (Zyklus: Waffen → leere Hände); ein getragener Schild verteidigt immer (D18/D21). Ohne führbare Waffe meldet `w` „Keine Waffe zum Fuehren (Schild zaehlt getragen).“
- Hilfeseite „Kampf“ und Lexikon-Einträge (Schwert/Axt/Magie-Slayer/Schild) beschreiben die neuen Regeln und Würfel.

## [Unreleased] – M5 Präsentationsrunde

### Hinzugefügt (M5d: Titelbild + Titelmusik)
- **Titelbild** vor dem Hauptmenü: eigene 320×240-Pixelart (Nachthimmel, Mond, Turm, Zauberer mit Stab, Portal, Schriftzug „LORDS OF CHAOS“) in der Agon-64-Palette. `tools/art/make_title.py` erzeugt `assets/title/title.png` (Quelle), `tools/build_title.py` kompiliert nach `/loc/title.bin` (76 800 Byte RGBA2222, ADR 0011). Neuer Streaming-Loader `render_show_title()`: häppchenweises Laden über den Staging-Puffer in einen eigenen VDP-Puffer (0x4000), wiederholte `write_block_data`-Aufrufe hängen an (QUIRKS S1). Ohne Datei zeigt der Titel Text. Taste führt ins Menü. **Das Motiv ist ein Vorschlag/Platzhalter — vom Nutzer abzustimmen.**
- **Titelmusik:** eigener dreistimmiger Satz in A-Moll (Lead square, Bass triangle, Arpeggio sine — nichts kopiert, D7) als Notentabelle `data/music/title.txt`, kompiliert durch `tools/gen_music.py` nach `/loc/music/title.bin`. Sequencer `src/agon/music.[ch]` auf den VDP-Audiokanälen 1–3 (Kanal 0 bleibt Effekten), nicht blockierend — Titel- und Menüschleife pollen, **jeder Tastendruck beendet die Musik**.
- SD-Staging nimmt `title.bin` und `music/` mit.

### Hinzugefügt (M5c: Ereignisse, Kampfanimation, Sound)
- **Ereignis-Ring im Core (`src/core/events.[ch]`):** `EV_SWING/HIT/WOUND/MISS/DEATH/SPELL/SMASH` mit Position und Beteiligten. Reine Beobachtung — RNG, Weltzustand, Savegames und View-Logik bleiben unberührt (Selftest prüft Ereignisfolge und identische Würfel). Emitted aus Nahkampf/Freiem Schlag/Rückschlag, `combat_damage` (alle Schadensquellen), `world_kill_unit` und Blutungstod, `pay_for_spell` (jeder Zauber inkl. Beschwörung/Brauen), Terrain-Angriff und Blitz.
- **Kampf-/Todesanimation (`src/agon/fx.c`):** spielt den Ring ab — Slash-/Treffer-/Verfehlt-Overlays, rote Schadenszahl, Todessequenz in 4 Frames (Aufblitzen → Verblassen → Staub/Kreuz), Zauberblitz und Trümmerwolke. Sieben neue Kacheln (`fx_*`, `tools/art/make_tiles.py`), `view_mark_dirty()` zum sauberen Neuzeichnen einzelner Felder. Kurze getimete Frames; Tasten während der Show werden verworfen (K5). In Skript-Runs (`--dump/--bench`) deaktiviert, damit keine Skripttasten verloren gehen.
- **KI sichtbar:** neuer Callback `Turns.on_ai` nach jeder KI-Phase und den unabhängigen Kreaturen — das Frontend zeigt Züge, Schläge und Tode der KI als Animation über der aktuellen Karte.
- **Klang-Überholung (`src/agon/sound.c`):** 16 Effekte mit Wellenform (Square/Triangle/Saw/Sine/Noise/VIC-Noise), ADSR-Hüllkurve und Notenfolgen auf Kanal 0 (1–3 frei für Musik): Schwung, Treffer, Verfehlt, Tod, Zauber, Bogen, Wurf, Tür, Truhe, Zerschmettern, Portal-Arpeggio, Rundenwechsel, Sieg/Niederlage, Schritt, Aufheben. Bisher stumme Pfade (Rückschlag, Bolt, Wurf, KI-Aktionen) spielen jetzt; `SND_MISS` ist verdrahtet.
- Dev-Screen: `loc --fxdemo` zeigt alle sieben Effektkacheln und spielt die Sounds.

### Geändert (M5c)
- **Speicherformat v3** (`SAVE_VERSION` 3, Struktur `Turns` + Callback-Feld): alte v2-Spielstände werden abgelehnt („Kein Spielstand“).
- View-Hash: durch die sieben fx-Kacheln verschieben sich die Tile-IDs — `HOUSE_VIEW_HASH` neu übernommen.

### Hinzugefügt (M5b: Hilfe, Tutorial, Lexikon)
- **Hilfeseiten (F1, Hauptmenü „Hilfe“):** sieben Seiten (Steuerung, Aktionen, Spielziel, Runden/AP, Kampf, Magie, Objekte) aus `data/help/keys.txt`, kompiliert nach `/loc/help/keys.hlp` (`tools/gen_help.py`, ADR 0011 — Daten von der SD statt ins Binary). ←/→ blättert, Umlaute über den umdefinierten Font. Fällt die Datei aus, zeigt F1 weiter die alte Tastenliste.
- **Geführtes Tutorial (Hauptmenü „Tutorial“):** kleine Karte (`data/maps/tutorial.txt`, 16×12) mit Zauberer, Zwerg, Truhenschlüssel, Truhe, Goblin und Portal (öffnet Runde 2). Schritt-Engine im Core (`src/core/tutorial.[ch]`): Bewegen → Einheit wechseln → Schlüssel nehmen → Truhe öffnen → Goblin besiegen → Zaubern → Portal; die Hinweiszeile steht unten (Texte aus `help/tutorial.hlp`), Tab/Zauber werden gemeldet und „merken sich“. Die Runde-1-Sperre [PM 7] ist im Tutorial aufgehoben.
- **Lexikon (Taste `i`, Hauptmenü „Lexikon“):** entdeckte Kreaturen (26) und Objekte (40) als Bitmasken (`src/core/lexicon.[ch]`), persistent in `/loc/lexicon.dat`; Markierung beim Sehen (Sichtregel, `lexicon_watch`) und Aufheben. Liste mit „???“ für Unentdecktes, Detailseite mit Porträt, Werten aus den Tabellen und Kurztext aus `help/lexicon.hlp` (66 Seiten, Reihenfolge = CSV).
- Dev-Screens zum Ansehen: `loc --helppage`, `loc --lexicon`, `loc --tutorial` (auch `tools/run.py`).

## [Unreleased] – M4 Classic komplett

### Geändert
- **Gebunden-Regel neu (D26, Issue #82):** Aus dem Nahkontakt weg bewegen ist jetzt erlaubt — der angrenzende Gegner bekommt dafür einen **freien Schlag** ohne AP-Kosten (Trefferchance/Schaden wie Nahkampf, Untoten-Immunität und Boden-gegen-Flieger gelten). Diagonaler Ausbruch ohne verbleibenden Kontakt bleibt frei. Behebt den Totstand „ohne Waffe neben dem Gegner: weder Angriff noch Flucht". Freie Schläge gelten symmetrisch für Spieler und KI; `BUMP_ENGAGED` entfällt.
- **Panel zeigt das Objekt in der Hand:** neue Zeile „Hand: <Name>" über der Boden-Liste (gelb); leere Hände zeigen „Hand: -". Antwortet auf die Frage „Wo sehe ich, was aktiv ist?" — Waffen wirken nur in der Hand, der Schild zählt getragen (D21).
- **Grafik v2 (Kreaturen-Review):** alle 26 Kreaturen (Greif komplett neu), 35 Objekte, alle Böden, alle 16 Wandstücke und Türen neu gezeichnet — Design-Export als PNG-Quelle in `assets/tiles/`; der Python-Generator überschreibt diese Kacheln nicht mehr (`V2_TILES`-Liste in `make_tiles.py`, Kreaturen-Generator ausser Betrieb). Schlüsselfarben für die Besitzer-Varianten bleiben erhalten (geprüft: Goblin 29/21, Zauberer 70/81 Pixel). Möbel, Kerzen, Dekor, Overlay- und Cursor-Kacheln bleiben v1.

### Behoben
- **Gebunden nur für eine Phase (GDD 6):** Eine Bodeneinheit neben einem Gegner konnte sich nie mehr wegbewegen, solange der Gegner stand – auf dem Agon fühlte sich das wie eine unsichtbare Mauer an (auch weil der Grund nicht gemeldet wurde). Jetzt bindet erst Nahkampfkontakt (heranziehen, angreifen, angegriffen werden) beide Seiten, die Bindung endet mit der eigenen Phase („im nächsten Zug ist Bewegung wieder möglich“). Beim Versuch wegzugehen erscheint „Gebunden: Gegner daneben - nur Angriff.“, bei Blob/Vine „Brei oder Ranken versperren den Weg.“
- **Szenario 1 startet mit dem Zauberer allein:** Zwerg, Fledermaus und Einhorn des Spielers waren Testeinheiten aus M3g/M4e und standen von Anfang an in der Karte (Garten, Haus). Kreaturen kommen aus dem Zauberbuch. Die Testkarte `testland` behält ihre Entwicklungseinheiten.
- **Nachbesserung M4h:** Die Zauberer-KI läuft nur noch zu Schätzen, die sie sieht (vorher ging sie zum ersten Schatz der Objektliste, auch unsichtbar); Magic Bolt trifft den nächsten sichtbaren Gegner statt den ersten in der Einheitenliste, mit genau einem Wurf pro Phase.
- **Nachbesserung M4f:** Designer: `-` senkt Attribute jetzt (volle XP-Rückgabe, nie unter den Startwert); Menüpunkt „Zauberer zurücksetzen“ ist korrekt benannt und fragt vor dem Löschen nach; `wizards.dat` hat Kopf (Magic, Version, Strukturgröße) und wird auf gültige Werte geprüft, sonst Stock-Zauberer; Szenario 0 im Kampagnenergebnis wird ignoriert (kein Shift um −1).
- **Nachbesserung M4e:** Karten mit Dach, aber ohne Portal, wurden vom Loader falsch geparst (v4 schreibt jetzt immer einen Portal-Block, `0xFF` = kein Portal); `world.c` bindet `ride.h` ein statt einer lokalen `extern`-Deklaration.
- **Review-Fixes M4a–c:**
  - Panel zeigte Kreaturen statt der Balken- und Status-Icons: Seit M4c liegen die Icon-Tile-IDs über 255, `BAR_ICON`/`STATUS_ICON` waren `uint8_t`.
  - Flugtrank: Wer ohne Flügel fliegt, bekam in der Luft 0 AP pro Runde und blieb nach Ablauf des Tranks oben. Jetzt gilt das Boden-Budget, nach Ablauf landet die Einheit (oder schwebt weiter, bis Platz ist).
  - Die Bombenphiole explodierte nie, `t` warf sie wie einen normalen Gegenstand. Phiolen fliegen jetzt bis zur ersten Einheit und zerplatzen dort.
  - Curse, Subversion, Magic Attack und Enchant brauchten weder Reichweite noch Sichtlinie (D17).
  - Kessel: Der Inhalt hing an einer Liste statt am Kessel-Objekt; nach dem Wegtragen blieb ein „Geisterkessel“. Jetzt ist das Objekt maßgeblich, volle Kessel lassen sich nicht tragen.
  - Getränke aus dem Kessel wirkten immer mit Stufe 2 statt mit der gebrauten Stufe (F1). Phiolen bleiben vorerst bei Stufe 2.
  - Drachenkraut braute nebenbei Heiltränke und wurde dabei verbraucht; es dient jetzt nur den Drachen.
  - Die KI sah unsichtbare Einheiten.
  - Rückschlag auch nach einem Fehlschlag und nach harmlosen Hieben gegen Untote (GDD §6); bisher schlug der Verteidiger nur nach einem Treffer zurück.
  - Verzauberte Waffen (Enchant) zählen doppelt (GDD §6.1).
  - Teleport streut symmetrisch und misst Chebyshev; Magic Attack und die Bombe rechnen mit Wrap-around und schreiben Kills dem richtigen Werfer bzw. Zaubernden zu.

### Hinzugefügt
- **M5a Endbildschirm:** Sieg (Zauberer entkommt durchs Portal) und Niederlage (Zauberer gefallen) lösen sofort einen Vollbildschirm aus — Gratulation bzw. „Game Over“ mit Runden, besiegten Gegnern, Beute- und Siegpunkten; bei Sieg in den Szenarien 1–3 werden VP als XP gutgeschrieben und die Stufe steigt beim ersten Abschluss (`wizard_campaign_result`, `wizards.dat`). Enter führt zurück ins Hauptmenü (die Spielschleife läuft jetzt Menü → Spiel → Ende → Menü), Esc beendet. Core: `game_outcome()`, `Game.kills/loot_vp`; ein reitender Zauberer zählt in `game_over` als lebendig. Dev: `loc --endscreen` / `--endscreen-lose`, `run.py --endscreen`.
- **Reiter sichtbar (M4k):** Wer ein Reittier reitet, wird als Reiter-Kachel hinter einer um 25 % verkleinerten Reittier-Kachel (18 statt 24 px, beim Laden aus der vollen Kachel berechnet) gezeichnet: Der Reiter bleibt deutlich sichtbar, das Tier steht darunter. Keine neue Grafik: beide vorhandenen Sprites werden zur Laufzeit kombiniert (`FieldLayers.ride`, Versatz je Reittier in `render.c`, auch im Panel-Porträt). Behoben dabei: Das Dach-Aufdecken (`apply_roof_rule`) verschob die Luft- und Reiter-Maske nicht mit, wenn es eine Ebene entfernte. Fliegende Paare tragen auf beiden Ebenen die Luft-Markierung.
- **M4j Politur (#52):**
  - **Kontextmenü mit Enter** (GDD §5.1.4): alle aktuell möglichen Aktionen mit Taste und AP-Kosten (unmögliche ausgeblendet), Buchstabe wirkt direkt, Esc schließt
  - **Big Map `m`** (GDD §11.1): 36×36 als 4×4-Blöcke im Kartenfenster, eigene Einheiten weiß, Feinde rot, Portal magenta
  - **Nachrichten-Log `l`**: Ringpuffer mit den letzten 10 Ereignissen (Kills, Zauber-Treffer, Rettung, Beschwörungen)
  - **Hilfe F1**: vollständige Tastenübersicht
  - **Sound** (GDD §11.4): Schritte, Treffer, Zauber, Aufheben, Portal, Tod über den Agon-Audiokanal (VDU 23,0,135)
  - **Umlaut-Font**: der Systemfont wird in einen VDP-Puffer kopiert und um ä/ö/ü/ß ergänzt (CP437-Codes), UI-Texte können echte Umlaute tragen
  - Bugfix: `run.py --no-menu` kollidierte nicht mehr mit `--bench`

- **M4i Speichern und Setup (#51):**
  - `src/core/save.[ch]`: binärer Spielstand (Welt, Zug-State, Portal/VP, Zauberbücher, Ladungen) mit Magic/Version/Größen-Gates; FNV-Hash für die Abnahme
  - **Autosave am Rundenende** in `save.dat` (GDD §2.3), Meldung „Gespeichert."
  - **5-Ladungen-Regel**: jeder Ladevorgang verbraucht eine Ladung, aufgebraucht = kein Laden mehr; **F8:** im Setup abschaltbar
  - **Setup-Panel** (Menü): Zufalls-Zauberer-Stärke 1–8 (+/−, erzeugt einen Slot-3-Zufalls-Zauberer, F9) und die Ladungen-Regel (L)
  - Hauptmenü: „Spielstand laden" stellt den exakten Zustand wieder her (Karte, Zug-State, Bücher)
  - Der Spielstand enthält auch Flächeneffekte und die erkundete Karte; Laden überspringt das Neuladen der Karte, die Ladungen-Regel prüft den Zähler der Datei, Schreiben geht über `save.new` und lässt die alte Datei bei Fehlern stehen

- **M4h KI-Ausbau (#50):**
  - **Wächter-Profil** (GDD §10): Untoten-Wachen der Szenario-Karten stehen auf ihrem Spawn-Posten, greifen Eindringlinge an und kehren zurück (`ai_set_post`/`ai_guard`, Postbereich 3 Felder); normale Unabhängige bleiben Jäger
  - **Zauberer-KI** nutzt jetzt Magic Bolt auf sichtbare Gegner und **sammelt Schätze** (nächster sichtbarer Schatz per Sichtstrahl, Aufheben auf dem Feld); Beschwörung/Portal-Flucht wie bisher
  - **Bench:** eine komplette KI-Zauberer-Phase kostet **160 ms** im Emulator (Budget 2 s, im `--bench`-Output sichtbar)
  - Abnahme: die KI entkommt gegen einen passiven Spieler zuverlässig durchs Portal (20-Runden-Dump)
  - **Fehler aus der Demo-Jagd gefixt:** `map_path` war im `--dump`/`--bench`-Modus uninitialisiert (Müll-Pfad beim Boot), und der SD-Lese-Puffer fasste v4-Karten (5304 B) nicht mehr — beide Karten laden jetzt wieder

- **M4g Szenarien 2 und 3 (#49):**
  - **Slayer's Dungeon** (eigene Karte, D2): Steinkorridore und Krypten, Untoten-Wache (Zombies, Geist, Vampir, Spectre), Schätze bis zum Slayer, Portal in der fernen Krypta (Runde 20–24)
  - **Ragaril's Domain** (eigene Karte): Zauberwald- und Schattenwald-Gürtel, Sumpfmoor, der Turmquartier im Nordosten; Ragaril (KI) befehligt Untote bis zum Dämon; Portal hinter dem Turm (Runde 44–51)
  - Szenario-Dateien für beide (Zauberbücher p1/p2 passend zur Stufe, GDD §9); das Hauptmenü listet alle drei Szenarien mit Stufenhinweis und lädt Karte + Bücher je nach Wahl
  - `run.py`: `--testland`/`--no-menu` ergänzt; das Spiel wertet jetzt **alle** Argumente aus statt nur argv[1] (Flag-Kombinationen funktionieren wieder)

- **M4f Zauberer und Kampagne (#48):**
  - `src/core/wizard.[ch]`: Designer-Datenmodell (Name, Level, XP, 5 Attribute mit linearen Kosten und Obergrenzen F6, Zauberbuch), 4 Slots
  - Hauptmenü (GDD §2.3): Szenario starten, Designer, Zauberer zurücksetzen, Beenden; Pfeile + Enter
  - Designer-Bildschirm: Attribute mit +/− erhöhen (XP-Konto im Kopf), Esc zurück
  - 4 Zauberer-Plätze in `wizards.dat` auf der SD (Laden beim Boot, Zurücksetzen auf den Stock-Zauberer)
  - Kampagne: **VP → XP 1:1**, erstes Abschließen eines Szenarios hebt die Stufe (Wiederholen bringt nur XP, GDD §9); **F5:** der Übertrag trägt nur Attribute, Zauberbuch und XP — der Zauberer startet unbewaffnet
  - Zufalls-Zauberer nach Stärke (Setup-Startwert, F9): XP-Budget und zufällige Buchstufen über den Partie-RNG

- **M4e Kampf komplett (#47):**
  - 7 neue Waffen als Objekte (Messer, Speer, Keule, Axt, Wurfstern, Slayer, Magie-Slayer) mit eigener Pixelart und den Werten aus `weapons.csv` (D7)
  - **Reiten** (`src/core/ride.[ch]`, Taste `b`): freundliche Reittiere (CF_MOUNT) nehmen Reiter (CF_RIDE: Zauberer, Pixie, Zwerg, Goblin, Troll) auf — das Paar ist eine Einheit (Flag `UF_RIDDEN`, Reiterart an Bord, Gepäck wandert mit), bewegt sich mit den AP des Reittiers, erscheint einmal in der Tab-Reihenfolge; Absteigen braucht ein freies Nachbarfeld (4 AP). **Reiter greifen von befreundetem Feld an** (D21-Ausnahme: `world_engaged` gilt nicht für sie)
  - **Dächer** im Kartenformat v4 (`roof`-Grid, R pro Feld): blockieren Sicht (auch Luft→Boden, GDD 3.2) und Landen; **F7:** das Dach ist von außen sichtbar und wird ausgeblendet, wenn eine eigene Einheit im Gebäude steht (dynamische View-Regel in beiden Kompositionspfaden)
  - Szenario-Karte: Dach über dem Starthaus, Einhorn daneben

- **M4d Flächeneffekte (#46):**
  - `src/core/area.[ch]`: Feld-Ebene mit 4 Flächenarten (Feuer, Gooey Blob, Tangle Vine, Flood), je bis 48 Felder mit eigener Stärke
  - F4-Ausbreitung am Rundenende (Chance Stärke × 10 % pro Feld, neue Felder Stärke − 1, alte − 1, Erlöschen bei 0), deterministisch über den Partie-RNG; Überschneidungen: gleiche Art frischt auf/ab (GDD-Feuerregel), verschiedene Arten überschreiben
  - Startwerte (D7, im GDD dokumentiert): Feuer 6 Schaden nur gegen Feinde (auf Gras/Holz/Bäumen), Blob 3 (außer Wasser, blockiert ab Stärke 2), Vine 2 (Gras/Wald, blockiert ab Stärke 2), Flood ertränkt Nicht-Wasserwesen mit 50 %/Runde
  - 8 animierte Overlay-Kacheln (Layer 7, GDD §11.3); Zauber wirken über den Zielmodus; Bench: 71 ms pro Tick mit 4 Flächen (Abnahme < 500 ms erfüllt)

- **M4c Tränke und Brauen (#45):**
  - `src/core/brew.[ch]`: Kessel (bis 4 pro Karte, leer/voll, Stufen) und Phiolen als Objekte; Brauen braucht leeren Kessel + Zutat auf dem Zaubererfeld, füllt `Stufe+3` Schlucke (GDD §7.2), verbraucht die Zutat und eine Zauberstufe
  - 7 Zutaten (Mistelzweig, Kleeblatt, Kristall, Schwefel, Feeenschwingel, Nitro, Drachenkraut; Apfel heilt mit), 12 neue Kacheln
  - Trinken `q` (Kessel oder Phiole, 4 AP), Füllen `v` (6 AP, Phiole trägt ihren Trank als eigenes Objekt); Wirkungen und F1-Dauern aus M4b; Bombenphiole explodiert beim Wurf im 3×3-Bereich
  - Drachen-Beschwörung braucht Kessel mit Drachenkraut unterm Zauberer; das Kraut wird bei Erfolg verbraucht `[PM 21]`
  - Kessel-Objekte aus Karten werden beim Laden registriert

- **M4b Wirkungen und sonstige Zauber (#44):**
  - `src/core/effect.[ch]`: zeitlich begrenzte Wirkungen pro Einheit (Art, Stärke, Rundenzahl, 4 Plätze), Tick am Rundenende; `UF_INVISIBLE`/`UF_MAGIC_WEAPON` folgen ihrer Wirkung
  - Wirkungen in den Werten: Schild/Schutz +Verteidigung, Stärke +Kampf, Schnell doppelt AP und dreifache Ausdauer-Regeneration, Flugg-Trank erlaubt Start ohne `ap_fly`; Status-Icons im Panel (Schild/Schwert/Stern/Blitz)
  - 7 Zauber (D22/F1–F3): Magic Shield (+2×Stufe Verteidigung für 2×Stufe Runden, wirkt auf den Zauberer), Magic Eye (Sicht von einem Punkt, eine Runde, durch Wände), Teleport (Reichweite 6, F3-Streuung, danach 0 AP), Curse (tödliche Wunde, Widerstand nach F2 +20), Subversion (Kreatur wechselt die Seite, nicht auf Zauberer/Reittiere), Magic Attack (trifft die ganze Kreaturart im Umkreis 2, auch eigene, Widerstand −10), Enchant (Waffen aller Einheiten auf dem Feld werden magisch, 2×Stufe Runden — Untote verwundbar)
  - Zauberliste führt alle Zauber in den Zielmodus; Selbstziel-Abbruch gilt nicht für Schild/Enchant (GDD-Ausnahme)
  - Nebenher: send_keys fokussiert das Emulatorfenster jetzt per Klick (Windows-Vordergrundsperre machte Tasten unzuverlässig)

- **M4a Classic-Lücken und Szenario-Format (#43):**
  - Untote nur durch Untote, magische Waffen (Magic Slayer, Enchant-Flag) und Zauber verletzbar `[PM 18]` — Bogen/Wurf/Nahkampf prüfen, Zauber umgehen die Regel
  - Unter 50 % Constitution: −2 Combat und − Defence (Startwert, GDD §4.1)
  - Nahrung: Apfel/Pilz heilen Constitution, magische Varianten Mana (`e`, 6 AP), Werte aus objects.csv; 6 neue Kacheln
  - Truhenspezialfall: Bump öffnet — mit Truhenschlüssel 8 AP (Schlüssel verschwindet), ohne 3× Aufbrechen; Loot-Tabelle wirft einen zufälligen Schatz aus; `r` liest Schriftrollen (8 AP, Hinweistext)
  - Szenario-Dateien `data/scenarios/*.txt` → kompilierte `.scn` (LOCS v1) mit Zauberbüchern aller Zauberer; `spellbook_default` entfällt; Zauberbücher kommen aus der Datei

## [Unreleased] – M3 Classic spielbar

### Geändert
- **Regeln aus dem M3-Review (GDD D21):**
  - Werfen, Bogen und Bolt nutzen dieselbe Trefferformel wie der Nahkampf (10–90 %, Defence inklusive Schild).
  - Schilde stapeln nicht mehr: Ein getragener Schild zählt, weitere nicht.
  - Wer stirbt (Kampf, Zauber, Verbluten), lässt alles Getragene auf sein Feld fallen, auch Schätze. Wer durchs Portal entkommt, nimmt es mit.
  - Bestätigt ohne Codeänderung: Ein Bodenverteidiger schlägt einen angreifenden Flieger zurück; der Blitz-Splash trifft auch eigene Einheiten und den Zaubernden.

### Behoben
- **Review-Fixes M3:**
  - Das Spiel hing in einer Endlosschleife, wenn der Mensch keine Einheiten mehr hatte (z. B. nach der Flucht durchs Portal). Die KI spielt jetzt zu Ende, bis kein Zauberer mehr da ist, höchstens 40 Runden (`TURN_AUTOPLAY_ROUNDS`). Danach kommt die Abrechnung und nur noch Esc wirkt.
  - Das Portal öffnet sich jetzt auch in Runden, die die KI allein spielt: Runden-Hook `turns.on_round` statt Aufruf in `main.c`.
  - Nach einem Tod war oft die falsche Einheit aktiv, und Fertig-Markierungen wurden vertauscht. Einheiten haben jetzt eine stabile `id`, das Fertig-Flag liegt an der Einheit, und `turn_revalidate` findet die aktive Einheit über die ID wieder. `turn_on_unit_removed` entfällt.
  - Kill-VP zählen jetzt den Wert des **Opfers** statt den des Killers. Tode laufen über `world_kill_unit` mit Protokoll, `game_credit_kills` rechnet ab. Treffer auf eigene Einheiten zählen nicht, mehrere Tote pro Blitz zählen einzeln, Fernkampf nennt den echten Schützen, und die KI bekommt ihre Kills ebenfalls gutgeschrieben.
  - Ein tödlicher Wurf konnte dem Spieler eine fremde Einheit als aktive geben.
  - Die KI las nach einem abgelehnten Nahkampf ein uninitialisiertes `CombatResult`; `combat_melee` nullt das Ergebnis jetzt immer.
  - Die KI behielt nach Toden veraltete Indizes, und Jäger konnten doppelt handeln. Jetzt wird über ID-Schnappschüsse iteriert (`ai_run_hunters`).
  - Die Fertig-Bits als `1u << i` waren auf dem eZ80 ab Einheit 24 undefiniert (24-Bit-`int`).
  - Die vier Kopien der Schadensanwendung sind zu `combat_damage` zusammengeführt.

### Hinzugefügt
- **Design Mega Drive als zweite Plattform (GDD D23):** Gamepad-Bedienung (§5.4), Darstellung (§11.5), ADR 0010 mit Core-Spike und Abbruchkriterien, Roadmap-Spur „MD“.
- **M3g Szenario 1 „The Many Coloured Land“ (#33):**
  - Kartenformat v3 mit `portal x y rmin rmax`-Sektion (v2-Karten laden weiter); Welt trägt Portal-Position und Rundenspanne
  - 4 neue Schatz-Kacheln (Runenstein 6, Zauberstab 8, Rubin 20, Diamant 30 VP) neben Gold/Smaragd — die komplette Schatztabelle aus GDD §9.1
  - Szenario-Karte `data/maps/many_coloured_land.txt` (eigene Karte im Geist des Originals, D2): bunte Regionen, 6+ Schätze, KI-Zauberer, Portal Runde 12–15 bei (26,3)
  - `loc` startet jetzt Szenario 1; `loc --testland` öffnet die Entwicklungskarte
  - komplette Partie im Emulator durchspielbar und dokumentiert (p1 entkommt, p2-KI beschwört/flieht/zieht VP)

### Hinzugefügt
- **M3f Einfache KI (#32):**
  - `src/core/ai.[ch]`: Unabhängige jagen das nächste sichtbare Ziel (ein Strahl pro Kandidat — Hidden Movement wird respektiert), greifen an, wenn angrenzend; ohne Beute Umherstreifen
  - Zauberer-KI pro Phase: eigene Kreaturen jagen zuerst, Nahkampf gegen sichtbare Angrenzende, beschwört solange Begleitung < 3 und Mana reicht (günstigster Zauber), läuft zum Portal (auch vor dem Öffnen) und tritt hindurch, sobald es offen ist
  - AI-Registrierung über `turns.ai`-Callback (Standard: passen, für Tests); `turn_init` nullt jetzt den ganzen Zustand (fixt uninitialisierte Callback-Felder)
  - AP-Budgets begrenzen jede Phase natürlich; deterministisch über den Partie-RNG

- **M3e Portal und Siegpunkte (#31):**
  - `src/core/game.[ch]`: Portal erscheint deterministisch in der Rundenspanne (RNG), Betreten = Rettung für Zauberer mit getragenen Schätzen (VP), Kreaturen können nicht durch `[PM 29]`
  - Siegpunkte: Entkommen +10 (D19), Schätze aus objects.csv beim Übertritt, Kills (Kreaturwert, Zauberer doppelt im Nahkampf, AMI 4), Fernkampf einfach
  - Spielende, wenn kein Zauberer mehr lebt: Abrechnung als Meldungszeile; Runden-Hook öffnet das Portal mit Meldung
  - Portal als animierte 2-Frame-Kachel (View-Ebene über Terrain, blinkt mit)
  - Testland-Portal bei (26,3), Spanne Runde 12–15 (bis M3g in die Karte zieht)

- **M3d Basis-Objekte (#30):**
  - `data/objects.csv` + `data/weapons.csv` (Startwerte PM 35, D7), generierte `OBJECTS`/`WEAPONS`-Tabellen; 5 neue Objekt-Kacheln (Schwert, Bogen, Schild, Gold, Smaragd)
  - Inventar pro Einheit (6 Plätze) mit Trage-Limit aus der Kreaturtabelle, ein Objekt „in Benutzung“
  - Aufheben `g` (6 AP), Fallenlassen `d` (2 AP), Wechseln `w` (4 AP), Werfen `t` (10 AP, Flug bis 6 Felder, Wurfschaden, landet vor dem Ziel), Bogen `f` (12 AP, Reichweite 6, Sichtlinie, trifft Boden und Luft)
  - Waffenboni im Kampf: Schwert +4 Combat in der Hand, Schild +4 Defence immer beim Tragen (D18)
  - Testland: Schwert/Schild/Bogen/Gold/Smaragd beim Start-Haus

- **M3c Magic Bolt und Lightning (#29):**
  - `spell_bolt()`/`spell_lightning()`: Reichweite 6 (D17), Sichtlinie via neue öffentliche `sight_has_los()` (baut ihre Blockade-Bitmap selbst), Treffer nach Kampf-Modell (Defence zählt), Erd-/Lufteinheiten treffbar
  - Lightning: zusätzlich 8 Nachbarfelder, zerschlägt zerstörbares Terrain am Ziel, massives Ziel (Wand) wird abgelehnt
  - Zielmodus: Cursor gelb/blau/rot je Ziel, Pfeile/Akkorde bewegen, Enter/Leertaste wirkt, Esc oder Zielen auf den Zauberer bricht ohne Kosten ab; Panel zeigt das Zielfeld
  - `turn_revalidate()` als Sicherheitsnetz nach Flächen-Toden
  - eZ80-Selftest druckt nur noch Fehler (Emulator-Konsole verlor das Ende langer Ausgaben)

- **M3b Beschwörungen (#28):**
  - `Spellbook` pro Zauberer: Zauberstufen 1–8, jede Anwendung senkt die Stufe, bei 0 ist der Zauber verbraucht (GDD §7.1); Startbücher bis zur Szenario-Datei (M3g) im Core
  - `spell_summon()`: `Stufe` Kreaturen erscheinen auf freien Boden-Nachbarfeldern (Ring-Reihenfolge), kein Platz für alle → Mana verloren (GDD §7.2); 10 AP (`ACT_CAST`), Mana linear, nicht aus der Luft
  - Zauberliste als Overlay (`c`): Name, Stufe, Mana; Auswahl mit Buchstaben (Handler vor der WASD-Bewegung), `Esc` bricht ab
  - `SUMMON_KIND`-Tabelle generiert aus spells.csv/creatures.csv (0xFF = kein Beschwörungszauber)

- **M3a Kampf (#27):**
  - `src/core/combat.[ch]`: Nahkampf nach GDD D16 — Trefferchance 50+5×(Combat−Defence) (10–90 %), Schaden (Combat+Zufall)/4
  - Rückschlag automatisch, wenn der Verteidiger AP und Stamina hat `[PM 18]`; Aktionskosten aus `actions.csv`
  - Tödliche Wunde bei Einzeltreffer über 25 % Con `[PM 17]`, blutet −1 Con pro Runde bis zum Tod
  - Tod entfernt die Einheit (swap-mit-letzter); `turn_on_unit_removed()` repariert aktive Einheit und Fertig-Bits
  - Gebunden: Bodeneinheiten neben lebenden Gegnern können nicht mehr weg `[PM 17]`
  - Terrain-Angriff: Zähigkeit je Feature aus `data/features.csv` (Wände unzerstörbar), Feature fällt bei Schaden+Zufall(0..3) > Zähigkeit
  - Bump auf Gegner ist jetzt der Angriff (Meldungen für Treffer/Wunde/Tod/Rückschlag), Bump auf Terrain der Hieb

## [Unreleased] – M2 Core-Skelett

### Hinzugefügt
- **M2f Bump und Look-Modus (#18):**
  - `world_bump_kind()` klassifiziert gescheiterte Schritte (Tür, Einheit, Terrain, AP) für Bump-Interaktionen (GDD §5.1)
  - Bump auf geschlossene Tür öffnet sie (`world_open_door`: nur Kreaturen mit Händen `CF_USE`, 6 AP aus `actions.csv`, View-Cache und Sicht werden neu berechnet)
  - Bump auf Gegner/Terrain meldet „Kampf folgt in M3“
  - Look-Modus `x`: freier weißer Cursor per Pfeilen/Akkorden, Panel zeigt das untersuchte Feld bzw. die (sichtbare) Einheit, `describe_field()` nennt Einheit (mit „(Luft)“) oder Boden; Unerforscht bleibt dunkel; `Esc`/`x` beendet
  - Erste Haustür in Testland ist jetzt geschlossen (Bump-Demo)

- **M2e Luft- und Bodenebene (#17):**
  - `world_unit_at` mit Ebenen-Parameter (Boden/Luft, GDD D15): eine Boden- und eine Lufteinheit pro Feld
  - Fliegen: konstante 4/6 AP aus `costs.csv` (`AIR_AP_*`), ignoriert Terrain und Boden-Einheiten; Aufsteigen `<` / Landen `>` mit Aktionskosten, kein Landen auf Wasser
  - Rundenende füllt das Ebenen-Budget auf (am Boden `ap_max`, in der Luft `ap_fly`)
  - Sicht aus der Luft: Reichweite 11, keine Terrain-Blockade (GDD §3.4)
  - View: Flieger 3 px höher mit Bodenschatten über der Boden-Einheit (GDD §11.3), blaue Cursor-Farbe in der Luft, Panel-Icon „fliegt“ und AP-Balken am Luft-Budget
  - Testland hat eine p1-Riesenfledermaus; `loc --fly` startet Flieger in der Luft (Demo: ISO-Taste `<` ist im Emulator nicht sendbar, #3)

- **M2d Sichtlinie und Hidden Map (#16):**
  - `src/core/sight.[ch]`: Bresenham-Sicht pro Zielfeld (Chebyshev 9 Boden / 11 Luft, Endpunkte exklusiv, GDD D14), pro Spieler Bitfelder „erkundet“ (akkumuliert) und „sichtbar“
  - Unerforscht = schwarze Kachel (neue Pixelart), erinnert = Raster-Overlay; Gegner nur bei aktueller Sicht (Hidden Movement `[AMI 4]`), unsichtbare Gegner nie
  - Sicht wird nur nach eigenen Schritten und am Rundenende neu berechnet
  - `-O2` statt agondevs `-Oz` (Compose 28→18 ms, Voll-Redraw 52→44 ms), Sicht-Berechnung 792→162 ms durch Blocking-Bitmap und int8-Rays (ADR 0009)
  - View-Hash bewusst aktualisiert (neue Kachel verschiebt IDs)

- **M2c Rundenablauf (#15):**
  - `src/core/turn.[ch]`: Rundenzähler, Phase (unabhängige Kreaturen, dann Zauberer 1..n), aktive Einheit, seedbares RNG
  - `Tab`/`Shift+Tab` wählt die nächste bzw. vorige eigene Einheit mit AP, `Leertaste` beendet eine Einheit, `Shift+E` beendet den Zug (zweimal drücken als Bestätigung, `Esc` bricht ab)
  - Runde 1 erlaubt kein Bewegen, nur Zaubern `[PM 7]`; für Skript-Läufe per `loc --free-round1` abschaltbar
  - unabhängige Kreaturen streifen deterministisch umher (Platzhalter bis zur KI in M3), der KI-Zauberer passt
  - Rundenende: AP auffüllen, 25 % Stamina, 4 % Mana; erschöpfte Kreaturen (Stamina unter 25 %) erhalten nur halbe AP `[PM 12]`
  - Meldungszeile zeigt „Runde n – Zauberer-1: <Einheit>“, Panel und Kamera folgen der aktiven Einheit
  - VKey-Messung Tab/Leertaste/Shift (ADR 0007 Nachtrag)

- **M2a Terrains und Testland (#13):**
  - 7 neue Böden (hohes Gras, Wald, Zauberwald, Schattenwald, Sumpf, Wasser animiert, Geröll) und Fels als eigene Pixelart
  - Kosten, Sicht-Blockade, Terrain-Affinität und Ertrinken aus `data/costs.csv`
  - Testkarte 36×36 mit Wrap-around (zwei Häuser, Fluss mit Brücke, Wälder, Sumpf, Geröllfeld)
  - Kamera scrollt über die Kartenränder
  - `gen/maps.h` deklariert alle Karten

- **M2b Kreaturen (#14):**
  - `data/creatures.csv` aus der Kreaturtabelle `[PM 34]` (D12), mit Terrain-Affinität
  - alle 25 Kreaturen als eigene 24×24-Pixelart in 3/4-Ansicht (D13), jeweils in 5 Besitzerfarben (`tools/art/creatures.py`, Übersicht `docs/design/mockups/creatures.png`)
  - 16-Bit-Kachel-IDs, Kartenformat v2
  - Testland mit mehr Kreaturen

### Behoben
- Leistung: keine Divisionen mehr pro Feld auf Wrap-Karten (Berechnung 32 → 22 ms).
- Eingabe: Die Ereignis-Warteschlange wird vor der Tastenwiederholung geleert (falsche Richtungen nach Akkorden bei langsamen Frames).
- ez80-clang-Absturz bei `||`-Ketten über Enums, umgangen per Lookup-Tabelle (AGON-QUIRKS T7).

## M1 Grafik und Eingabe

### Geändert
- **Darstellungskonzept v2** (GDD D9–D11, ADR 0005): eigene 24×24-Pixelart in 3/4-Frontansicht, 9×9-Sicht, mehrere Ebenen pro Feld. Die 8×8-Glyphen-Demo ist entfernt.

### Hinzugefügt
- **Pixelart-Pipeline:**
  - Agon-64-Palette (`assets/palette/agon64.gpl`)
  - erste 44 Kacheln und 6 Icons
  - `tools/build_tiles.py` (→ `tiles.bin` RGBA2222, Besitzerfarben, Halb-Böden)
  - `tools/mockup.py`
- **Karten als Text:** `data/maps/wizard_house.txt` und `tools/gen_maps.py`.
- **Core:**
  - `world` mit Boden-, Dekor- und Feature-Ebene, Möbel-Blockade und AP-Kosten (Weg 3, sonst 4, diagonal ×1,5)
  - `view` mit Ebenen-Komposition, Wand-Auto-Tiling, Halb-Böden, Dirty-Feldern und Static-Cache
- **Agon-Renderer:**
  - Kacheln von SD in VDP-Buffer, Felder Ebene für Ebene
  - Panel mit 6 Balken und Icons, Meldungszeilen
  - Kerzen-Animation
  - `--bench`: Voll-Redraw 48 ms (ADR 0006)
- **Tests:** Welt-, View- und AP-Checks; schneller Pfad gleich Referenz; View-Hash plattformgleich.
- **Datenladen (#5, ADR 0008):**
  - Karten als validiertes Binärformat von SD (`/loc/maps/*.map`)
  - Regeltabellen aus CSV einkompiliert (`gen_data.py`): Zauber mit Mana-Formel, Bodenkosten, Aktionen
  - `gen_maps.py` liest Enum-Werte aus den C-Headern
  - Selftest prüft den Lader auch mit kaputten Daten
  - Speicher: etwa 63 KB von 448 KB
- **Info-Panel (#6):**
  - Einheiten mit Werten: Stamina, Constitution, Combat, Defence, Mana, Status-Flags (vorläufig bis M2)
  - Gehen kostet Stamina (AP/2), eine neue Runde stellt 25 % wieder her
  - 5 Status-Icons, Namen-Tabelle (`names.c`), „Am Boden“ zeigt Objekt, Möbel, Teppich bzw. Boden
  - Mockup-Panel an das Agon-Layout angeglichen
- **Sprite-Cursor und Animation (#4):**
  - Cursor als VDP-Sprite (4 Farben, blinkt)
  - `view_animate()` tauscht nur animierte Felder: Kerzen 16 → 6 ms pro Frame
- **Eingabe (#3, ADR 0007):**
  - `loc --keytest` (Tastatur-Spike)
  - Bewegung per Virtual Key
  - Pfeil-Akkorde für Diagonalen (80 ms), Pos1/Bild↑/Ende/Bild↓ als Diagonalen
  - eigene Tastenwiederholung (350/200 ms)
  - `send_keys.py` kann Akkorde und gehaltene Tasten senden; `run.py` setzt das deutsche Layout

## M0 Fundament (`v0.1.0`)

### Hinzugefügt
- **Projektgerüst:** `src/core` (plattformfrei), `src/agon` (VDP-Frontend), `host/` (PC-Frontend).
- **Tooling:**
  - `tools/setup.py` mit gepinntem fab-agon-emulator 1.2.5 und agondev v0.22 (SHA-256-geprüft)
  - `build.py`, `test.py`, `run.py`, `send_keys.py`, `screenshot.py`
- **Tests:** Core-Selftest, der identisch auf dem PC und im Headless-Emulator (eZ80) läuft; Exit-Code über I/O-Port 0.
- **Demo-Szene „Hello Glyph“:**
  - MODE 8 mit eigenen 8×8-Glyphen und Dirty-Cell-Rendering
  - Bewegung per Pfeiltasten/WASD
  - Bildschirm-Dump nach `loc.log`
- **CI:** GitHub Actions (Build, Host-Test, Emulator-Test, Artefakt `loc.bin`).
- **Docs:** GDD v0.4, Amiga-Beobachtungen, Roadmap, Architektur, ADRs 0001–0004.
- **Daten:** `data/spells.csv` (Mana-Formel), `data/costs.csv`, `data/actions.csv`.
