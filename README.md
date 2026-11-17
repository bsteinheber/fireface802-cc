# fireface802-cc

Linux-Werkzeug zur Umschaltung des **ursprünglichen RME Fireface 802**
(nicht 802 FS) zwischen proprietärem USB-Modus und Class-Compliant-Modus.

## Projektstand

| Phase | Inhalt | Stand |
|-------|--------|-------|
| 1  | Kodierung der Registerbefehle und Tests | abgeschlossen |
| 2a | Diagnoseprogramm `ff802-detect`: lesende Geräteerkennung | abgeschlossen, auf dem Host getestet |
| 2b | Umschaltprogramm `ff802-cc` mit Diagnosemodus | abgeschlossen, Umschaltung auf dem Host erfolgreich getestet |
| 3 | ALSA-UCM-Profil mit sechs Stereo-Ausgängen für den CC-Modus, Debian-Paket `ff802-ucm-profile` | Host-Test T1 bestanden, Paket gebaut und isoliert getestet, **nicht installiert, Kanalzuordnung unbestätigt** |
| 4 | Messwerkzeug `ff802-chanmap` für die Kanalzuordnung (Hardwaretest T2) | implementiert und im Container getestet, T2 nicht ausgeführt |

Im Standard-Build kann **kein Programm Daten an das Gerät senden**. Die
Schreibfunktion wird nur mit `-DFF802_ENABLE_USB_WRITE=ON` einkompiliert
(siehe [docs/usb_comm.md](docs/usb_comm.md)).

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
src/                         Implementierung
tools/ff802_detect.c         Programm ff802-detect (schreibfrei)
tools/ff802_cc.c             Programm ff802-cc
tools/ff802_chanmap.c        Programm ff802-chanmap (Loopback-Messung, T2)
tests/                       CTest-Tests, simuliertes USB-Backend, Sicherheitsprüfungen
tests/audio/                 Tests des UCM-Profils: statisch, alsa-lib-Parser, Auswertung mit nachgebildeter Karte, T1-Simulation
tests/chanmap/               Tests von ff802-chanmap mit simuliertem Gerät
tests/packaging/             Pakettest in isolierter Wurzel
packaging/                   Debian-Paket ff802-ucm-profile (build-deb.sh, Paketinhalt)
audio/ucm2/                  ALSA-UCM-Profil (Aufbau wie /usr/share/alsa/ucm2)
tools/hw-test/               Skripte für Host-Tests (nur nach Freigabe ausführen)
docs/                        Protokoll, Erkennung, Kommunikation, UCM-Profil
```

| Bibliothek | Inhalt | libusb | darf schreiben |
|------------|--------|:------:|:--------------:|
| `ff802_protocol` | `reg_codec`, `cc_mode` | nein | – |
| `ff802_detect` | `device_id`, `detect` | nein | – |
| `ff802_switch` | `usb_backend`, `cc_switch` | nein | nur über Backend |
| `ff802_usb` | `usb_enum` | ja | nein |
| `ff802_comm` | `usb_comm` | ja | nur mit `FF802_ENABLE_USB_WRITE=ON` |
| `ff802_chanmap` | `chanmap_*` ohne ALSA | nein | – |
| `ff802_chanmap_alsa` | `chanmap_alsa` (alsa-lib) | nein | Audio nur mit −40 dBFS, Mixer nie |

## Bauen und Testen (im Dev-Container)

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

| CMake-Option | Standard | Bedeutung |
|--------------|----------|-----------|
| `FF802_ENABLE_USB_WRITE` | `OFF` | Claim und Bulk-Write einkompilieren |
| `FF802_BUILD_TOOLS` | `ON` | Programme bauen (benötigt libusb) |
| `FF802_WERROR` | `ON` | Warnungen als Fehler |
| `FF802_REQUIRE_ALSA_TESTS` | aus Umgebung, sonst `OFF` | ALSA-/Pakettests schlagen fehl statt übersprungen zu werden (Dev-Container: `ON`) |
| `FF802_SYSTEM_UCM2` | `/usr/share/alsa/ucm2` | UCM-Baum für `ucm_alsa_eval` und `deb_package` |

Das Debian-Paket entsteht mit `cmake --build build --target deb` unter
`build/deb/` (reproduzierbar). Installation und Entfernung: siehe
[packaging/ff802-ucm/README.Debian](packaging/ff802-ucm/README.Debian), erst
nach T2 und T3.

## Programme auf dem Kubuntu-Host

Die im Container gebauten Programme liegen im eingebundenen
Projektverzeichnis und laufen ohne Entwicklungswerkzeuge und ohne
Root-Rechte.

```sh
# Geräteerkennung (öffnet kein Gerät)
/home/bernhard/Develop/fireface802-cc/build/ff802-detect

# Diagnose vor der Umschaltung (öffnet das Gerät, beansprucht nichts, sendet nichts)
/home/bernhard/Develop/fireface802-cc/build/ff802-cc check

