# fireface802-cc

Linux-Werkzeug zur Umschaltung des **ursprünglichen RME Fireface 802**
(nicht 802 FS) zwischen proprietärem USB-Modus und Class-Compliant-Modus.

## Projektstand

| Phase | Inhalt | Stand |
|-------|--------|-------|
| 1  | Kodierung der Registerbefehle und Tests | abgeschlossen |
| 2a | Diagnoseprogramm `ff802-detect`: lesende Geräteerkennung | abgeschlossen, auf dem Host getestet |
| 2b | Umschaltprogramm `ff802-cc` mit Diagnosemodus | abgeschlossen, Umschaltung auf dem Host erfolgreich getestet; `disable` (CC → proprietär) und Port-Reset nach `enable` umgesetzt (2026-11-19 am Gerät erfolgreich getestet); Debian-Paket `ff802-cc` 0.4.0 gebaut und getestet, **nicht installiert** |
| 3 | ALSA-UCM-Profil mit sechs Stereo-Ausgängen für den CC-Modus, Debian-Paket `ff802-ucm-profile` (T1, T3) | Host-Test T1 bestanden; Version **0.3.0 auf dem Host installiert**, automatischer USB-Portwechsel auf dem Host erfolgreich getestet (2026-11-18, [docs/audio_t3_usb_port.md](docs/audio_t3_usb_port.md)); Kanalzuordnung unbestätigt (T2) |
| 4 | Messwerkzeug `ff802-chanmap` für die Kanalzuordnung (Hardwaretest T2) | implementiert und im Container getestet, T2 nicht ausgeführt |
| T4 | Automatischer Wechsel in den CC-Modus beim Anschließen/Systemstart, Debian-Paket `ff802-cc-auto`; Untersuchung `ff802-cc disable` | Version **0.1.0 auf dem Host installiert**, automatische Umschaltung in den Class-Compliant-Modus auf dem Host erfolgreich getestet; 0.1.1 (nur Dokumentation, Neubau) vorbereitet; `disable` ist Teil von `ff802-cc` |

Im Standard-Build kann **kein Programm Daten an das Gerät senden**. Die
Schreibfunktion wird nur mit `-DFF802_ENABLE_USB_WRITE=ON` einkompiliert
(siehe [docs/usb_comm.md](docs/usb_comm.md)). Das Paket `ff802-cc-auto`
enthält einen so gebauten Dienst; seine Automatik ist nach der Installation
aus (siehe [docs/t4_usb_mode.md](docs/t4_usb_mode.md)).

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
| `ff802_auto` | `auto_sysfs`, `auto_state`, `auto_switch` | nein | nur über Backend |

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
| `FF802_PW_ROOT` | `/` | Wurzel der PipeWire-/WirePlumber-Installation für `pw_integration` (entpackter Paketbaum möglich) |

Das Debian-Paket entsteht mit `cmake --build build --target deb` unter
`build/deb/` (reproduzierbar). Installation und Entfernung: siehe
[packaging/ff802-ucm/README.Debian](packaging/ff802-ucm/README.Debian), erst
nach T2 und T3.
Das T4-Paket entsteht mit `cmake --build build --target deb-auto`
(`build/deb/ff802-cc-auto_<Version>_amd64.deb`, siehe [docs/t4_usb_mode.md](docs/t4_usb_mode.md)),
das Paket der manuellen Umschaltung mit `cmake --build build --target deb-cc`
(`build/deb/ff802-cc_<Version>_amd64.deb`, Release-Build aus einem sauberen
Commit). Projekt- und Paketversionen, Installation, Aktualisierung und
Rückbau aller drei Pakete: [docs/packages.md](docs/packages.md).

## Programme auf dem Kubuntu-Host

Mit dem Paket `ff802-cc` (0.4.0, vorbereitet) liegen `ff802-cc` und
`ff802-detect` unter `/usr/bin`:

