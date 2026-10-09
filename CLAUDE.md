# CLAUDE.md – Hinweise für KI-Agenten

Lords of Chaos Remake für den Agon Light (eZ80). C99 mit agondev, Python-Tools mit uv. Docs auf Deutsch, Code und Kommentare auf Englisch.

**Einstieg:** [docs/HANDOVER.md](docs/HANDOVER.md) enthält den aktuellen Stand, die Zusammenarbeit mit dem Nutzer, Fallstricke und die nächsten Schritte.

## Befehle

```bash
uv run tools/setup.py                 # einmalig (Emulator + agondev, gepinnt)
uv run tools/build.py --all           # bin/loc.bin + build/host/loc_host
uv run tools/test.py                  # MUSS grün sein vor jedem Commit
uv run tools/run.py --dump --time 8 --free-round1 --keys "dd" --screenshot   # GUI-Check
```

## Grafik- und Karten-Pipeline

- **Kacheln** sind `assets/tiles/*.png` (24×24, nur Farben aus `assets/palette/agon64.gpl`). `tools/build_tiles.py` erzeugt `build/tiles.bin` und `src/core/gen/tiles.h`.
- **Karten** sind `data/maps/*.txt`. `tools/gen_maps.py` erzeugt `build/maps/*.map` (Binärformat, Laden von SD) und `src/core/gen/maps.c` (dieselben Bytes für Tests). `tools/mockup.py` liest dieselben Textdateien (ADR 0008). `tools/gen_variants.py` erzeugt daraus 16 Gelände-Varianten von Level 1 (`build/maps/mcl_vNN.map`, D57; die Häuser kommen aus der Basiskarte).
- **Regeltabellen** sind `data/*.csv`. `tools/gen_data.py` erzeugt `src/core/gen/data.[ch]`: Zauber, Bodenkosten, Aktionen.
- `src/core/gen/` ist generiert und gitignored. `tools/build.py` und `tools/test.py` erzeugen es automatisch.
- **Aseprite (ADR 0013):** Je Familie eine `assets/aseprite/*.aseprite` (Kachel = Slice). Bearbeiten in Aseprite oder über das MCP `aseprite` (`.mcp.json`, eingerichtet von `tools/setup.py`), dann `uv run tools/art/aseprite.py export`; vor Grafik-Commits `… check`. Die PNGs bleiben eingecheckt, Build und CI brauchen kein Aseprite.
- `tools/art/make_tiles.py` hat die ersten Kacheln erzeugt. Die PNGs sind jetzt die Quelle; das Skript nur mit `--only NAME` neu laufen lassen.

## Regeln

- **`src/core` ist plattformfrei.** Keine `agon/`-, MOS- oder VDP-Header, nur `stdint`-Typen (`int` ist auf dem eZ80 24 Bit), Zufall nur über `rng.h`, keine Gleitkommazahlen in Regeln (ADR 0003).
- **Alles unter `src/` wird von agondev kompiliert.** Host-Code gehört nach `host/`.
- **Neue Core-Logik bekommt Checks in `tests/selftest.c`.** Sie laufen auf Host **und** eZ80 (eigenes Programm `loctest.bin`, nicht im Spiel; QUIRK S6). `build.py` bricht ab, wenn die RAM-Reserve des Spiels unter 16 KB fällt.
- **View-Hash (`HOUSE_VIEW_HASH`)** ändert sich, wenn Karte, Kacheln oder Kompositionsregeln sich ändern. Den neuen Wert aus der Testausgabe übernehmen, aber nur bewusst.
- **Spieldesign:** `docs/design/GDD.md` ist die Quelle der Wahrheit. Entscheidungen D1–D13 stehen in §14. Keine Originalwerte kopieren (D7), außer sie sind dort ausdrücklich übernommen.
- **Niemals committen:**
  - `reference/`: Handbücher, ADF/DSK, Screenshots, Fremdkarten; urheberrechtlich geschützt
  - `emulator/`, `toolchain/`, `sdcard/`, `.cache/`
- **Plattformwissen** steht in `docs/AGON-QUIRKS.md`. Neue Erkenntnisse dort eintragen.
- **Workflow:** Feature-Branch → PR → CI grün → Merge. `CHANGELOG.md` pflegen.

## Studio-Framework (CCGS)

Dieses Repo nutzt *Claude Code Game Studios* (v1.1.3): Agenten, Skills und Hooks unter `.claude/`, Einstellungen in `project.yaml` (`/settings` zeigt die wirksamen Werte). Die Regeln oben gehen vor; wo das Framework Engine-Standards annimmt, gilt diese Zuordnung:

| Framework-Annahme | Hier |
|---|---|
| Engine (Godot/Unity/Unreal) | keine: C99 + eZ80, agondev. `engine.name` bleibt leer (kennt nur Godot/Unity/Unreal), die Engine steht als agondev v0.22 in `.claude/docs/technical-preferences.md`; Engine-Spezialisten entfallen |
| Code-Wurzel | `src/` (`src/core` plattformfrei, `src/agon` Frontend), Host-Code in `host/` |
| Design-Quelle (`design/gdd/`) | `docs/design/GDD.md` mit Entscheidungen in §14 (D1–D67 …) |
| ADRs (`docs/architecture/`) | `docs/adr/` |
| Tests (gdUnit4, `tests/unit/`) | `tests/selftest.c`, ausgeführt mit `uv run tools/test.py` (Host + eZ80) |
| Sichtbeleg (Screenshot) | `uv run tools/run.py --dump --screenshot …`, Bilder nach `production/qa/evidence/` |

**Arbeitsweise:** Frage → Optionen → Entscheidung → Entwurf → Freigabe. `modes.automation: guided`: Kleinigkeiten selbst entscheiden, bei Umfangsänderungen, Löschungen und Schemaänderungen nachfragen. Keine Commits ohne Auftrag. Eine übersprungene Frage ist keine Antwort.

**Sprache:** Von Agenten geschriebene Dokumente auf Deutsch, Code und Kommentare auf Englisch.

**Sitzungsstand:** `production/session-state/active.md` ist der Checkpoint. Nach Kompaktierung, Absturz oder `/clear` zuerst lesen. Den fachlichen Stand führt weiterhin `docs/HANDOVER.md`.

**Referenz:** Die Spectrum-Portierung liegt als eigenes Repo in `../lords-of-chaos-zx-agon` und dient nur als Nachschlagewerk.

@.claude/docs/coordination-rules.md
