# Pakete: Übersicht, Versionen, Bau, Installation, Rückbau

> **Stand 2026-11-23, Projektversion 0.5.0 (Release `v0.5.0` in
> Vorbereitung):** Pakete `ff802-cc` 0.5.0, `ff802-cc-auto` 0.2.0 und
> `ff802-ucm-profile` 0.3.1. Auf dem Entwicklungsrechner ist
> `ff802-cc-auto` 0.2.0~dev6 installiert (Integrationstest bestanden,
> [mixer_v3_integration.md](mixer_v3_integration.md)); 0.2.0 ist
> inhaltlich derselbe Stand mit Release-Versionsnummern.

## Release v0.5.0

| Paket | Version | gegenüber v0.4.1 |
|---|---|---|
| `ff802-cc` | 0.5.0 | Funktion unverändert; die Zeitwerte des Port-Resets (2 s aus, bis 15 s Warten) stehen jetzt mit denselben Werten in `usb_port.h` und werden mit der Automatik geteilt; neue Projektversion in `--version` |
| `ff802-cc-auto` | 0.2.0 | automatischer Port-Reset nach dem CC-Befehl (eigene Unit `ff802-cc-auto-enable@.service`), optionale Mixer-Initialisierung mit 38 Registern (`ff802-mixer-init-v3`, zwei Durchgänge im Dienst), Programm `ff802-mixer-init`, Abhängigkeit `libasound2t64` |
| `ff802-ucm-profile` | 0.3.1 | unverändert (gleiche Quellen wie in v0.4.1) |

Die Pakete werden aus dem getaggten Commit gebaut; ihre SHA-256 stehen
in `SHA256SUMS` des GitHub-Releases. Release-Notizen:
[release-notes-v0.5.0.md](release-notes-v0.5.0.md).

## Frühere Releases

### v0.4.1 (2026-11-20)

Pakete aus dem Tag `v0.4.1` (Commit `04fa6e2`), Anhänge des
GitHub-Releases
<https://github.com/bsteinheber/fireface802-cc/releases/tag/v0.4.1>:

| Anhang | Paketversion | SHA-256 |
|---|---|---|
| `ff802-cc_0.4.1_amd64.deb` | `ff802-cc` 0.4.1 | `cf67323712f55e0f6b550ee340348faa0787f81c8a93a8f9c27dd4744b5b0f60` |
| `ff802-cc-auto_0.1.2_amd64.deb` | `ff802-cc-auto` 0.1.2 | `806f030f4a5afd56782709403419897feb2ffa09a25eb878b003af927332918c` |
| `ff802-ucm-profile_0.3.1_all.deb` | `ff802-ucm-profile` 0.3.1 | `842ea4fc230c875d97a8369933abf535634c686f3302ca97c4535eb68deeca1c` |
| `SHA256SUMS` | Prüfsummen der drei Pakete | `6a6ec459691c73fc10901f7908a77213fb72bee6db02a1ff20874a842ab8ee88` |

### v0.4.0

`ff802-cc` 0.4.0 wurde aus `e12a1d0` gebaut, dem Commit direkt vor dem
Tag `v0.4.0` (`c0acd9a`); der einzige Unterschied ist das historische
Referenzskript `tools/hw-test/ff802-disable-test.py` (in keinem Paket).
`--version` und `BUILDINFO` dieser Pakete zeigen deshalb `e12a1d0`.

### Entwicklungsstände 0.2.0~dev1 … dev6 von `ff802-cc-auto`

Nur für Hardwaretests auf dem Entwicklungsrechner gebaut, nie
veröffentlicht; ihr Verlauf steht im Changelog des Pakets. dev6 ist der
am Gerät getestete Stand, aus dem 0.2.0 hervorgeht.

## Die drei Pakete

| Paket | Version | Inhalt | hängt ab von |
|---|---|---|---|
| `ff802-ucm-profile` | 0.3.1 | UCM-Profil (sechs Stereo-Ausgänge, ein Eingang), `ff802-ucm-bind`, udev-Regel für die Bindung an jedem Port | `alsa-ucm-conf`, `libasound2t64` |
| `ff802-cc-auto` | 0.2.0 | **automatische** Umschaltung proprietär → CC beim Anschließen mit Port-Reset (T4), optionale Mixer-Initialisierung, `ff802-mixer-init` | `libc6`, `libusb-1.0-0`, `libasound2t64`, `systemd`, `udev` |
| `ff802-cc` | 0.5.0 | **manuelle** Umschaltung: `ff802-cc enable` (mit USB-Hub-Port-Reset) und `ff802-cc disable`, dazu `ff802-detect` | `libc6`, `libusb-1.0-0` |

