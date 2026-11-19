# Pakete: Übersicht, Versionen, Bau, Installation, Rückbau

> **Stand 2026-11-19:** Auf dem Host installiert sind `ff802-ucm-profile`
> 0.3.0 und `ff802-cc-auto` 0.1.0. `ff802-cc` 0.4.0 und `ff802-cc-auto`
> 0.1.1 sind im Dev-Container gebaut und getestet, aber **nicht
> installiert**.

## Die drei Pakete

| Paket | Paketversion | Inhalt | hängt ab von |
|---|---|---|---|
| `ff802-ucm-profile` | 0.3.0 | UCM-Profil (sechs Stereo-Ausgänge, ein Eingang), `ff802-ucm-bind`, udev-Regel für die Bindung an jedem Port | `alsa-ucm-conf`, `libasound2t64` |
| `ff802-cc-auto` | 0.1.1 (vorbereitet; installiert: 0.1.0) | **automatische** Umschaltung proprietär → CC beim Anschließen (T4) | `libc6`, `libusb-1.0-0`, `systemd`, `udev` |
| `ff802-cc` | 0.4.0 (neu) | **manuelle** Umschaltung: `ff802-cc enable` (mit USB-Hub-Port-Reset) und `ff802-cc disable`, dazu `ff802-detect` | `libc6`, `libusb-1.0-0` |

Die Pakete hängen nicht voneinander ab, und keine Datei gehört zu zwei
Paketen (geprüft in `deb_cc_package`). `ff802-cc` schlägt die beiden
anderen nur vor (`Suggests`). Es liest den Automatikzustand
`/var/lib/ff802-cc-auto/mode`: Fehlt die Datei, gilt die Automatik als
aus. `ff802-cc disable` wird abgelehnt, solange die Automatik an ist.

## Versionen: Projekt- und Paketversion

| Begriff | Wo festgelegt | Wo sichtbar | Beispiel |
|---|---|---|---|
| **Projektversion** | `project(VERSION …)` in `CMakeLists.txt` | `--version` aller Programme | `0.4.0` |
| **Git-Revision** | ermittelt von CMake beim Konfigurieren (12 Hexziffern, „+dirty“ bei uncommitteten Änderungen an versionierten Dateien, „unknown“ ohne Git) | `--version` von `ff802-cc` und `ff802-detect`, `BUILDINFO` | `8440f8311881` |
| **Paketversion** | erste Zeile von `packaging/<paket>/changelog` | `dpkg -l`, Dateiname der `.deb` | `ff802-cc 0.4.0`, `ff802-cc-auto 0.1.1` |

Die Paketversion kann von der Projektversion abweichen:
- `ff802-cc-auto` 0.1.1 enthält ein Programm mit Projektversion 0.4.0.
- Eine reine Paketänderung bekäme zum Beispiel `0.4.0-2`.
- Der Override der Bauskripte ändert nur die Paketversion, etwa für
  Upgrade-Tests.

```
$ ff802-cc --version
ff802-cc 0.4.0 (git 8440f8311881, Release, USB writes enabled)
$ ff802-detect --version
ff802-detect 0.4.0 (git 8440f8311881, Release)
```

„+dirty“ zeigt den Zustand **beim Konfigurieren**. Ändert man in einem
Entwicklungs-Build danach Dateien, ohne neu zu konfigurieren, wird das
nicht angezeigt. Ein neuer Commit löst die Konfiguration dagegen
automatisch neu aus. Release-Pakete betrifft das nicht: Sie werden immer
frisch aus einem sauberen Checkout konfiguriert.

## Bauen (Dev-Container)

```sh
cmake -S . -B build && cmake --build build
cmake --build build --target deb       # build/deb/ff802-ucm-profile_0.3.0_all.deb
cmake --build build --target deb-auto  # build/deb/ff802-cc-auto_0.1.1_amd64.deb
cmake --build build --target deb-cc    # build/deb/ff802-cc_0.4.0_amd64.deb
ctest --test-dir build                 # u. a. deb_package, deb_auto_package, deb_cc_package
```

So baut `deb-cc` (`packaging/build-deb-cc.sh`):
- **Bricht ab** (Exit 3), wenn versionierte Dateien von HEAD abweichen,
  gestaged oder nicht, oder wenn eine verpackte Datei nicht committet
  ist. Außerhalb eines eigenen Git-Checkouts bricht es ebenfalls ab.
- Konfiguriert einen **frischen** Build-Ordner (Release,
  `FF802_ENABLE_USB_WRITE=ON`) und prüft, dass CMake genau die Revision
  von HEAD ermittelt hat.
- Kompiliert und kopiert die Programme, führt sie aber nie aus.
- Legt `/usr/share/doc/ff802-cc/BUILDINFO` bei: Commit, Revision,
  Build-Typ, Compiler, CMake, libusb-dev und `SOURCE_DATE_EPOCH`.
- **Reproduzierbar:** Derselbe Commit mit denselben Werkzeugversionen
  (gleiches Dev-Container-Image) ergibt dieselbe `.deb`, auch aus einem
  anderen Checkout-Pfad und bei einem späteren Neubau (geprüft).

## Installation und Aktualisierung (Host, Administrator, nach Freigabe)

```sh
sudo apt install ./ff802-cc_0.4.0_amd64.deb            # neu
sudo apt install ./ff802-cc-auto_0.1.1_amd64.deb       # Upgrade von 0.1.0
ff802-cc --version
ff802-cc-auto status
dpkg -l 'ff802-*'
```

- Das Upgrade von `ff802-cc-auto` behält den Automatikzustand (on/off).
  Regel, Units, polkit-Richtlinie und Maintainer-Skripte sind
  bytegleich zu 0.1.0. Es wird nichts ausgelöst und nichts neu
  gestartet.
- `ff802-cc` hat keine Maintainer-Skripte. Aufruf: `sudo ff802-cc enable`
  bzw. `sudo ff802-cc disable`.
- `ff802-ucm-profile` 0.3.0 bleibt unverändert.
- Danach bitte nur noch `/usr/bin/ff802-cc` verwenden, nicht mehr
  `build-write/ff802-cc`. So ist eindeutig, welche Fassung läuft
  (`--version`).

## Rückbau

```sh
sudo apt remove ff802-cc                                          # manuelle Umschaltung entfernen
sudo apt install --allow-downgrades ./ff802-cc-auto_0.1.0_amd64.deb  # zurück auf 0.1.0
```

Das Entfernen von `ff802-cc` lässt `ff802-cc-auto`, dessen Zustand und das
UCM-Profil unberührt; das ist in `deb_cc_package` geprüft. Den
vollständigen Rückbau der anderen Pakete beschreiben
[t4_usb_mode.md](t4_usb_mode.md) und
[audio_t3_usb_port.md](audio_t3_usb_port.md).
