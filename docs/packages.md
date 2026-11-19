# Pakete: Übersicht, Versionen, Bau, Installation, Rückbau

> **Stand 2026-11-19:** Auf dem Entwicklungsrechner installiert sind
> `ff802-ucm-profile` 0.3.0, `ff802-cc-auto` 0.1.0 und `ff802-cc` 0.4.0
> (gebaut aus Commit `e12a1d0`). Ob `ff802-cc-auto` 0.1.1 installiert
> wurde, ist nicht bestätigt. Einzelheiten:
> [Installationsstand](#installationsstand).

## Installationsstand

| Paket | installiert | Paket im Dev-Container (`build/deb/`) | SHA-256 |
|---|---|---|---|
| `ff802-ucm-profile` | 0.3.0 | 0.3.0, gebaut 2026-11-18 (Paket seit `c116416`) | `53bf3cf5c0e71c3612a30192a0f5e185484e3c72db2926b76ef888723dd79805` |
| `ff802-cc-auto` | 0.1.0; 0.1.1 nicht bestätigt | 0.1.0, gebaut 2026-11-17 | `4735acddb02ae57f22cb26baca88c28cc9d9298a61259c806d85a2e7dfb52cef` |
| | | 0.1.1, gebaut 2026-11-19 aus `e12a1d0` | `555356ccd2d1bad2c1d942911ef067fbdb7e471b2bd2ceeed165bc4f6ab8b162` |
| `ff802-cc` | 0.4.0 | 0.4.0, gebaut 2026-11-19 aus `e12a1d0` | `14772f0720589a8aea2e6a09ba4fd49dfe23c7b5445070ace516ed270b0a04b0` |

Ob die installierten Dateien genau diese `.deb` waren, ist nicht über
Prüfsummen auf dem Host bestätigt.

- **`ff802-cc` 0.4.0 stammt aus `e12a1d0`, nicht aus dem Tag `v0.4.0`.**
  `e12a1d0` ist der Commit direkt vor `v0.4.0` (`c0acd9a`). Der einzige
  Unterschied ist das historische Referenzskript
  `tools/hw-test/ff802-disable-test.py`, das in keinem Paket enthalten
  ist. Programme und Paketinhalt sind daher gleich.
- `ff802-cc --version` und `/usr/share/doc/ff802-cc/BUILDINFO` zeigen
  deshalb `e12a1d0`, nicht `c0acd9a`. Ein Neubau aus `v0.4.0` ergibt eine
  andere Revision und damit andere Prüfsummen.
- Prüfen auf dem Host (nur lesend):

  ```sh
  dpkg -l 'ff802-*'
  ff802-cc --version     # ff802-cc 0.4.0 (git e12a1d0201ea, Release, USB writes enabled)
  grep commit /usr/share/doc/ff802-cc/BUILDINFO   # git commit: e12a1d0201ea2b018b7ece39b89a0b7f712c5996
  ```

- Die installierten Pakete bleiben vorerst unverändert. Die nächsten
  Paketversionen (Projektversion 0.4.1) unterscheiden sich von den
  installierten nur in Lizenzangaben und Dokumentation.

## Die drei Pakete

| Paket | Paketversion (Quellen) | Inhalt | hängt ab von |
|---|---|---|---|
| `ff802-ucm-profile` | 0.3.1 (installiert: 0.3.0) | UCM-Profil (sechs Stereo-Ausgänge, ein Eingang), `ff802-ucm-bind`, udev-Regel für die Bindung an jedem Port | `alsa-ucm-conf`, `libasound2t64` |
| `ff802-cc-auto` | 0.1.2 (installiert: 0.1.0, 0.1.1 nicht bestätigt) | **automatische** Umschaltung proprietär → CC beim Anschließen (T4) | `libc6`, `libusb-1.0-0`, `systemd`, `udev` |
| `ff802-cc` | 0.4.1 (installiert: 0.4.0) | **manuelle** Umschaltung: `ff802-cc enable` (mit USB-Hub-Port-Reset) und `ff802-cc disable`, dazu `ff802-detect` | `libc6`, `libusb-1.0-0` |

Gegenüber den installierten Fassungen ändern sich in 0.3.1, 0.1.2 und
0.4.1 nur die Lizenzangaben (`copyright`), der Changelog und die
Projektversion in `--version`; bei `ff802-ucm-profile` zusätzlich ein
Kommentarkopf (Copyright, SPDX) in `longname-master.conf`. Die Funktion
ist unverändert.

Die Pakete hängen nicht voneinander ab, und keine Datei gehört zu zwei
Paketen (geprüft in `deb_cc_package`). `ff802-cc` schlägt die beiden
anderen nur vor (`Suggests`). Es liest den Automatikzustand
`/var/lib/ff802-cc-auto/mode`: Fehlt die Datei, gilt die Automatik als
aus. `ff802-cc disable` wird abgelehnt, solange die Automatik an ist.

## Versionen: Projekt- und Paketversion

| Begriff | Wo festgelegt | Wo sichtbar | Beispiel |
|---|---|---|---|
| **Projektversion** | `project(VERSION …)` in `CMakeLists.txt` | `--version` aller Programme | `0.4.1` |
| **Git-Revision** | ermittelt von CMake beim Konfigurieren (12 Hexziffern, „+dirty“ bei uncommitteten Änderungen an versionierten Dateien, „unknown“ ohne Git) | `--version` von `ff802-cc` und `ff802-detect`, `BUILDINFO` | `e12a1d0201ea` |
| **Paketversion** | erste Zeile von `packaging/<paket>/changelog` | `dpkg -l`, Dateiname der `.deb` | `ff802-cc 0.4.1`, `ff802-cc-auto 0.1.2` |

Die Paketversion kann von der Projektversion abweichen:
- `ff802-cc-auto` 0.1.2 enthält ein Programm mit Projektversion 0.4.1.
- Eine reine Paketänderung bekäme zum Beispiel `0.4.1-2`.
- Der Override der Bauskripte ändert nur die Paketversion, etwa für
  Upgrade-Tests.

```
$ ff802-cc --version
ff802-cc 0.4.1 (git <Revision>, Release, USB writes enabled)
$ ff802-detect --version
ff802-detect 0.4.1 (git <Revision>, Release)
```

„+dirty“ zeigt den Zustand **beim Konfigurieren**. Ändert man in einem
Entwicklungs-Build danach Dateien, ohne neu zu konfigurieren, wird das
nicht angezeigt. Ein neuer Commit löst die Konfiguration dagegen
automatisch neu aus. Release-Pakete betrifft das nicht: Sie werden immer
frisch aus einem sauberen Checkout konfiguriert.

## Bauen (Dev-Container)

```sh
cmake -S . -B build && cmake --build build
cmake --build build --target deb       # build/deb/ff802-ucm-profile_0.3.1_all.deb
cmake --build build --target deb-auto  # build/deb/ff802-cc-auto_0.1.2_amd64.deb
cmake --build build --target deb-cc    # build/deb/ff802-cc_0.4.1_amd64.deb
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

Für ein Release die Pakete aus dem Commit bauen, der getaggt wird, damit
`--version` und `BUILDINFO` die Revision des Tags zeigen (anders als bei
0.4.0, siehe [Installationsstand](#installationsstand)).

## Installation und Aktualisierung (Host, Administrator, nach Freigabe)

```sh
sudo apt install ./ff802-cc_0.4.1_amd64.deb            # Upgrade von 0.4.0
sudo apt install ./ff802-cc-auto_0.1.2_amd64.deb       # Upgrade von 0.1.0 oder 0.1.1
sudo apt install ./ff802-ucm-profile_0.3.1_all.deb     # Upgrade von 0.3.0
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
- Nur noch `/usr/bin/ff802-cc` verwenden, nicht `build-write/ff802-cc`.
  So ist eindeutig, welche Fassung läuft (`--version`).

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