```sh
ff802-detect                 # Geräteerkennung (öffnet kein Gerät)
ff802-cc --version           # ff802-cc 0.4.0 (git <Revision>, Release, USB writes enabled)
sudo ff802-cc enable         # proprietär -> CC, danach USB-Hub-Port-Reset
sudo ff802-cc disable        # CC -> proprietär; verweigert bei ff802-cc-auto on
```

Ohne das Paket laufen die im Container gebauten Programme direkt aus dem
eingebundenen Projektverzeichnis, ohne Entwicklungswerkzeuge:

```sh
# Geräteerkennung (öffnet kein Gerät)
/home/bernhard/Develop/fireface802-cc/build/ff802-detect

# Diagnose vor der Umschaltung (öffnet das Gerät, beansprucht nichts, sendet nichts)
/home/bernhard/Develop/fireface802-cc/build/ff802-cc check

# Umschaltung (im Standard-Build gesperrt; schreibfähiger Build: build-write)
# proprietär -> CC, danach automatischer USB-Hub-Port-Reset
sudo /home/bernhard/Develop/fireface802-cc/build-write/ff802-cc enable
# CC -> proprietär (USB-MIDI-Interface 3); verweigert bei ff802-cc-auto on
sudo /home/bernhard/Develop/fireface802-cc/build-write/ff802-cc disable
```

Details: [docs/cc_disable_port_reset.md](docs/cc_disable_port_reset.md).

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
| `test_auto_sysfs`, `test_auto_switch` | T4: Identität aus sysfs, Zustandsdateien, Sperre, automatischer Ablauf mit allen Fehler- und Änderungsfällen |
| `auto_flow`, `auto_notify`, `auto_control`, `auto_units` | T4: Dienstaufruf wie systemd (auch gleichzeitig), Benachrichtigung mit Test-Doubles, `ff802-cc-auto on/off/status`, udev-Regel und Units (`systemd-analyze`) |
| `autoswitch_binary_symbols`, `autoswitch_sim_no_libusb` | T4: Symbole des Dienstprogramms; Simulationsbuild ohne libusb |
| `deb_auto_package` | T4-Paket: reproduzierbar, Inhalt, Installation/Upgrade/Entfernung/Purge in isolierter Wurzel mit Stolperdrähten |
| `deb_cc_package` | Paket `ff802-cc`: Bau nur aus sauberem Commit, bitgleich aus zwei Checkouts und beim Neubau, Inhalt, Programme, Zusammenspiel mit `ff802-cc-auto` in isolierter Wurzel |
| `reproducible_release_build` | Release-Build mit `-Werror` in zwei Verzeichnissen, Programme byte-gleich |

Fehlen die Voraussetzungen von `ucm_alsa_syntax`, `ucm_alsa_eval` oder `pw_integration`, werden sie
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
- [docs/audio_t3.md](docs/audio_t3.md): T3, Integration in PipeWire/WirePlumber/Plasma, Lautstärkesicherheit, Host-Test
- [docs/audio_t3_usb_port.md](docs/audio_t3_usb_port.md): T3 0.3.0, automatische UCM-Bindung an jedem USB-Port (udev-Hook), Annahmen, Host-Testplan
- [docs/t4_usb_mode.md](docs/t4_usb_mode.md): T4, automatischer CC-Moduswechsel: Architektur, Sicherheit, Installation, Aktivierung, Diagnose, Rollback, Tests
- [docs/t4_disable.md](docs/t4_disable.md): T4, Untersuchung `ff802-cc disable` (Rückweg in den proprietären Modus)
- [docs/cc_disable_port_reset.md](docs/cc_disable_port_reset.md): `ff802-cc disable` (USB-MIDI-SysEx) und USB-Hub-Port-Reset nach `enable`
- [docs/packages.md](docs/packages.md): die drei Debian-Pakete, Projekt- und Paketversion, Bau, Installation, Aktualisierung, Rückbau

## Lizenz

Noch nicht festgelegt.
