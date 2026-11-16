# fireface802-cc

Linux-Werkzeug zur Umschaltung des **ursprünglichen RME Fireface 802**
(nicht 802 FS) zwischen proprietärem USB-Modus und Class-Compliant-Modus.

## Projektstand

**Phase 1 (abgeschlossen):** Kodierung der Registerbefehle und
automatisierte Tests. Dieser Stand enthält **keinerlei USB-Kommunikation**.

Die USB-Kommunikation folgt in einer späteren Phase als eigenes Modul. Sie
übernimmt dann nur die fertig kodierten 4-Byte-Befehle aus der
Protokollbibliothek.

## Aufbau

```
include/ff802/reg_codec.h   Generische Registerkodierung (Adresse, Wert, Parität, LE)
include/ff802/cc_mode.h     Befehl für Register 0x3D44 (CC an/aus)
src/                        Implementierung der Bibliothek ff802_protocol
tests/                      CTest-Tests (ohne externes Test-Framework)
docs/protocol.md            Beschreibung der Protokollkodierung
```

Die Bibliothek `ff802_protocol` ist rein funktional. Sie führt keine
Ein-/Ausgabe durch, reserviert keinen Speicher und hängt nicht von libusb ab.

## Bauen und Testen

Die Entwicklung erfolgt ausschließlich im Dev-Container (`.devcontainer/`).

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

Compiler-Warnungen werden standardmäßig als Fehler behandelt. Abschalten
lässt sich das mit `-DFF802_WERROR=OFF`.

## Tests

| Test             | Inhalt                                                        |
|------------------|---------------------------------------------------------------|
| `test_reg_codec` | Byte-Reihenfolge, Bit-Layout, Parität (auch gegen eine unabhängige Referenz), Fehlerfälle, Erkennung aller Einzelbit-Fehler |
| `test_cc_mode`   | Byte-genauer Vergleich mit den Windows-Aufzeichnungen `01 00 44 BD` und `00 00 44 3D` |

Die erwarteten CC-Befehle stehen als feste Referenzwerte im Test. Sie
werden nicht mit der zu testenden Funktion berechnet.

## Sicherheitshinweise

- Phase 1 enthält keinen Code, der auf USB-Geräte zugreift.
- Der Dev-Container läuft ohne Privilegien und ohne durchgereichte Geräte.
- Das Verhalten des Geräts unter Linux ist noch nicht getestet.

## Protokoll

Siehe [docs/protocol.md](docs/protocol.md).

## Lizenz

Noch nicht festgelegt.
