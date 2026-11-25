# Entwicklung: Aufbau, Build, Tests, Entwicklungsphasen

Dieses Dokument enthält die technischen Angaben für Entwickler, die bis
Version 0.4.1 in der README standen. Die README ([englisch](../README.md),
[deutsch](../README.de.md)) erklärt das Projekt aus Anwendersicht.

Im Standard-Build kann **kein Programm Daten an das Gerät senden**. Die
Schreibfunktion wird nur mit `-DFF802_ENABLE_USB_WRITE=ON` einkompiliert
(siehe [usb_comm.md](usb_comm.md)). Das Paket `ff802-cc-auto` enthält
einen so gebauten Dienst; seine Automatik ist nach der Installation aus
(siehe [t4_usb_mode.md](t4_usb_mode.md)). Das Paket `ff802-cc` ist
ebenfalls so gebaut (siehe [packages.md](packages.md)).

## Entwicklungsphasen und Stand

Die Testphasen T1–T4 bezeichnen die Hardwaretests am Gerät:
T1 Laden des UCM-Profils durch alsa-lib, T2 Kanalzuordnung,
T3 Integration in PipeWire/WirePlumber/Plasma, T4 USB-Modusverwaltung.

| Phase | Inhalt | Stand |
|-------|--------|-------|
| 1  | Kodierung der Registerbefehle und Tests | abgeschlossen |
| 2a | Diagnoseprogramm `ff802-detect`: lesende Geräteerkennung | abgeschlossen, auf dem Host getestet |
| 2b | Umschaltprogramm `ff802-cc` mit Diagnosemodus | abgeschlossen; `enable` mit Port-Reset und `disable` am 2026-11-19 am Gerät erfolgreich getestet; Paket `ff802-cc` **0.4.0 auf dem Host installiert** (gebaut aus Commit `e12a1d0`, siehe [packages.md](packages.md#v040)) |
| 3 | ALSA-UCM-Profil mit sechs Stereo-Ausgängen für den CC-Modus, Debian-Paket `ff802-ucm-profile` (T1, T3) | T1 bestanden, T3 funktional erfolgreich getestet (sechs Stereo-Ausgänge, ein Stereo-Eingang; Analog 3/4, Kopfhörer 9/10 und 11/12 mit realer Wiedergabe); Version **0.3.0 auf dem Host installiert**, automatischer USB-Portwechsel erfolgreich getestet (2026-11-18, [audio_t3_usb_port.md](audio_t3_usb_port.md)) |
| 4 | Messwerkzeug `ff802-chanmap` für die Kanalzuordnung (Hardwaretest T2) | Werkzeug fertig; T2 am 2026-11-17 ausgeführt, **nur Analog Out 1 und 2 nachgewiesen** (Out 3, 5, 7 ohne erkanntes Signal, Out 4 abgebrochen, Rest nicht gemessen; kein Nachweis eines Fehlers). Protokolle und Auswertung: [audio_t2_results/](audio_t2_results/README.md). Die Markierung `FF802-CHANNEL-MAP: UNVERIFIED` im Profil bleibt |
| T4 | Automatischer Wechsel in den CC-Modus beim Anschließen/Systemstart, Debian-Paket `ff802-cc-auto`; Untersuchung `ff802-cc disable` | 0.1.x: Umschaltung mit Umstecken, am Gerät getestet; 0.2.0: automatischer Port-Reset (wie `ff802-cc enable`), am Gerät getestet 2026-11-23 ([t4_port_reset.md](t4_port_reset.md)); `disable` ist Teil von `ff802-cc` |
| M | Mixer-Initialisierung im CC-Modus über den ALSA-Sequencer (Paket `ff802-cc-auto` 0.2.1, `ff802-mixer-init`) | Hardwaretests B.2–B.4, D1–D3, E1; Konfiguration `ff802-mixer-init-v3` mit 38 Registern, Integrationstest mit dev6 bestanden (2026-11-23, [mixer_v3_integration.md](mixer_v3_integration.md)); v4 mit vollständiger 12×12-Playback-Matrix (168 Register), Hardwaretest mit `0.2.1~dev1` bestanden (2026-11-25, [mixer_v4.md](mixer_v4.md)) |

Release v0.5.0 (in Vorbereitung): Pakete `ff802-cc` 0.5.0,
`ff802-cc-auto` 0.2.1 und `ff802-ucm-profile` 0.3.1 (unverändert seit
v0.4.1); Einzelheiten und frühere Releases in [packages.md](packages.md),
Änderungen in [release-notes-v0.5.0.md](release-notes-v0.5.0.md).

## Aufbau

```
include/ff802/reg_codec.h    Registerkodierung (Adresse, Wert, Parität, LE)
include/ff802/cc_mode.h      Befehl für Register 0x3D44 (CC an/aus)
include/ff802/device_id.h    USB-Kennungen und Zuordnung VID:PID -> Modus
include/ff802/detect.h       Erkennung und Bericht auf einfachen Gerätedatensätzen
include/ff802/usb_enum.h     Lesende libusb-Enumeration
include/ff802/usb_backend.h  Abstrakte USB-Schnittstelle, Layout-Prüfung
include/ff802/cc_switch.h    Ablaufsteuerung der Umschaltung
include/ff802/usb_comm.h     libusb-Backend für die Kommunikation (einziges Modul mit Schreibzugriff)
include/ff802/chanmap.h      ff802-chanmap: Signal, Wächter, Erkennung, Ablauf, Reservierung, Auswertung
include/ff802/chanmap_alsa.h ff802-chanmap: ALSA-Backend (nur hw:CARD=<id>, Mixer nur lesend)
include/ff802/auto_*.h        T4: sysfs-Identität, Zustand/Ereignisse, automatischer Ablauf (ohne libusb)
src/                         Implementierung
tools/ff802_detect.c         Programm ff802-detect (schreibfrei)
tools/ff802_cc.c             Programm ff802-cc
tools/ff802_chanmap.c        Programm ff802-chanmap (Loopback-Messung, T2)
tools/ff802_cc_autoswitch.c  T4: Dienstprogramm ff802-cc-autoswitch (nur von systemd gestartet)
tests/                       CTest-Tests, simuliertes USB-Backend, Sicherheitsprüfungen
tests/audio/                 Tests des UCM-Profils: statisch, alsa-lib-Parser, Auswertung mit nachgebildeter Karte, T1-Simulation
tests/chanmap/               Tests von ff802-chanmap mit simuliertem Gerät
tests/packaging/             Pakettest in isolierter Wurzel
tests/auto/                  T4: simuliertes sysfs/USB, Dienst-Ablauf, Benachrichtigung, Steuerung, Units
packaging/                   Debian-Pakete ff802-ucm-profile (build-deb.sh), ff802-cc-auto (build-deb-auto.sh), ff802-cc (build-deb-cc.sh)
audio/ucm2/                  ALSA-UCM-Profil (Aufbau wie /usr/share/alsa/ucm2)
tools/hw-test/               Skripte für Host-Tests (nur nach Freigabe ausführen)
docs/                        Protokoll, Erkennung, Kommunikation, UCM-Profil
.devcontainer/               Dev-Container mit allen Werkzeugen für Build und Tests
```

| Bibliothek | Inhalt | libusb | darf schreiben |
|------------|--------|:------:|:--------------:|
| `ff802_protocol` | `reg_codec`, `cc_mode`, `mixer_init` (feste Liste der 24 Mixer-Register, SysEx) | nein | – |
| `ff802_detect` | `device_id`, `detect` | nein | – |
| `ff802_switch` | `usb_backend`, `cc_switch` | nein | nur über Backend |
| `ff802_usb` | `usb_enum` | ja | nein |
| `ff802_comm` | `usb_comm` | ja | nur mit `FF802_ENABLE_USB_WRITE=ON` |
| `ff802_chanmap` | `chanmap_*` ohne ALSA | nein | – |
| `ff802_chanmap_alsa` | `chanmap_alsa` (alsa-lib) | nein | Audio nur mit −40 dBFS, Mixer nie |
| `ff802_auto` | `auto_sysfs`, `auto_state`, `auto_switch`, `mixer_port`, `mixer_run` | nein | nur über Backend |
| `ff802_midi_alsa` | `midi_seq_alsa` (alsa-lib, ALSA-Sequencer) | nein | MIDI-SysEx nur mit `FF802_ENABLE_MIXER_WRITE=ON` |

## Bauen und Testen (im Dev-Container)

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

| CMake-Option | Standard | Bedeutung |
|--------------|----------|-----------|
| `FF802_ENABLE_USB_WRITE` | `OFF` | Claim und Bulk-Write einkompilieren |
| `FF802_ENABLE_MIXER_WRITE` | `OFF` | Mixer-Initialisierung (`ff802-cc-autoswitch`, `ff802-mixer-init`): Verbinden mit dem MIDI-Port und Senden der SysEx-Nachrichten einkompilieren; nur der Paketbau `build-deb-auto.sh` setzt `ON` |
| `FF802_BUILD_MIXER_B2` | `OFF` | Hardwaretest B.2 `ff802-mixer-b2` bauen: das einzige Programm mit Mixer-Schreibfunktion, genau zwei Nachrichten für Register `0x2600`; nie installiert, kein Paket |
| `FF802_BUILD_MIXER_B3` | `OFF` | Hardwaretest B.3 `ff802-mixer-b3` bauen: genau eine Nachricht `0x4228 = 0x9000` (Playback 9 → Output 9, 0 dB); gleiche Regeln wie B.2 |
| `FF802_BUILD_MIXER_B4` | `OFF` | Hardwaretest B.4 `ff802-mixer-b4` bauen: genau eine Nachricht `0x4261 = 0x0000` (Playback 2 → Output 10 aus), Dry-Run als Standard; gleiche Regeln wie B.2/B.3 |
| `FF802_BUILD_MIXER_D1` | `OFF` | Diagnose D1 `ff802-mixer-d1` bauen: `0x4269 = 0x0000` (`--apply`) bzw. `= 0x9000` (`--restore`), je eine Nachricht, Dry-Run als Standard |
| `FF802_BUILD_MIXER_D3` | `OFF` | Diagnose D3 `ff802-mixer-d3` bauen: genau eine Nachricht `0x4220 = 0x0000` (Playback 1 → Phones 9 aus), Dry-Run als Standard |
| `FF802_BUILD_TOOLS` | `ON` | Programme bauen (benötigt libusb) |
| `FF802_WERROR` | `ON` | Warnungen als Fehler |
| `FF802_REQUIRE_ALSA_TESTS` | aus Umgebung, sonst `OFF` | ALSA-/Pakettests schlagen fehl statt übersprungen zu werden (Dev-Container: `ON`) |
| `FF802_SYSTEM_UCM2` | `/usr/share/alsa/ucm2` | UCM-Baum für `ucm_alsa_eval` und `deb_package` |
| `FF802_PW_ROOT` | `/` | Wurzel der PipeWire-/WirePlumber-Installation für `pw_integration` (entpackter Paketbaum möglich) |

Das Debian-Paket entsteht mit `cmake --build build --target deb` unter
`build/deb/` (reproduzierbar). Installation und Entfernung: siehe
[packaging/ff802-ucm/README.Debian](../packaging/ff802-ucm/README.Debian).
Das T4-Paket entsteht mit `cmake --build build --target deb-auto`
(`build/deb/ff802-cc-auto_<Version>_amd64.deb`, siehe [t4_usb_mode.md](t4_usb_mode.md)),
das Paket der manuellen Umschaltung mit `cmake --build build --target deb-cc`
(`build/deb/ff802-cc_<Version>_amd64.deb`, Release-Build aus einem sauberen
Commit). Projekt- und Paketversionen, Installation, Aktualisierung und
Rückbau aller drei Pakete: [packages.md](packages.md).

## Programme aus den Build-Verzeichnissen starten

Mit dem Paket `ff802-cc` liegen `ff802-cc` und `ff802-detect` unter
`/usr/bin` (auf dem Entwicklungsrechner installiert: 0.4.0 aus Commit
`e12a1d0`, siehe [packages.md](packages.md#v040)):

```sh
ff802-detect                 # Geräteerkennung (öffnet kein Gerät)
ff802-cc --version           # ff802-cc <Version> (git <Revision>, Release, USB writes enabled)
sudo ff802-cc enable         # proprietär -> CC, danach USB-Hub-Port-Reset
sudo ff802-cc disable        # CC -> proprietär; verweigert bei ff802-cc-auto on
```

Ohne das Paket laufen die im Dev-Container gebauten Programme auch direkt
auf dem Host aus dem eingebundenen Projektverzeichnis, ohne
Entwicklungswerkzeuge. `<Projekt>` steht für den Pfad des Checkouts auf
dem Host:

```sh
cd <Projekt>

# Geräteerkennung (öffnet kein Gerät)
build/ff802-detect

# Diagnose vor der Umschaltung (öffnet das Gerät, beansprucht nichts, sendet nichts)
build/ff802-cc check

# Umschaltung (im Standard-Build gesperrt; schreibfähiger Build: build-write,
# konfiguriert mit -DFF802_ENABLE_USB_WRITE=ON)
# proprietär -> CC, danach automatischer USB-Hub-Port-Reset
sudo build-write/ff802-cc enable
# CC -> proprietär (USB-MIDI-Interface 3); verweigert bei ff802-cc-auto on
sudo build-write/ff802-cc disable
```

Ist das Paket installiert, nur noch `/usr/bin/ff802-cc` verwenden, damit
eindeutig ist, welche Fassung läuft (`--version`).

Details: [cc_disable_port_reset.md](cc_disable_port_reset.md).

| Exit | `ff802-detect` | `ff802-cc` |
|:----:|----------------|------------|
| 0 | Gerät gefunden | Prüfung bestanden bzw. umgeschaltet |
| 1 | kein Gerät | kein passendes Gerät |
| 2 | Fehler | Bedienfehler |
| 3 | – | Vorbedingung nicht erfüllt, nichts gesendet |
| 4 | – | Übertragung fehlgeschlagen, Gerätezustand unbekannt |
| 5 | – | nicht gesendet (abgebrochen oder Schreiben gesperrt) |
| 6 | – | übertragen, aber der neue Modus wurde nicht erkannt |

`ff802-cc check` braucht Schreibrechte auf die Gerätedatei. Ohne udev-Regel
meldet es `access to the USB device denied`. Eine udev-Regel wird nicht
automatisch installiert (siehe [usb_comm.md](usb_comm.md)).

**Voraussetzungen auf dem Host:** Laufzeitbibliothek `libusb-1.0-0`
(normalerweise vorinstalliert) und glibc ab 2.34 (Kubuntu 22.04 oder
neuer), x86-64.

`build/ff802-chanmap` (Hardwaretest T2, nur nach Freigabe und nach
[audio_t2_plan.md](audio_t2_plan.md)) braucht zusätzlich
`libasound2t64` und für die Geräte-Reservierung `pw-reserve` (Paket
`pipewire-bin`). `ff802-chanmap check` ist nur lesend.

`ff802-cc-autoswitch` und `ff802-mixer-init` werden mit dem
ALSA-Sequencer-Backend der Mixer-Initialisierung gebaut, wenn
`libasound2-dev` vorhanden ist (sonst `ff802-cc-autoswitch` ohne, mit
Warnung, und kein `ff802-mixer-init`; `build-deb-auto.sh` bricht dann ab).
Das Paket `ff802-cc-auto` hängt deshalb ab 0.2.0~dev1 von `libasound2t64`
ab. Ablauf, Registerliste, Bedienung und Installation:
[mixer_init.md](mixer_init.md).

## Tests

| Test | Inhalt |
|------|--------|
| `test_reg_codec`, `test_cc_mode` | Registerkodierung, Windows-Aufzeichnungen |
| `test_device_id`, `test_detect` | Geräteerkennung mit simulierten Gerätelisten |
| `test_usb_backend` | Layout-Prüfung, Bestätigungswort, `--device`-Syntax, Exit-Codes |
| `test_cc_switch` | Kompletter Ablauf gegen ein simuliertes USB-Backend: Reihenfolge, genau ein Schreibversuch, Freigabe aller Ressourcen, alle Fehlerfälle |
| `test_cc_disable`, `test_disable_check`, `test_usb_port` | `ff802-cc disable` (Ablauf, Vorprüfung) und USB-Hub-Port-Reset nach `enable`, gegen simuliertes Backend und nachgebautes sysfs |
| `test_write_gate` | Backend meldet die konfigurierte Schreibfähigkeit |
| `check_forbidden_usb_calls` | Quelltextprüfung: Schreib-APIs nur in `src/usb_comm.c`, verbotene APIs nirgends |
| `detect_binary_symbols`, `cc_binary_symbols` | Prüfung der importierten Symbole der fertigen Programme mit `nm` |
| `cli_*`, `cc_cli_*` | Kommandozeilen beider Programme ohne USB-Zugriff; `--version` mit Projektversion, Git-Revision und Build-Typ |
| `ucm_profile_static` | Struktur und Inhalt des UCM-Profils gegen eine eigene Erwartungstabelle |
| `ucm_alsa_syntax` | Syntax des UCM-Profils mit dem alsa-lib-Parser (braucht `libasound2-dev`) |
| `ucm_alsa_eval` | Auswertung des Profils durch die echte alsa-lib mit nachgebildeter Fireface-Karte; führt das T1-Skript im Container aus (braucht `alsa-ucm-conf` und alsa-lib 1.2.15.x) |
| `ucm_t1_script` | Host-Test-Skript T1 und Host-Wrapper gegen nachgebautes UCM-Verzeichnis, procfs und `alsaucm` |
| `deb_package` | Paket: reproduzierbarer Bau, Inhalt, Installation, Gerätezuordnung, Upgrade, Entfernung und Purge in isolierter Wurzel; installierter Baum mit der echten alsa-lib ausgewertet |
| `ucm_bind_udev` | T3 0.3.0: udev-Hook `ff802-ucm-bind udev <N>` an neuen Ports, Idempotenz, fremde Dateien, nur das originale 802, alsa-lib ≥ 1.2.16, udev-Regel statisch |
| `pw_integration` | T3: private PipeWire-Sitzung (PipeWire 1.6.x, WirePlumber 0.5.x) mit unverändertem WirePlumber-ALSA-Monitor gegen nachgebildete Karte im Paketaufbau: sechs Sinks und eine Quelle, Signalwege bis zum Hardwarekanal, Startlautstärke, Anstecken während der Wiedergabe, Persistenz, Host-Skript `ff802-t3-host.sh` (braucht `pipewire`, `wireplumber`, `pipewire-pulse`, `pulseaudio-utils`, `libspa-0.2-dev`) |
| `test_chanmap_*`, `chanmap_cli_*`, `check_chanmap_sources`, `chanmap_binary_symbols` | ff802-chanmap: Simulation korrekter und falscher Zuordnungen, aller Abbruch- und Fehlerfälle, Pegelwächter, Kartenerkennung, pw-reserve, Auswertung, ALSA-Backend am `null`-Plugin |
| `test_auto_sysfs`, `test_auto_switch` | T4: Identität aus sysfs, Zustandsdateien, Sperre, automatischer Ablauf mit allen Fehler- und Änderungsfällen; Mixer-Initialisierung im CC-Zweig |
| `test_mixer_init`, `test_mixer_port`, `test_mixer_run` | Mixer-Initialisierung: Registerliste gegen Messwerte und Formeln, SysEx-Bytes, Soundkarte aus sysfs, Auswahl des Sequencer-Ports, Ablauf gegen simuliertes MIDI-Backend mit allen Fehlerfällen |
| `mixer_b2` | Hardwaretest B.2 mit simuliertem MIDI-Backend: Erkennung bei wechselnden Client-Nummern, genau eine Nachricht, Bestätigung, alle Ablehnungen; das echte Programm (falls gebaut) nur `--help`/`--version` und `nm` |
| `mixer_b3` | Hardwaretest B.3, gleiches Schema wie `mixer_b2` mit der einen Routing-Nachricht und `--restore-stereo` |
| `mixer_b4` | Hardwaretest B.4, gleiches Schema wie `mixer_b3`: Dry-Run als Standard, genau eine Nachricht `0x4261 = 0x0000`, Kodierung unabhängig nachgerechnet, falsches Gerät/Modus, fehlende Bestätigung |
| `mixer_d1` | Diagnose D1: beide Nachrichten unabhängig nachgerechnet (Parität), Dry-Run ohne Zugriff, je genau eine Nachricht, keine automatische Wiederherstellung, Bestätigung, MIDI-Fehler, falsches Gerät/Modus |
| `mixer_d3` | Diagnose D3, gleiches Schema wie `mixer_b4` mit `0x4220 = 0x0000` und `--apply` |
| `mixer_init_tool` | `ff802-mixer-init`: die 24 Nachrichten bytegenau und in Reihenfolge, Dry-Run, Bestätigung, Geräteauswahl, Sperre mit dem Dienst, alle Fehlerfälle; das echte Programm nur `--help`/`--version`/`--list` und `nm` |
| `check_mixer_sources`, `autoswitch_mixer_symbols` | Quelltextprüfung (ALSA-Aufrufe, SysEx, Registerliterale nur in den vorgesehenen Dateien) und Symbole des Dienstprogramms (nur Sequencer, ohne Freigabe keine Ausgabefunktionen) |
| `auto_flow`, `auto_notify`, `auto_control`, `auto_units` | T4: Dienstaufruf wie systemd (auch gleichzeitig), Benachrichtigung mit Test-Doubles, `ff802-cc-auto on/off/status` und `mixer-init on/off/status`, udev-Regel und Units (`systemd-analyze`) |
| `autoswitch_binary_symbols`, `autoswitch_sim_no_libusb` | T4: Symbole des Dienstprogramms; Simulationsbuild ohne libusb und ohne libasound |
| `deb_auto_package` | T4-Paket: reproduzierbar, Inhalt, Installation/Upgrade/Entfernung/Purge in isolierter Wurzel mit Stolperdrähten |
| `deb_cc_package` | Paket `ff802-cc`: Bau nur aus sauberem Commit, bitgleich aus zwei Checkouts und beim Neubau, Inhalt, Programme, Zusammenspiel mit `ff802-cc-auto` in isolierter Wurzel |
| `reproducible_release_build` | Release-Build mit `-Werror` in zwei Verzeichnissen, Programme byte-gleich |

Fehlen die Voraussetzungen von `ucm_alsa_syntax`, `ucm_alsa_eval` oder `pw_integration`, werden sie
übersprungen. Im Dev-Container (`FF802_REQUIRE_ALSA_TESTS=ON`) schlagen sie dann
stattdessen fehl.

Keiner der automatisierten Tests greift auf USB-Hardware oder eine Soundkarte zu.

`pw_integration` bricht ab, wenn sein Arbeitsverzeichnis für den
PipeWire-Socket zu lang ist („scratch dir path too long“). Build-Ordner
deshalb mit kurzem Pfad anlegen, z. B. `build-<name>/` im Projektordner
(von `.gitignore` erfasst).

## Vorgemerkt für das nächste Paket-Release

Die folgende Korrektur betrifft eine Datei des Pakets
`ff802-ucm-profile`. Sie ist bewusst **noch nicht** umgesetzt: Das Paket
bleibt in v0.5.0 unverändert bei 0.3.1, damit gleiche Versionsnummern
nicht unterschiedliche Inhalte bekommen. Beim nächsten Paketbau umsetzen
und dabei die Version auf 0.3.2 erhöhen (Changelog „documentation
only“). Die zweite früher vorgemerkte Korrektur (Kopfkommentar von
`/usr/bin/ff802-cc-auto` zu `ff802-cc disable`) ist mit `ff802-cc-auto`
0.2.0 umgesetzt.

**1. `packaging/ff802-ucm/README.Debian`**
(installiert als `/usr/share/doc/ff802-ucm-profile/README.Debian`): Der
Abschnitt „Status“ ist überholt. Er gibt den Stand vor T2/T3 wieder und
erwähnt das Mixer-Routing nicht.

Bisher:

```
Status: the profile is evaluated correctly by alsa-lib with the real card
(host test T1). Analog outputs 1 and 2 are physically confirmed; the other
outputs are not yet verified (hardware test T2). Do not rely on the other
output names before T2.
```

Vorgesehen:

```
Status: the profile is evaluated correctly by alsa-lib with the real card
(host test T1). The channel measurement (hardware test T2) confirmed
Analog outputs 1 and 2 and Line In 1; the other outputs were not
confirmed by it. Independently, Analog 3/4 and Headphones 9/10 and 11/12
were heard with real playback through PipeWire (T3). Analog 5/6, 7/8 and
Line In 2 are not demonstrated.
Whether a channel is audible at a socket also depends on the routing
(faders) in the internal mixer of the unit, which this package does not
set.
```

## Dokumente

- [protocol.md](protocol.md): Registerprotokoll
- [detection.md](detection.md): Geräteerkennung
- [usb_comm.md](usb_comm.md): Kommunikation, Sicherheitsmechanismen, Risiken
- [audio_ucm.md](audio_ucm.md): ALSA-UCM-Profil, Host-Test T1, Lautstärkesicherheit
- [audio_ucm_einbindung.md](audio_ucm_einbindung.md): produktive UCM-Einbindung (Variante A1, Paket)
- [audio_t2_plan.md](audio_t2_plan.md): Hardwaretest T2 (Kanalzuordnung) mit `ff802-chanmap`
- [audio_t3.md](audio_t3.md): T3, Integration in PipeWire/WirePlumber/Plasma, Lautstärkesicherheit, Host-Test
- [audio_t3_usb_port.md](audio_t3_usb_port.md): T3 0.3.0, automatische UCM-Bindung an jedem USB-Port (udev-Hook), Annahmen, Host-Testplan
- [t4_usb_mode.md](t4_usb_mode.md): T4, automatischer CC-Moduswechsel: Architektur, Sicherheit, Installation, Aktivierung, Diagnose, Rollback, Tests
- [t4_port_reset.md](t4_port_reset.md): T4, automatischer Port-Reset nach dem CC-Befehl (0.2.0~dev4)
- [t4_disable.md](t4_disable.md): T4, Untersuchung `ff802-cc disable` (Rückweg in den proprietären Modus)
- [cc_disable_port_reset.md](cc_disable_port_reset.md): `ff802-cc disable` (USB-MIDI-SysEx) und USB-Hub-Port-Reset nach `enable`
- [packages.md](packages.md): die drei Debian-Pakete, Projekt- und Paketversion, Bau, Installation, Aktualisierung, Rückbau
- [mixer_routing.md](mixer_routing.md): interner Mixer, Software-Playback-Fader, Beobachtungen und offene Fragen
- [mixer_init.md](mixer_init.md): Mixer-Initialisierung im CC-Modus (24 Register, Automatik, manuelle Ausführung, Installation)
- [mixer_b4.md](mixer_b4.md): Hardwaretest B.4, Routing Playback 2 → Phones 10 abschalten
- [mixer_d1.md](mixer_d1.md): Diagnose D1, Wirkung von `0x0000` an `0x4269`
- [mixer_d3.md](mixer_d3.md): Diagnose D3, Playback 1 → Phones 9 abschalten
- [mixer_e1.md](mixer_e1.md): Hardwaretest E1, Input 9 → Analog 3 mit 30 % linear (`0x4088 = 0x2666`), geführt, mit Wiederherstellung
- [mixer_v3_integration.md](mixer_v3_integration.md): Integrationstest der Mixer-Initialisierung v3 (38 Register)
- [mixer_v4.md](mixer_v4.md): Mixer-Konfiguration v4, vollständige 12×12-Playback-Matrix (168 Register)
- [release-notes-v0.5.0.md](release-notes-v0.5.0.md): Release-Notizen v0.5.0
- [hw_results/](hw_results/README.md): unveränderte Protokolle der Hardwaretests
- [development.md](development.md): dieses Dokument