# Umschaltung (im Standard-Build gesperrt)
/home/bernhard/Develop/fireface802-cc/build/ff802-cc enable
```

| Exit | `ff802-detect` | `ff802-cc` |
|:----:|----------------|------------|
| 0 | Gerät gefunden | Prüfung bestanden bzw. Befehl übertragen |
| 1 | kein Gerät | kein passendes Gerät |
| 2 | Fehler | Bedienfehler |
| 3 | – | Vorbedingung nicht erfüllt, nichts gesendet |
| 4 | – | Übertragung fehlgeschlagen, Gerätezustand unbekannt |
| 5 | – | nicht gesendet (abgebrochen oder Schreiben gesperrt) |

`ff802-cc check` braucht Schreibrechte auf die Gerätedatei. Ohne udev-Regel
meldet es `access to the USB device denied`. Eine udev-Regel wird nicht
automatisch installiert (siehe [docs/usb_comm.md](docs/usb_comm.md)).

**Voraussetzungen auf dem Host:** Laufzeitbibliothek `libusb-1.0-0`
(normalerweise vorinstalliert) und glibc ab 2.34 (Kubuntu 22.04 oder
neuer), x86-64.

`build/ff802-chanmap` (Hardwaretest T2, nur nach Freigabe und nach
[docs/audio_t2_plan.md](docs/audio_t2_plan.md)) braucht zusätzlich
`libasound2t64` und für die Geräte-Reservierung `pw-reserve` (Paket
`pipewire-bin`). `ff802-chanmap check` ist nur lesend.


## Tests

| Test | Inhalt |
|------|--------|
| `test_reg_codec`, `test_cc_mode` | Registerkodierung, Windows-Aufzeichnungen |
| `test_device_id`, `test_detect` | Geräteerkennung mit simulierten Gerätelisten |
| `test_usb_backend` | Layout-Prüfung, Bestätigungswort, `--device`-Syntax, Exit-Codes |
| `test_cc_switch` | Kompletter Ablauf gegen ein simuliertes USB-Backend: Reihenfolge, genau ein Schreibversuch, Freigabe aller Ressourcen, alle Fehlerfälle |
| `test_write_gate` | Backend meldet die konfigurierte Schreibfähigkeit |
| `check_forbidden_usb_calls` | Quelltextprüfung: Schreib-APIs nur in `src/usb_comm.c`, verbotene APIs nirgends |
| `detect_binary_symbols`, `cc_binary_symbols` | Prüfung der importierten Symbole der fertigen Programme mit `nm` |
| `cli_*`, `cc_cli_*` | Kommandozeilen beider Programme ohne USB-Zugriff |
| `ucm_profile_static` | Struktur und Inhalt des UCM-Profils gegen eine eigene Erwartungstabelle |
| `ucm_alsa_syntax` | Syntax des UCM-Profils mit dem alsa-lib-Parser (braucht `libasound2-dev`) |
| `ucm_alsa_eval` | Auswertung des Profils durch die echte alsa-lib mit nachgebildeter Fireface-Karte; führt das T1-Skript im Container aus (braucht `alsa-ucm-conf` und alsa-lib 1.2.15.x) |
| `ucm_t1_script` | Host-Test-Skript T1 und Host-Wrapper gegen nachgebautes UCM-Verzeichnis, procfs und `alsaucm` |
| `deb_package` | Paket: reproduzierbarer Bau, Inhalt, Installation, Gerätezuordnung, Upgrade, Entfernung und Purge in isolierter Wurzel; installierter Baum mit der echten alsa-lib ausgewertet |
| `test_chanmap_*`, `chanmap_cli_*`, `check_chanmap_sources`, `chanmap_binary_symbols` | ff802-chanmap: Simulation korrekter und falscher Zuordnungen, aller Abbruch- und Fehlerfälle, Pegelwächter, Kartenerkennung, pw-reserve, Auswertung, ALSA-Backend am `null`-Plugin |
| `reproducible_release_build` | Release-Build mit `-Werror` in zwei Verzeichnissen, Programme byte-gleich |

Fehlen die Voraussetzungen von `ucm_alsa_syntax` oder `ucm_alsa_eval`, werden sie
übersprungen. Im Dev-Container (`FF802_REQUIRE_ALSA_TESTS=ON`) schlagen sie dann
stattdessen fehl.

Keiner der automatisierten Tests greift auf USB-Hardware oder eine Soundkarte zu.

## Dokumentation

- [docs/protocol.md](docs/protocol.md): Registerprotokoll
- [docs/detection.md](docs/detection.md): Geräteerkennung
- [docs/usb_comm.md](docs/usb_comm.md): Kommunikation, Sicherheitsmechanismen, Risiken
- [docs/audio_ucm.md](docs/audio_ucm.md): ALSA-UCM-Profil, Host-Test T1, Lautstärkesicherheit
- [docs/audio_ucm_einbindung.md](docs/audio_ucm_einbindung.md): produktive UCM-Einbindung (Variante A1, Paket)
- [docs/audio_t2_plan.md](docs/audio_t2_plan.md): Hardwaretest T2 (Kanalzuordnung) mit `ff802-chanmap`

## Lizenz

Noch nicht festgelegt.
