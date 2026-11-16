# fireface802-cc

Linux-Werkzeug zur Umschaltung des **ursprünglichen RME Fireface 802**
(nicht 802 FS) zwischen proprietärem USB-Modus und Class-Compliant-Modus.

## Projektstand

| Phase | Inhalt | Stand |
|-------|--------|-------|
| 1  | Kodierung der Registerbefehle und Tests | abgeschlossen |
| 2a | Diagnoseprogramm `ff802-detect`: lesende Geräteerkennung über libusb | abgeschlossen |
| 2b | Senden des Umschaltbefehls | **nicht implementiert** |

Das Projekt enthält bisher **keinen Code, der Daten an ein USB-Gerät sendet**.

## Aufbau

```
include/ff802/reg_codec.h   Registerkodierung (Adresse, Wert, Parität, LE)
include/ff802/cc_mode.h     Befehl für Register 0x3D44 (CC an/aus)
include/ff802/device_id.h   USB-Kennungen und Zuordnung VID:PID -> Modus
include/ff802/detect.h      Erkennung und Bericht auf einfachen Gerätedatensätzen
include/ff802/usb_enum.h    Lesende libusb-Enumeration (einziges libusb-Modul)
src/                        Implementierung der Bibliotheken
tools/ff802_detect.c        Kommandozeilenprogramm ff802-detect
tests/                      CTest-Tests und Sicherheitsprüfung
docs/protocol.md            Beschreibung der Protokollkodierung
docs/detection.md           Beschreibung der Geräteerkennung
```

Bibliotheken:

| Bibliothek       | Inhalt                         | libusb |
|------------------|--------------------------------|--------|
| `ff802_protocol` | `reg_codec`, `cc_mode`         | nein   |
| `ff802_detect`   | `device_id`, `detect`          | nein   |
| `ff802_usb`      | `usb_enum` (nur Enumeration)   | ja     |

## Bauen und Testen (im Dev-Container)

Die Entwicklung erfolgt ausschließlich im Dev-Container (`.devcontainer/`).

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

- Compiler-Warnungen werden standardmäßig als Fehler behandelt. Abschalten
  lässt sich das mit `-DFF802_WERROR=OFF`.
- Ohne libusb oder mit `-DFF802_BUILD_TOOLS=OFF` werden nur die reinen
  Bibliotheken und ihre Tests gebaut.

## Diagnoseprogramm auf dem Kubuntu-Host ausführen

Das im Container gebaute Programm liegt im eingebundenen Projektverzeichnis
und kann direkt auf dem Host gestartet werden, ohne Entwicklungswerkzeuge
und ohne Root-Rechte:

```sh
/home/bernhard/Develop/fireface802-cc/build/ff802-detect
```

Beispielausgabe:

```
Scanned 26 USB device(s).
Bus 003 Device 018: ID 2a39:3fcd mode=proprietary port=3-2.4.1.1.3 bcdDevice=0.01
  Fireface 802, proprietary USB mode
Found 1 Fireface 802 device(s).
```

Rückgabewerte: `0` Fireface 802 gefunden, `1` keins gefunden,
`2` Bedienfehler oder Enumeration fehlgeschlagen.

**Voraussetzungen auf dem Host:**

- Laufzeitbibliothek `libusb-1.0-0` (Paket `libusb-1.0-0`, keine
  Entwicklungsdateien). Sie ist auf Kubuntu normalerweise vorinstalliert.
  Prüfen lässt sich das mit `ldd build/ff802-detect`.
- glibc ab Version 2.34 (Kubuntu 22.04 oder neuer), x86-64.

Das Programm ist dynamisch gelinkt. Eine statische Variante ist mit den
Ubuntu-Paketen nicht möglich, weil es keine statische `libudev` gibt.

## Tests

| Test | Inhalt |
|------|--------|
| `test_reg_codec` | Byte-Reihenfolge, Bit-Layout, Parität, Fehlerfälle, Erkennung aller Einzelbit-Fehler |
| `test_cc_mode` | Byte-genauer Vergleich mit den Windows-Aufzeichnungen `01 00 44 BD` und `00 00 44 3D` |
| `test_device_id` | Zuordnung VID:PID zum Modus, ähnliche, aber falsche Kennungen |
| `test_detect` | Erkennung und Bericht mit simulierten Gerätelisten (ohne Hardware) |
| `check_forbidden_usb_calls` | Schlägt fehl, wenn eine Quelldatei libusb-Funktionen zum Öffnen, Beanspruchen, Zurücksetzen oder für Transfers verwendet |
| `cli_*` | `--help`, `--version`, unbekannte Argumente (ohne USB-Zugriff) |

Keiner der automatisierten Tests benötigt angeschlossene USB-Hardware.

## Sicherheitshinweise

- `ff802-detect` liest nur die beim Enumerieren zwischengespeicherten
  Gerätedeskriptoren. Es öffnet kein Gerät, beansprucht kein Interface,
  löst keinen Kernel-Treiber ab, setzt nichts zurück und sendet keine Daten.
- Der Dev-Container läuft ohne Privilegien und ohne durchgereichte Geräte.
  Siehe auch den Hinweis zu `/sys` in [docs/detection.md](docs/detection.md).
- Ob die Umschaltung unter Linux funktioniert, ist noch nicht getestet.

## Dokumentation

- [docs/protocol.md](docs/protocol.md): Registerprotokoll
- [docs/detection.md](docs/detection.md): Geräteerkennung

## Lizenz

Noch nicht festgelegt.