Die Pakete hängen nicht voneinander ab, und keine Datei gehört zu zwei
Paketen (geprüft in `deb_cc_package`). `ff802-cc` schlägt die beiden
anderen nur vor (`Suggests`). Es liest den Automatikzustand
`/var/lib/ff802-cc-auto/mode`: Fehlt die Datei, gilt die Automatik als
aus. `ff802-cc disable` wird abgelehnt, solange die Automatik an ist.

## Versionen: Projekt- und Paketversion

| Begriff | Wo festgelegt | Wo sichtbar | Beispiel |
|---|---|---|---|
| **Projektversion** | `project(VERSION …)` in `CMakeLists.txt` | `--version` aller Programme | `0.5.0` |
| **Git-Revision** | ermittelt von CMake beim Konfigurieren (12 Hexziffern, „+dirty“ bei uncommitteten Änderungen an versionierten Dateien, „unknown“ ohne Git) | `--version` von `ff802-cc` und `ff802-detect`, `BUILDINFO` | `e12a1d0201ea` |
| **Paketversion** | erste Zeile von `packaging/<paket>/changelog` | `dpkg -l`, Dateiname der `.deb` | `ff802-cc 0.5.0`, `ff802-cc-auto 0.2.0` |

Die Paketversion kann von der Projektversion abweichen:
- `ff802-cc-auto` 0.2.0 enthält Programme mit Projektversion 0.5.0.
- Eine reine Paketänderung bekäme zum Beispiel `0.5.0-2`.
- Der Override der Bauskripte ändert nur die Paketversion, etwa für
  Upgrade-Tests.

```
$ ff802-cc --version
ff802-cc 0.5.0 (git <Revision>, Release, USB writes enabled)
$ ff802-detect --version
ff802-detect 0.5.0 (git <Revision>, Release)
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
cmake --build build --target deb-auto  # build/deb/ff802-cc-auto_0.2.0_amd64.deb
cmake --build build --target deb-cc    # build/deb/ff802-cc_0.5.0_amd64.deb
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

`deb-auto` (`packaging/build-deb-auto.sh`) baut `ff802-cc-autoswitch` und
`ff802-mixer-init` mit `FF802_ENABLE_USB_WRITE=ON` und
`FF802_ENABLE_MIXER_WRITE=ON` (die einzige Stelle, an der der
Mixer-Schreibweg eingeschaltet wird); ebenfalls reproduzierbar (geprüft in
`deb_auto_package`).

Für ein Release die Pakete aus dem Commit bauen, der getaggt wird, damit
`--version` und `BUILDINFO` die Revision des Tags zeigen.

## Installation und Aktualisierung (Host, Administrator)

Die Pakete stammen aus dem GitHub-Release oder aus einem eigenen Bau;
vorher `sha256sum -c SHA256SUMS`.

```sh
sudo apt install ./ff802-cc_0.5.0_amd64.deb            # Upgrade von 0.4.x
sudo apt install ./ff802-cc-auto_0.2.0_amd64.deb       # Upgrade von 0.1.x oder 0.2.0~devN
sudo apt install ./ff802-ucm-profile_0.3.1_all.deb     # Upgrade von 0.3.0
ff802-cc --version
ff802-cc-auto status
ff802-cc-auto mixer-init status
dpkg -l 'ff802-*'
```

- Das Upgrade von `ff802-cc-auto` behält den Automatikzustand und den
  Zustand der Mixer-Initialisierung (on/off); beide sind nach einer
  Neuinstallation aus. Es wird nichts ausgelöst und nichts neu gestartet;
  die neuen Regeln und Units gelten ab dem nächsten Anschließen.
- `ff802-cc` hat keine Maintainer-Skripte. Aufruf: `sudo ff802-cc enable`
  bzw. `sudo ff802-cc disable`.

## Rückbau

```sh
sudo apt remove ff802-cc                 # manuelle Umschaltung entfernen
sudo apt purge ff802-cc-auto             # Automatik und ihren Zustand entfernen
sudo apt remove ff802-ucm-profile        # Profil, udev-Regel und Bindungen entfernen
```

Das Entfernen von `ff802-cc` lässt `ff802-cc-auto`, dessen Zustand und das
UCM-Profil unberührt; das ist in `deb_cc_package` geprüft. Der Rückbau
ändert weder den USB-Modus noch die Mixerwerte im Gerät: Werte, die die
Mixer-Initialisierung gesetzt hat (auch Input 9 auf allen Ausgängen),
bleiben bis zur nächsten Änderung bestehen; ein Programm zum Zurücksetzen
gibt es nicht. Den vollständigen Rückbau der anderen Pakete beschreiben
[t4_usb_mode.md](t4_usb_mode.md) und
[audio_t3_usb_port.md](audio_t3_usb_port.md).
