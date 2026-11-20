# ALSA-UCM-Profil für das Fireface 802 (Class-Compliant-Modus)

> **Stand: Debian-Paket `ff802-ucm-profile`, auf dem Host Version 0.3.0
> installiert (UCM-Dateien seit 0.1.0 unverändert).**
> **T1 ist bestanden** ([Lauf 2 am 2026-11-17](#t1-lauf-2-am-2026-11-17):
> 29/29 Prüfungen, Rückbau nachgewiesen). alsa-lib findet und wertet das
> Profil mit der echten Karte aus, wenn es geladen wird.
> **T2** (Messung der Kanalzuordnung, 2026-11-17) hat **nur Analog Out 1
> und 2** nachgewiesen; an Out 3, 5 und 7 wurde kein Signal erkannt, Out 4
> wurde abgebrochen, der Rest nicht gemessen. Das ist kein Nachweis eines
> Fehlers ([audio_t2_results/](audio_t2_results/README.md)). Die
> Markierung `FF802-CHANNEL-MAP: UNVERIFIED` in den Profildateien bleibt
> deshalb bestehen.
> Unabhängig davon sind die **Funktionstests von T3** erfolgreich: sechs
> Stereo-Ausgänge und ein Stereo-Eingang unter Kubuntu, Analog 3/4,
> Kopfhörer 9/10 und 11/12 mit realer Wiedergabe gehört (ein Hörtest über
> PipeWire, keine Kanalmessung). Die produktive Einbindung (Variante A1,
> Paket) beschreibt [audio_ucm_einbindung.md](audio_ucm_einbindung.md).
>
> Die Abschnitte „Risiken vor T2 und T3“ und die Hinweise „unbestätigt“
> in den Tabellen geben den Planungsstand vor diesen Tests wieder.

## Ziel

Unter KDE Plasma sollen sechs **eigenständig auswählbare** Stereo-Ausgabegeräte
und ein Stereo-Eingabegerät erscheinen:

| UCM-Gerät (Datei) | Name in alsa-lib | Bezeichnung (UCM-Comment) | Richtung | USB-Kanäle (unbestätigt) | Priorität |
|---|---|---|---|---|---|
| `Headphones 1` | `Headphones1` | Headphones Output 9/10 | Wiedergabe | 8, 9 | 255 (Standard) |
| `Headphones 2` | `Headphones2` | Headphones Output 11/12 | Wiedergabe | 10, 11 | 200 |
| `Line 1` | `Line1` | Analog Output 1/2 | Wiedergabe | 0, 1 | 192 |
| `Line 2` | `Line2` | Analog Output 3/4 | Wiedergabe | 2, 3 | 191 |
| `Line 3` | `Line3` | Analog Output 5/6 | Wiedergabe | 4, 5 | 190 |
| `Line 4` | `Line4` | Analog Output 7/8 | Wiedergabe | 6, 7 | 189 |
| `Line 5` | `Line5` | Analog Input 1/2 | Aufnahme | 0, 1 | 192 |

alsa-lib ab 1.2.15 normalisiert Gerätenamen nach `use-case.h`: Das
Leerzeichen vor der Nummer entfällt (`verb_normalize_device_names()` in
`src/ucm/parser.c`), und die Geräte werden nach Priorität sortiert.
`alsaucm`, PipeWire und WirePlumber sehen nur die Namen der zweiten Spalte.

AES/EBU, ADAT und die übrigen Eingänge erhalten keine eigenen Geräte. Sie
bleiben über das Profil `pro-audio` erreichbar. Die Geräte- und
Kommentarnamen folgen den RME-UCX-Profilen von upstream und sind englisch.

## Dateien

```
audio/ucm2/USB-Audio/conf.d/0424-3fdd.conf        Erkennung, wählt das Profil
audio/ucm2/USB-Audio/RME/Fireface-802.conf        Profil: Kanalbelegung, Use Case "HiFi"
audio/ucm2/USB-Audio/RME/Fireface-802-HiFi.conf   SplitPCM-Definitionen und 7 Geräte
tests/audio/check_ucm_profile.cmake               statische Prüfung (Struktur, Inhalt, Sicherheit)
tests/audio/test_ucm_syntax.c                     Syntaxprüfung mit dem Parser von alsa-lib
tests/audio/test_ucm_t1.sh                        simulierter Test des T1-Skripts und des Host-Wrappers
tests/audio/test_ucm_eval.sh                      T1 im Container mit der echten alsa-lib
tests/audio/fake_ctl_ff802.c                      nachgebildetes Steuergerät (Fireface 802) für alsa-lib
tests/audio/ucm_query.c                           nur lesender Ersatz für alsaucm (gleiches Ausgabeformat)
tests/audio/fixtures/t1/                          echte alsa-lib-Ausgaben, Antworten des Test-alsaucm
tests/audio/fail_with_reason.cmake                 lässt fehlende Pflicht-Tests fehlschlagen (FF802_REQUIRE_ALSA_TESTS)
tools/hw-test/ff802-ucm-t1.sh                     isolierter, nur lesender Host-Test T1
tools/hw-test/ff802-ucm-t1-host.sh                Host-Lauf von T1 mit Zustandsvergleich und garantiertem Rückbau
packaging/build-deb.sh                           baut das Paket ff802-ucm-profile (reproduzierbar)
packaging/ff802-ucm/                              Paketinhalt: Hauptdatei, ff802-ucm-bind, Maintainer-Skripte, Doku
tests/packaging/test_deb.sh                        Paket: Installation, Zuordnung, Upgrade, Entfernung in isolierter Wurzel
docs/audio_ucm_einbindung.md                     Entscheidungsvorlage: produktive Einbindung unter alsa-lib 1.2.15.3
docs/audio_t2_plan.md                             Hardwaretest T2 (Kanalzuordnung) mit ff802-chanmap
```

Der Verzeichnisaufbau unter `audio/ucm2/` entspricht dem von
`/usr/share/alsa/ucm2/`. So lassen sich die Dateien unverändert in eine
Testkopie, ein Paket oder einen Upstream-Beitrag übernehmen.

## Funktionsweise

Die folgenden Aussagen sind im Quellcode der Host-Versionen nachvollzogen:
alsa-ucm-conf 1.2.15.3, alsa-lib 1.2.15.3, PipeWire 1.6.2, WirePlumber 0.5.13.

### 1. Einbindung (alsa-ucm-conf, alsa-lib)

- `USB-Audio/USB-Audio.conf` (Syntax 8) enthält
  `If.opt.Append.Include.opt { File "/USB-Audio/conf.d/${var:USBID1}-${var:USBID2}.conf" Optional true }`.
- **alsa-lib, `ucm_cond.c`:** Ab Syntax 8 darf ein `If` ohne `Condition`
  stehen, wenn es einen `Append`-Block hat. Alle `If`-Blöcke werden der Reihe
  nach ausgewertet, und der `Append`-Inhalt wird sofort an Ort und Stelle
  ausgewertet (`uc_mgr_evaluate_inplace`: zuerst `Define`, dann `Include`).
  Ein `Define.ProfileName` in unserer `conf.d`-Datei stünde deshalb fest,
  **bevor** `If.inc` prüft, ob `ProfileName` leer ist („UCM is not supported“).
- **Fehler in alsa-lib < 1.2.16 (auch Ubuntu 1.2.15.3-1ubuntu1.1):** Bei
  `Optional true` prüft `include_eval_one()` in `ucm_include.c` die Datei mit
  `access(s, R_OK)` auf dem **unaufgelösten** Pfad
  `/USB-Audio/conf.d/0424-3fdd.conf`. Dieser Pfad ist relativ zum
  UCM-Verzeichnis gemeint, `access()` sucht ihn aber im Dateisystem. Die Datei
  wird deshalb nie geladen, für kein Gerät, auch nicht mit `ALSA_CONFIG_UCM2`.
  Behoben ist das upstream mit Commit `62c24074` „ucm: fix optional include“
  (2026-03-18, ab alsa-lib 1.2.16). Ubuntu hat den Fix nicht zurückportiert.
  Nachgewiesen im Container mit der Host-Bibliothek: Ein vorhandenes
  `Optional`-Include wird übersprungen, ohne `Optional` wird es geladen.
- **Folge:** Der `conf.d`-Weg funktioniert auf dem Host (alsa-lib 1.2.15.3)
  nicht. T1 fügt deshalb **nur in seiner privaten Kopie** von
  `USB-Audio.conf` vor `If.inc` einen Block ein, der unsere `conf.d`-Datei
  ohne `Optional` lädt, abgesichert über `${CardComponents}` „USB0424:3fdd“
  (siehe `insert_loader` in `ff802-ucm-t1.sh`). Die Profildateien selbst
  bleiben unverändert. Mit einer korrigierten alsa-lib wird die Datei zweimal
  ausgewertet, das ist harmlos.
- **Zusätzliche Absicherung:** Die `conf.d`-Datei setzt `ProfileName` nur, wenn
  `${CardLongName}` „Fireface 802“ enthält und nicht „802 FS“. Passt der Name
  nicht, bleibt alles wie bisher, denn alsa-lib meldet dann „UCM is not
  supported“.
- **Vorbild:** Das Fireface UCX meldet sich im CC-Modus ebenfalls mit der
  Microchip-ID (`USB0424:3fb9`).

### 2. SplitPCM

- `Fireface-802-HiFi.conf` definiert mit `Macro.SplitPCM` zwei Aufteilungen
  des 22-Kanal-Geräts: `ff802_stereo_out` und `ff802_stereo_in`. Jedes
  UCM-Gerät wählt mit `Macro.pcm_split.SplitPCMDevice` zwei Kanäle aus.
- **Mit SplitPCM-Unterstützung der Anwendung** (Präfix `<<<SplitPCM=1>>>`)
  liefert UCM `PlaybackPCM "hw:<Karte>,0"`, `PlaybackChannels 22` und die
  Kanalindizes. Die Anwendung teilt dann selbst auf. **Ohne** diese
  Unterstützung verwendet alsa-lib die `dshare`- bzw. `dsnoop`-Geräte
  `ff802_stereo_out:…`.
- Es gibt keine Einschränkung der Abtastrate: Nach `/proc/asound/card1/stream0`
  stehen 22 Kanäle bei allen Raten bis 192 kHz zur Verfügung.

### 3. Sechs Geräte statt sechs Ports (PipeWire-ACP, WirePlumber)

- **ACP, `alsa-ucm.c`:** Jedes UCM-Gerät erhält ein eigenes Mapping
  (`"Mapping HiFi: <Gerät>: sink"`, mit dem normalisierten Namen, also
  z. B. `HiFi: Headphones1: sink`) und wird damit zu einer eigenen Senke bzw.
  Quelle.
- **Ohne SplitPCM** dürften Geräte mit demselben `PlaybackPCM` nicht
  gleichzeitig in einem Profil stehen. Dann wäre immer nur ein Ausgang aktiv,
  umschaltbar wie ein Port. **Mit SplitPCM** gilt eine Ausnahme im Code
  („…except when both split“). Die Profilbildung nimmt die größtmögliche
  Gerätegruppe. Da es keine `ConflictingDevice` gibt, entsteht **ein**
  HiFi-Profil mit allen sieben Geräten gleichzeitig.
- **WirePlumber, `monitors/alsa.lua`:** setzt `api.alsa.split-enable = true`
  und erzeugt je Split-Gerät
  - eine sichtbare `Audio/Sink` (bzw. `Audio/Source`) mit eigener
    Software-Lautstärke. Das ist die Seite, die KDE anzeigt.
  - einen internen Stream (`Stream/Output/Audio/Internal`,
    `audio.position [AUXn, AUXn+1]`) zu
  - **einem** verborgenen Hardwareknoten (`Audio/Sink/Internal`, 22 Kanäle).
- Beim Entfernen des Geräts entfernt `removeNode` alle diese Objekte wieder.
  Die Ausgänge existieren nur, solange das Fireface angeschlossen ist.
- Das Zuordnen von Anwendungen und die Lautstärke je Ausgang sind damit
  vorhandene Funktionen von PipeWire und KDE. Eigener Laufzeitcode ist nicht
  nötig.

### 4. Standardausgang

- `Headphones 1` hat die höchste `PlaybackPriority` des Profils (255). Ein
  Test stellt sicher, dass das so bleibt.
- **Offen:** ob dieser Wert die Priorität anderer Karten übersteigt und ob
  WirePlumber ihn tatsächlich als Standard wählt. Das bisher gewählte
  Standardgerät (Stereo-Profil) verschwindet mit dem Profilwechsel.
- Eine zusätzliche WirePlumber-Regel wird **nur** entwickelt, wenn T3 zeigt,
  dass sie nötig ist. Ausschlaggebend sind dafür die `priority.session`-Werte
  aller Senken in `pw-dump`.

## Prüfstatus

| Prüfung | Ort | Status |
|---|---|---|
| `ucm_profile_static` | Container | ✓ bestanden. Prüft gegen eine eigene Erwartungstabelle: Geräte, Richtungen, Kanalpaare, Kommentare; `HWChannels 22` und 22 FL/FR-Positionen in beiden SplitPCM; eindeutige Kanäle; `Headphones 1` mit der höchsten Priorität; Absicherung gegen 802 FS; genau ein Use Case; Kennzeichnung der Kanalzuordnung; keine `cset`/`exec`/Sequenzen/Mixer-Elemente/Raten/Konflikte/Digitalgeräte; kein Schreibfehler `PlackbackPriority`. Zwölf eingebaute Fehler wurden alle erkannt. |
| `ucm_alsa_syntax` | Container | ✓ bestanden (alsa-lib 1.2.15.3: drei Dateien geparst, 7 Geräte) |
| `ucm_alsa_eval` | Container | ✓ bestanden mit dem UCM-Baum von alsa-ucm-conf 1.2.15.3-1ubuntu1.7 (siehe unten). 27 Prüfungen: das unveränderte T1-Skript besteht mit der echten alsa-lib, mit Lade-Block und mit Paketaufbau; der alsa-lib-Fehler wird als Gegenprobe nachgewiesen; 802 FS und andere RME-Geräte wählen das Profil nicht; `_alibcfg` ist gültig (dshare/dsnoop, 22 Positionen); die Vorlagen unter `fixtures/t1` stimmen mit der echten Ausgabe überein. Ohne Systembaum wird der Test übersprungen; im Dev-Container ist er **verpflichtend** (siehe unten). |
| `ucm_t1_script` | Container | ✓ bestanden. 76 Prüfungen, auch mit Paketaufbau (`prepare --package`), mit nachgebautem UCM-Verzeichnis, nachgebautem procfs und einem `alsaucm`, das echte alsa-lib-Ausgaben liefert. Dazu kommen die Exit-Codes des Host-Wrappers, der Rückbau nach Fehlern und nach Ctrl+C sowie die Sperre gegen gleichzeitige Läufe. |
| UCM-Auswertung mit echter Karte | Host, T1 | ✓ **bestanden** in Lauf 2 am 2026-11-17 (29/29). Lauf 1 war wegen des alsa-lib-Fehlers nicht bestanden. In beiden Läufen ist der Rückbau nachgewiesen. |
| `deb_package` | Container | ✓ bestanden. 49 Prüfungen: reproduzierbarer Bau, genauer Paketinhalt, keine Überschneidung mit dem Ubuntu-Baum; Installation in isolierter Wurzel mit Platzhalter-Paketen; Zuordnung und alle Verweigerungen; der installierte Baum liefert mit der echten alsa-lib die Ausgabe von T1-Lauf 2; Upgrade, Entfernung, Purge, Aufräumen ohne Bind-Werkzeug. |
| `ff802-chanmap` (12 Tests) | Container | ✓ bestanden. Simulation (20 Szenarien), Signal und Wächter, Kartenerkennung, Reservierung, Auswertung, ALSA-Backend am `null`-Plugin, Kommandozeile, Quelltext- und Symbolprüfung; siehe [audio_t2_plan.md](audio_t2_plan.md) |
| physische Kanalzuordnung | Host, T2 | ausgeführt am 2026-11-17: nur Analog Out 1 und 2 nachgewiesen, übrige Ausgänge offen ([audio_t2_results/](audio_t2_results/README.md)) |
| Geräte in PipeWire/KDE, Prioritäten, Lautstärken | Host, T3 | funktional erfolgreich, Portwechsel `verify-03` bestanden ([audio_t3.md](audio_t3.md), [audio_t3_usb_port.md](audio_t3_usb_port.md)) |

**Verpflichtende ALSA-Tests im Dev-Container:** CTest zählt einen
übersprungenen Test nicht als Fehler und meldet trotzdem „100% tests
passed“. Der Dev-Container setzt deshalb die Umgebungsvariable
`FF802_REQUIRE_ALSA_TESTS=ON` (`devcontainer.json`). Dann **schlagen**
`ucm_alsa_syntax`, `ucm_alsa_eval`, `deb_package`, `test_chanmap_alsa_null`
und `chanmap_binary_symbols` mit einer Begründung fehl, wenn
`libasound2-dev`, alsa-lib 1.2.15.x, der UCM-Baum (`alsa-ucm-conf`) oder
`dpkg-deb` fehlen. Ist die Variable gesetzt, überschreibt sie den CMake-Cache. Auch ein
Build-Verzeichnis, das vor dem Neuaufbau mit `OFF` konfiguriert wurde,
übernimmt sie deshalb. Prüfen nach dem Neuaufbau:

```sh
cmake -S . -B build && cmake --build build
ctest --test-dir build --output-on-failure
# erwartet: alle "Passed", kein "Skipped" (ctest listet übersprungene Tests am Ende)
```

Ohne die Variable, etwa außerhalb des Containers, bleibt es beim
Überspringen. Mit `-DFF802_REQUIRE_ALSA_TESTS=ON` lässt sich die Pflicht auch
dort einschalten.

**Vorlagen und Version von alsa-ucm-conf:** `ucm_alsa_eval` vergleicht die
echte Ausgabe mit `tests/audio/fixtures/t1`. Bringt ein neueres
alsa-ucm-conf im Container eine andere Ausgabe für das Fireface, schlägt der
Test fehl. Das ist gewollt, denn der Host sähe dieselbe Änderung. Die
Vorlagen werden dann nach `fixtures/t1/README` neu erzeugt und begutachtet.

**Feste Locale (`LC_ALL=C`):** `test_ucm_eval.sh` setzt für sich und alle
Kindprozesse `LC_ALL=C`. Mehrere Prüfungen vergleichen Werkzeugausgaben mit
festem Text, etwa die sortierte Ausgabe von `diff -rq` zwischen Systembaum
und Overlay. Ubuntu 26.04 bringt `sort` aus rust-coreutils (uutils 0.10.0)
mit. Dieses `sort` sortiert bei jedem Locale-Namen außer C/POSIX nach
Sprachregeln, auch wenn die Locale im Container gar nicht installiert ist.
GNU sort fiel in diesem Fall auf die Bytereihenfolge zurück. Mit einem vom
Terminal geerbten `LANG=de_DE.UTF-8` (VS Code setzt das je nach
Anzeigesprache) stand dann `Only in …/USB-Audio: conf.d` vor
`Only in …/USB-Audio/RME: …`, und die Prüfung „overlay differs from the
system tree only in USB-Audio.conf and the profile“ schlug fehl. Der Inhalt
des Overlays war dabei korrekt, und alsa-ucm-conf 1.2.15.3-1ubuntu1.7 war
nicht beteiligt. Nachstellen lässt sich der Fehler ohne die Korrektur mit
`LC_ALL=de_DE.UTF-8 ctest --test-dir build -R ucm_alsa_eval`.

**Auswertung im Container (`ucm_alsa_eval`):** Ein externes Steuer-Plugin
(`fake_ctl_ff802.c`) stellt eine Karte mit den Kenndaten des Hosts dar:
Treiber `USB-Audio`, ID `F80224047128`, der Langname von T1 und die
Komponenten `USB0424:3fdd`. Es wird nur über eine private `ALSA_CONFIG_PATH`
der Testprozesse als `ctl.hw` eingebunden. Damit wertet die echte alsa-lib
den vollständigen UCM-Baum aus, mit `USB-Audio.conf`, Bedingungen, Includes,
Makros und SplitPCM, und zwar bis genau zu dem Punkt, an dem die Kartendaten
gelesen werden. `ucm_query.c` ersetzt `alsaucm` mit dessen Ausgabeformat
(alsa-utils 1.2.15.2). Das Plugin hängt vom internen Layout von alsa-lib
1.2.15.x ab und verweigert andere Versionen.

Den Ubuntu-Baum ohne Neuaufbau des Containers verwenden:
`cmake -S . -B build -DFF802_SYSTEM_UCM2=<Pfad zu ucm2>`. Den Baum
liefert `git clone https://git.launchpad.net/ubuntu/+source/alsa-ucm-conf`,
Tag `applied/1.2.15.3-1ubuntu1.7`.

**Grenzen der Container-Auswertung:** Sie ersetzt T1 nicht. Ungeprüft
bleiben die echten Kartendaten (Langname, Komponenten, ID), die
Steuerelemente der Karte und das Verhalten des echten `alsaucm`.

## Host-Test T1 (isoliert, nur lesend)

**Zweck:** Prüfen, ob alsa-lib das Profil mit der echten Karte findet und
auswertet. Dabei werden keine Systemdateien geändert, kein PCM geöffnet,
kein Ton ausgegeben, und PipeWire bleibt unberührt.

Voraussetzungen: Fireface im CC-Modus angeschlossen, `alsaucm` vorhanden
(Paket `alsa-utils`). Als normaler Benutzer ausführen, nicht als Root.

Empfohlen ist der vollständige Lauf mit Zustandsvergleich und garantiertem
Rückbau:

```sh
cd <Projekt>                            # Checkout auf dem Host
tools/hw-test/ff802-ucm-t1-host.sh      # Ausgaben in build-t1/ (git-ignoriert)
echo $?
```

Vor einer Installation des Pakets prüft dieselbe Prüfung nur lesend dessen
Aufbau: Die Paketdateien und die Hauptdatei kommen unter dem Langnamen der
angeschlossenen Karte in die Testkopie, `USB-Audio.conf` bleibt unverändert.

```sh
cmake --build build --target deb          # build/deb/ff802-ucm-profile_0.2.0_all.deb
tools/hw-test/ff802-ucm-t1-host.sh --package build/deb/ff802-ucm-profile_0.2.0_all.deb
```

Der Wrapper geht so vor:
- Er hält vorher den Zustand fest: Prüfsummen von `/usr/share/alsa`,
  `/etc/alsa`, `~/.asoundrc`, `~/.config/alsa`, der PipeWire- und
  WirePlumber-Konfiguration in `/etc` und `~/.config`, `/etc/udev/rules.d`
  und `environment.d`, dazu die Umgebung der Benutzersitzung.
- Er ruft `prepare` und `run` auf.
- Er führt `clean` **immer** aus, auch bei Fehlern oder Abbruch, über einen
  EXIT-Trap.
- Er vergleicht danach den Zustand mit dem Ausgangszustand.

| Exit-Code | Bedeutung |
|---|---|
| 0 | T1 bestanden, Rückbau nachgewiesen |
| 1 | T1-Prüfungen fehlgeschlagen, Rückbau nachgewiesen |
| 2 | `prepare` fehlgeschlagen oder Umgebungsfehler, Rückbau nachgewiesen |
| 3 | kein Fireface 802 im CC-Modus gefunden, Rückbau nachgewiesen |
| 4 | **Rückbau nicht nachgewiesen** (`clean` gescheitert oder Zustand verändert), sofort prüfen |
| 5 | ein anderer Lauf des Wrappers ist aktiv; es wurde nichts getan (Sperre per `flock` auf das Skript selbst, ohne Sperrdatei) |
| 130 | abgebrochen (Ctrl+C oder Signal), Rückbau nachgewiesen, kein Ergebnis |

Die Einzelschritte bleiben verfügbar:

```sh
tools/hw-test/ff802-ucm-t1.sh prepare   # private Kopie in ~/.cache/ff802-ucm-t1
tools/hw-test/ff802-ucm-t1.sh run       # nur lesende alsaucm-Abfragen
tools/hw-test/ff802-ucm-t1.sh clean     # vollständiger Rückbau
```

**`prepare`:**
- kopiert `/usr/share/alsa/ucm2` nach `~/.cache/ff802-ucm-t1/ucm2` und ergänzt
  die drei Profildateien
- fügt in die private Kopie von `USB-Audio/USB-Audio.conf` direkt vor `If.inc`
  den Block `If.ff802-t1` ein. Er lädt die `conf.d`-Datei ohne `Optional`,
  wenn `${CardComponents}` „USB0424:3fdd“ enthält, und umgeht so den
  alsa-lib-Fehler (siehe [Einbindung](#1-einbindung-alsa-ucm-conf-alsa-lib)).
  Das Manifest vermerkt die Änderung.
- bricht ab, wenn das System eine dieser Dateien bereits mitbringt, wenn der
  `conf.d`-Mechanismus fehlt, wenn `If.inc` nicht genau einmal vorkommt oder
  wenn eine der beteiligten Dateien Steuerbefehle enthält (`cset`, `exec`,
  Boot- oder Default-Sequenzen)

**`run`:**
- ermittelt die einzige Karte mit `usbid` `0424:3fdd`
- liest deren Langnamen aus der zweiten Zeile von `/proc/asound/cards` und
  prüft ihn
- führt die folgenden Abfragen aus und legt die Ausgaben unter
  `~/.cache/ff802-ucm-t1/results/` ab:
  - `alsaucm -c hw:N list _verbs` mit `ALSA_CONFIG_UCM2=/usr/share/alsa/ucm2`
    (unveränderter System-Baum), als Vergleich
  - mit `ALSA_CONFIG_UCM2=<Testkopie>`, gesetzt nur für die jeweiligen
    `alsaucm`-Prozesse: `list _verbs`, `list _devices/HiFi`, `dump text`
    sowie `dump text` mit Präfix `<<<SplitPCM=1>>>`
- `alsaucm` öffnet dabei nur das Steuergerät der Karte zum Lesen. In
  alsa-lib 1.2.15.3 führt `snd_use_case_mgr_open` keine Boot- oder
  Default-Sequenzen aus. `set`, `reset`, `_boot` und `_verb` verwendet das
  Skript nicht.
- **Prüfungen (29):**
  - Langname
  - Vergleich ohne Testkopie meldet „UCM is not supported“
  - alle vier Abfragen enden mit Exit 0, ohne Fehler oder Warnungen von ALSA
  - `HiFi` ist das einzige Verb
  - genau 7 Geräte
  - je Gerät:
    - Kommentar
    - ohne SplitPCM: `…PCM` = `ff802_stereo_out|in:<Karte>,0,<k0>,<k1>`,
      2 Kanäle, Priorität
    - mit SplitPCM: `…PCM` = `hw:<Karte>,0`, 22 Kanäle, `…Channel0/1` =
      Kanalpaar, Positionen FL/FR

**Rückbau:** `clean` löscht `~/.cache/ff802-ucm-t1`, aber nur, wenn das
Verzeichnis die Markierungsdatei des Skripts enthält. Sonst wurde nichts
verändert.

**Wenn `run` scheitert:** Die Ergebnisdateien zeigen, ob es am Langnamen
(Absicherung in `conf.d`), an der Syntax oder an den Werten liegt.
`card.txt` enthält den erkannten Langnamen.

### T1-Lauf 1 am 2026-11-17

- **Umgebung:** Kubuntu 26.04.1, Kernel 7.0.0-43, alsa-lib
  1.2.15.3-1ubuntu1.1, alsa-ucm-conf 1.2.15.3-1ubuntu1.7, alsa-utils
  1.2.15.2. Fireface 802 als `0424:3fdd`, Karte `hw:1` mit ID
  `F80224047128` und Langname
  „RME Fireface 802 (24047128) at usb-0000:00:14.0-2.4.1.1.3, high speed“.
- **Ergebnis:** `prepare` erfolgreich. `run` mit 14 von 15 Prüfungen
  fehlgeschlagen: kein Verb, keine Geräte, kein Dump. `clean` erfolgreich,
  Zustandsvergleich `IDENTICAL`. Die Protokolle liegen in
  `build-t1/run1-2026-11-17/`.
- **Primärfehler:** Alle vier Abfragen mit Testkopie lieferten dieselbe
  Meldung wie der Vergleich ohne Testkopie, nämlich „UCM is not supported for
  this USB device (… @ USB0424:3fdd)“. Unsere `conf.d`-Datei wurde nie
  geladen, wegen des Fehlers mit `Optional`-Includes in alsa-lib
  < 1.2.16. `ALSA_CONFIG_UCM2` wirkte dagegen korrekt. Im Container ist die
  Meldung mit derselben Bibliothek Zeichen für Zeichen nachgestellt.
- **Folgefehler:** Alle 14 fehlgeschlagenen Prüfungen. Profil, Makros und
  SplitPCM wurden gar nicht erst ausgewertet.
- **Verdeckte Fehler im T1-Skript, nach der Korrektur sichtbar geworden:**
  1. Die Geräteprüfung erwartete `Headphones 1`, alsa-lib liefert aber
     `Headphones1`.
  2. Als Langname wurde die Kurzzeile gelesen („USB-Audio - Fireface 802
     (24047128)“). Die Prüfung war nur zufällig richtig.
  3. Der damalige Wrapper (`build-t1/run-t1-host.sh`) endete auch bei
     fehlgeschlagenem T1 mit Exit 0.

  Alle drei sind behoben, `ucm_t1_script` prüft sie.

### T1-Lauf 2 am 2026-11-17

- **Lauf:** 10:05:56 UTC, Wrapper `ff802-ucm-t1-host.sh`.
- **Umgebung:** unverändert gegenüber Lauf 1 (Paketversionen, Karte `hw:1`,
  ID `F80224047128`, Langname).
- **Code-Stand:** `15d1d5c` plus 11 nicht committete Änderungen. Die Prüfsummen
  der drei Profildateien im Manifest stimmen mit dem Repository überein
  (`conf.d` 2952150003, `Fireface-802.conf` 4137258444,
  `Fireface-802-HiFi.conf` 3561808635). Alle Dateien wurden vor dem Lauf
  zuletzt geändert. Die Testkopie enthielt den Block `If.ff802-t1`.
- **Ergebnis: 29 von 29 Prüfungen bestanden, Exit 0.**
  - Der Langname wurde jetzt korrekt aus der zweiten Zeile gelesen.
  - Der Vergleichslauf mit dem unveränderten System-Baum meldet „UCM is not
    supported“. Ohne unser Profil ändert sich am System also nichts.
  - Alle vier Abfragen mit der Testkopie enden mit Exit 0, ohne Fehler oder
    Warnungen von ALSA.
  - `HiFi` ist das einzige Verb, mit genau 7 Geräten.
  - Je Gerät stimmen Kommentar, PCM, Kanalpaar, Kanalzahl und Priorität mit
    dem Profil überein, ohne SplitPCM ebenso wie mit SplitPCM (22
    Hardwarekanäle, Positionen FL/FR).
- **Abgleich mit der Container-Auswertung:** `verbs`, `devices`, `dump`,
  `dump-split` und `baseline-verbs` vom Host (echtes `alsaucm` 1.2.15.2) sind
  **byte-gleich** mit `tests/audio/fixtures/t1`. Diese Dateien stammen aus
  `ucm_alsa_eval` mit nachgebildeter Karte. Die Container-Auswertung bildet
  den Host also exakt nach, und die Vorlagen sind nun auch als
  Host-Ergebnis belegt.
- **Rückbau:** `clean` endete mit Exit 0, `~/.cache/ff802-ucm-t1` ist
  entfernt, und der Zustandsvergleich ergab `IDENTICAL` über 917
  Prüfsummenzeilen. Erfasst waren `/usr/share/alsa`, `/etc/alsa`,
  `/etc/udev/rules.d`, die Konfiguration von PipeWire und WirePlumber
  (beide nicht vorhanden) und die Umgebung der Sitzung (keine
  ALSA-/PipeWire-Variablen). Die Dateien in `~/.local/state/wireplumber/`
  haben ihre Zeitstempel von vor dem Test behalten. PipeWire und WirePlumber
  liefen vorher und nachher.
- **Nicht geprüft** (gehört nicht zu T1): physische Kanalzuordnung (T2) und
  das Verhalten in PipeWire und KDE (T3).
- **Protokolle:** `build-t1/` (git-ignoriert), Lauf 1 unter
  `build-t1/run1-2026-11-17/`.

## Hardwaretest T2: Kanalzuordnung

T2 prüft, welcher USB-Kanalindex an welcher Buchse ankommt. Die Prüfung
läuft über eine **elektrische Schleife** (Ausgang → Patchkabel →
Line-Eingang). Dabei sind keine Lautsprecher und keine Kopfhörer
angeschlossen, der Pegel ist im Code auf höchstens −40 dBFS begrenzt, und
die Karte wird über die Geräte-Reservierung von PipeWire freigegeben, ohne
Profilwechsel und ohne geänderte Konfiguration. Planung, Schutzmaßnahmen und
offene Entscheidungen: [audio_t2_plan.md](audio_t2_plan.md).

**Ergebnis (2026-11-17):** nachgewiesen sind nur Wiedergabe-Index 0 →
Analog Out 1 und Index 1 → Analog Out 2 (an Line In 1). Originalprotokolle
und Grenzen der Aussagekraft: [audio_t2_results/](audio_t2_results/README.md).

Die Nummerierung wurde am 2026-11-17 getauscht. Der frühere „T3,
Kanalzuordnung“ ist jetzt **T2**, der frühere „T2, Sitzungstest“ ist jetzt
**T3**. Der Sitzungstest baut damit auf einer bestätigten Kanalzuordnung auf.

## Lautstärkesicherheit vor T3

T3 aktiviert das Profil in PipeWire. Danach erscheinen sechs neue Senken,
und WirePlumber vergibt Startlautstärken. Wir verlassen uns nicht auf deren
Standardwerte. Vor T3 gilt diese Reihenfolge:

1. **Physisch:**
   - beide Kopfhörerregler am Fireface ganz nach links
   - Kopfhörer abnehmen
   - Endstufen bzw. Aktivlautsprecher an Analog 1–8 ausschalten oder abklemmen
2. **Keine laufende Wiedergabe:** Alle Wiedergaben beenden (Browser, Player).
   WirePlumber verschiebt Streams ohne festes Ziel zum neuen Standardgerät. Ein
   laufender Stream könnte also nach der Aktivierung sofort auf
   `Headphones1` landen.
3. **Ausgangslage festhalten (nur lesend):** `wpctl status`, `wpctl settings`
   (darunter `device.routes.default-sink-volume`), Kopie von
   `~/.local/state/wireplumber/` für den Rückbau.
4. **Direkt nach der Aktivierung, vor jeder Wiedergabe:**
   - Alle sechs neuen Senken stummschalten und auf 0 setzen. Die Objekt-IDs
     werden dabei zur Laufzeit über den `node.name` aus `pw-dump` ermittelt,
     es gibt keine festen IDs.
   - Mit `wpctl get-volume` kontrollieren, dass jede Senke `[MUTED]` meldet.
   - Den verborgenen Hardwareknoten zusätzlich stummschalten. Er ist ein
     Hauptschalter für alle Ausgänge, und eine Stummschaltung dort kann nichts
     lauter machen.
5. **Hörprobe in T3** (nur wenn gewünscht, die Zuordnung ist dann aus T2
   bekannt):
   - Testsignal mit höchstens −40 dBFS. Die Datei wird im Container erzeugt,
     und der niedrige Pegel gilt unabhängig von der Software-Lautstärke.
   - Pro Schritt wird genau eine Senke freigegeben, mit niedriger
     Lautstärke, und nur in kleinen Schritten erhöht.
   - Die Kopfhörerregler am Gerät werden erst danach behutsam aufgedreht.
6. **Nach dem Test:** Die gespeicherten Lautstärken in WirePlumber kontrollieren
   bzw. die gesicherte Kopie zurückspielen.

Die Schritte 4 bis 6 werden vor T3 als eigenes Skript ausgearbeitet und
gesondert freigegeben.

## Ausblick T3: Sitzungstest

- **Aufbau:** `ALSA_CONFIG_UCM2` wird nur für die Benutzerdienste `pipewire`
  und `wireplumber` gesetzt, danach werden beide neu gestartet. Die Testkopie
  erzeugt `ff802-ucm-t1.sh prepare`, wegen des alsa-lib-Fehlers nur mit dem
  Lade-Block. Alternativ prüft T3 gleich den Aufbau der vorgesehenen
  produktiven Einbindung, siehe
  [audio_ucm_einbindung.md](audio_ucm_einbindung.md).
- **Unvermeidlich:** Das Kartenprofil wechselt auf HiFi.
- **Geprüft werden:** sieben Geräte (`HiFi: Headphones1: sink` …), die Namen
  in KDE, Prioritäten und Standardausgang, Startlautstärken und die
  Speicherung nach erneutem Anschließen.
- **Rückbau:** Variable entfernen, gesicherten WirePlumber-Zustand
  zurückspielen, Dienste neu starten.

## Paketierung und Upstream

- **Produktive Einbindung unter alsa-lib 1.2.15.3:** Der `conf.d`-Weg wirkt erst
  ab alsa-lib 1.2.16. Die Optionen, darunter die im Container nachgewiesene
  Hauptdatei für den Langnamen der Karte (nur neue Dateien), stehen mit
  Bewertung und Empfehlung in
  [audio_ucm_einbindung.md](audio_ucm_einbindung.md).
- **Upstream (alsa-ucm-conf):** Statt `conf.d` käme ein Eintrag in
  `USB-Audio.conf` dazu, analog zum UCX:
  ```
  If.fireface-802 {
  	Condition {
  		Type String
  		Haystack "${CardComponents}"
  		Needle "USB0424:3fdd"
  	}
  	True.Define.ProfileName "RME/Fireface-802"
  }
  ```
  Dazu kämen `RME/Fireface-802.conf` und `RME/Fireface-802-HiFi.conf`. Erst
  nach T2 und mit entfernter Markierung `UNVERIFIED` sinnvoll.
- **Upstream-Hinweis:** Die vorhandenen RME-UCX-Profile schreiben
  `PlackbackPriority`. Diese Prioritäten werden deshalb ignoriert. Unser
  Profil verwendet `PlaybackPriority`, und der statische Test verhindert den
  Schreibfehler.

## Risiken vor T2 und T3

1. **Kanalzuordnung unbestätigt** (Gegenstand von T2). Im CC-Modus kann der im
   Gerät gespeicherte Mixer-Zustand (TotalMix) die Zuordnung beeinflussen.
   Ein solcher Mixer kann in T2 auch eine Rückkopplung erzeugen; dagegen
   sind Abbruchschwellen vorgesehen, siehe [audio_t2_plan.md](audio_t2_plan.md).
2. **Automatischer Profilwechsel:** Sobald ein UCM-Profil für die Karte
   wirksam ist, entfallen die bisherigen Profile, und WirePlumber wählt HiFi.
   Laufende Streams können umziehen.
3. **Standardausgang:** Die UCM-Priorität 255 reicht möglicherweise nicht
   gegenüber anderen Karten.
4. **Speicherung der Lautstärke** der Split-Senken über Neustarts ist
   erwartet, aber nicht nachgewiesen. Ebenso unbelegt ist der Startwert neuer
   Routen.
5. **Zugriff auf SPDIF/ADAT** neben dem HiFi-Profil (über die Ports des
   verborgenen Hardwareknotens) ist unbelegt. Sicher möglich ist er über das
   Profil `pro-audio`, dann allerdings ohne die sechs Stereo-Geräte.
6. **Langname der Karte:** Enthält er nicht „Fireface 802“, greift das Profil
   nicht. Dieser Fehlerfall ist harmlos; T1 deckt ihn auf. In beiden
   Host-Läufen enthielt er „Fireface 802“ und kein „802 FS“.
7. **Einbindung:** Ohne den T1-Ladeblock lädt alsa-lib 1.2.15.3 das Profil
   nicht (siehe [Einbindung](#1-einbindung-alsa-ucm-conf-alsa-lib)). Jede
   Testkopie für T3 muss deshalb von `ff802-ucm-t1.sh prepare` erzeugt
   werden. T2 braucht kein UCM.
8. **Gerätenamen:** PipeWire, WirePlumber und gespeicherte Zustände verwenden
   die normalisierten Namen (`Headphones1` …). Erwartungen in T3 müssen sich
   darauf beziehen.
9. **Geräte-Reservierung in T2:** Während T2 verschwindet das Fireface aus
   PipeWire. Streams ziehen auf andere Ausgänge um, möglicherweise auf die
   Laptop-Lautsprecher. Deshalb vorher alle Audioprogramme beenden.
