# `ff802-cc disable` und USB-Hub-Port-Reset nach `ff802-cc enable`

> **Stand:** Beide Verfahren wurden am 2026-11-19 von Hand am realen Gerät
> erfolgreich getestet: Disable mit dem Python-Skript
> `tools/hw-test/ff802-disable-test.py` (nicht eingecheckt), der Port-Reset
> mit `echo 1/0 | sudo tee …/disable`. Die Umsetzung in `ff802-cc` ist im
> Dev-Container mit simuliertem USB-Backend und nachgebautem sysfs
> getestet. Danach, ebenfalls am 2026-11-19, wurde die integrierte Fassung
> (`build-write/ff802-cc`) am Gerät erfolgreich getestet:
> - `disable`: 16 Bytes übertragen, Wechsel von `0424:3fdd` zu `2a39:3fcd`.
> - `enable`: 4 Bytes übertragen, Port-Reset, Wechsel zu `0424:3fdd`.
> - PipeWire hat danach alle sechs Ausgänge wieder erkannt.
>
> Als Debian-Paket `ff802-cc` 0.4.0 ist sie vorbereitet und nicht
> installiert, siehe [packages.md](packages.md).

## Übersicht

| Befehl | Gerät vorher | Weg | danach |
|---|---|---|---|
| `sudo ff802-cc enable` | `2a39:3fcd` (proprietär) | Interface 1, Bulk OUT `0x0C`, `01 00 44 BD` (unverändert), dann Port-Reset | `0424:3fdd` (CC) |
| `sudo ff802-cc enable --no-port-reset` | `2a39:3fcd` | wie bisher, ohne Port-Reset | nach Trennen von Hand `0424:3fdd` |
| `sudo ff802-cc disable` | `0424:3fdd` (CC) | Interface 3 (USB-MIDI), Bulk OUT `0x07`, 16 Byte | sofort `2a39:3fcd`, ohne Reset |

Beide Befehle brauchen ein interaktives Terminal und das Bestätigungswort
(`ENABLE` bzw. `DISABLE`). Sie laufen mit `sudo`, wie die Hardwaretests.
Es gibt keine udev-Regel und keine polkit-Konfiguration dafür.

## Disable

### Der Befehl

```
13 F0 00 20 | 13 0D 10 00 | 13 7E 7F 13 | 13 6A 0B F7     USB-MIDI-Pakete, Kabel 1
F0 00 20 0D 10 00  7E 7F 13 6A 0B  F7                    SysEx, Hersteller 00 20 0D (RME)
7E 7F 13 6A 0B = 0xBD44FFFE                               7-Bit-Gruppen, LSB zuerst
0xBD44FFFE = Register 0x3D44, Wert 0xFFFE                 gültige ungerade Parität
```

- Es ist dasselbe Register wie bei Enable (`0x3D44`), aber als SysEx auf
  dem MIDI-Interface. `ff802_cc_disable_midi()` erzeugt die Bytes aus dem
  Registercodec.
- Jedes Paket trägt wie im Test das Kopfbyte `0x13` (Kabel 1, CIN 3). Der
  USB-MIDI-Standard sähe für SysEx CIN 4 bzw. 7 vor.
- Die Bedeutung des Werts `0xFFFE` ist nicht dokumentiert.
- Der Test `test_cc_mode` vergleicht die Bytes mit dem getesteten
  Transfer.

### Ablauf (`src/disable_check.c`, `src/cc_disable.c`)

1. **Ohne Terminal** wird abgelehnt (Exit 2), noch bevor sysfs gelesen wird.
2. **Vorprüfung in sysfs und `/proc/asound`** (nur lesend, vor jedem USB-Zugriff):
   - genau ein originales Fireface 802 im CC-Modus: `0424:3fdd`, Hersteller
     `RME`, Produkt `Fireface 802 (…)`, High Speed; sonst Auswahl mit
     `--device BUS:ADDRESS`
   - aktive Konfiguration 1; Interface 3 ist USB-MIDI Streaming (01/03)
     mit `ep_07` Bulk OUT und 512 Byte
   - Interface 3 ist an genau `snd-usb-audio` gebunden
   - **Verweigert, wenn die T4-Automatik an ist** (`ff802-cc-auto on`).
     Sie würde das Gerät nach dem Wechsel sofort zurückschalten.
     Hinweis: `ff802-cc-auto off`.
   - **Verweigert, wenn Audio läuft:** ein PCM der Karte des Geräts im
     Zustand PREPARED, RUNNING, XRUN, DRAINING oder PAUSED.
3. **libusb:**
   - genau ein `0424:3fdd` an Bus und Adresse aus der Vorprüfung
   - Deskriptor von Interface 3 (`0x07`, Bulk, 512 Byte)
   - Gerät öffnen; den Kerneltreiber nur protokollieren
4. **Bestätigung:** Ziel und die 16 Bytes werden angezeigt, dann `DISABLE`
   eintippen.
5. **Erneute Prüfung danach:** die ganze Vorprüfung, derselbe Anschluss,
   dasselbe Gerät wie in libusb, genau diese Bytes.
6. **Beanspruchen von Interface 3 mit libusb-Auto-Detach.** libusb löst
   `snd-usb-audio` nur von diesem Interface. Die ALSA-Karte des Fireface
   verschwindet dabei.
7. **Genau ein Bulk-Transfer**, 16 Byte, Timeout 3000 ms, keine Wiederholung.
8. **Freigabe:** libusb bindet den Treiber wieder an. Schlägt die Freigabe
   fehl, weil das Gerät schon gewechselt hat, gilt das als erwartet und
   wird nur als Hinweis ausgegeben.
9. **Bis zu 10 s warten**, bis am selben Port `2a39:3fcd` erscheint.

Der Ablauf liegt in einer eigenen Datei. `cc_switch.c` (Enable) ist
unverändert.

### Kerneltreiber-Ablösung, eng begrenzt

- **Einzige Ablösefunktion:** Erlaubt ist nur
  `libusb_set_auto_detach_kernel_driver` in `src/usb_comm_detach.c`.
  Eingeschaltet wird sie nur durch `ff802_usb_comm_allow_detach_claim()`,
  und das ruft nur `ff802-cc disable` auf.
- **Was verboten bleibt**, überall, auch für Disable:
  `libusb_detach_kernel_driver`, Reset, Konfigurations- und
  Alternate-Setting-Wechsel, Control-Transfers.
- **Prüfung im Quelltext:** `check_forbidden_usb_calls`.
- **Prüfung im fertigen Programm:**
  - `cc_binary_symbols`: `ff802-cc` enthält den Aufruf nur im
    schreibfähigen Build.
  - `autoswitch_binary_symbols`: Das T4-Programm `ff802-cc-autoswitch`
    enthält ihn nie.

## Port-Reset nach Enable (`src/usb_port.c`)

1. **Vor dem Senden:**
   - das Gerät bestimmen, das beschrieben wird (genau ein `2a39:3fcd`,
     oder `--device`)
   - den Hub-Port aus dem Kernelnamen ableiten und prüfen, dass er
     beschreibbar ist (root)

   Fehlt etwas davon, sendet `enable` nichts (Exit 1 oder 3). Mit
   `--no-port-reset` lässt sich trotzdem senden, dann ohne Port-Reset.

   ```
   3-2.4.1.1.3  ->  Hub 3-2.4.1.1, Port 3
                ->  /sys/bus/usb/devices/3-2.4.1.1/3-2.4.1.1:1.0/3-2.4.1.1-port3
   3-2          ->  Root-Hub usb3, Port 2  ->  …/usb3/3-0:1.0/usb3-port2
   ```

   Der Port gilt nur, wenn sein Link `device` auf genau dieses Gerät zeigt
   (`realpath` gleich) und das Attribut `disable` existiert. Ein Port mit
   einem anderen Gerät oder ohne Link wird abgelehnt.
2. **Bestätigung:** Der Text nennt den Port. Nach `ENABLE` muss das von
   libusb gewählte Gerät das vorab geprüfte sein.
3. **Übertragung wie bisher** (`01 00 44 BD`).
4. **Vor dem Schreiben noch einmal prüfen:** derselbe Kernelname, Bus,
   Gerätenummer und USB-ID, und der Port gehört weiter zu diesem Gerät.
5. **`disable` = `1`, 2 s warten, `disable` = `0`.**
   - `0` wird in jedem Fall versucht, auch wenn `1` fehlschlug.
   - SIGINT, SIGTERM, SIGHUP und SIGQUIT sind währenddessen blockiert und
     wirken erst danach. Strg+C kann den Port also nicht abgeschaltet
     zurücklassen.
6. **Bis zu 15 s warten**, bis am selben Port `0424:3fdd` erscheint.

Es wird nichts anderes beschrieben: kein `usbreset`, kein `authorized`,
kein Treiber-`bind`/`unbind`. Die statische Prüfung erlaubt das
Schreiben von `disable` nur in `src/usb_port.c`.

Fehlermeldungen:

| Fall | Meldung | Exit |
|---|---|---|
| Port nicht bestimmbar oder nicht beschreibbar (vor dem Senden) | Grund, Hinweis auf `sudo` / `--no-port-reset` | 3 |
| Gerät nach dem Transfer nicht mehr dasselbe oder Port gehört nicht mehr dazu | Port nicht zurückgesetzt | 6 |
| `1` nicht schreibbar | Port nicht zurückgesetzt, Fehlercode | 6 |
| `0` nicht schreibbar | **wichtig:** Port evtl. abgeschaltet, Befehl zum Wiederanschalten | 6 |
| kein `0424:3fdd` nach 15 s, oder andere ID | Hinweis: von Hand trennen, `ff802-detect` | 6 |

## Exit-Codes von `ff802-cc`

| Exit | Bedeutung |
|---|---|
| 0 | Erfolg (bei `enable` und `disable`: neuer Modus erkannt) |
| 1 | kein passendes Gerät, oder mehrere |
| 2 | Bedienfehler, kein Terminal |
| 3 | Vorbedingung nicht erfüllt (Automatik an, Audio läuft, Port nicht beschreibbar, …), nichts gesendet |
| 4 | Übertragung fehlgeschlagen, Gerätezustand unbekannt |
| 5 | nicht gesendet (abgebrochen, Schreiben im Build gesperrt) |
| 6 | übertragen, aber der neue Modus wurde nicht erkannt |

## Tests (Container, ohne Hardware)

| Test | Inhalt |
|---|---|
| `test_cc_mode` | Disable-Bytes gleich dem getesteten Transfer; Dekodierung zu `0x3D44` = `0xFFFE` |
| `test_cc_disable` | Aufruffolge, genau ein Transfer an `0x07` mit 3000 ms, nur Beanspruchen mit Auto-Detach, alle Fehlerfälle; Enable benutzt die Ablösung nie |
| `test_disable_check` | Identität, Eindeutigkeit, Interface 3, Treiber, T4 an, Audio läuft (nachgebautes sysfs/`/proc/asound`) |
| `test_usb_port` | Port ermitteln (Hub, Root-Hub, fremder Port, mehrdeutig), Reset mit 2 s, fehlschlagende Schreibvorgänge, SIGTERM während der Auszeit, Warten auf die neue ID, Gesamtablauf nach Enable |
| `cc_cli_*` | Kommandozeile ohne Terminal, `--no-port-reset` nur mit `enable` |
| `check_forbidden_usb_calls`, `cc_binary_symbols`, `autoswitch_binary_symbols` | Grenzen von Ablösung und sysfs-Schreibzugriffen, im Quelltext und in den Programmen |

Kein automatisierter Test schreibt auf USB oder in das echte `/sys`.

## Noch offen

- Installation des Pakets `ff802-cc` 0.4.0 auf dem Host (Freigabe nötig),
  danach nur noch `/usr/bin/ff802-cc` verwenden.
- Die Bedeutung von `0xFFFE` und der Kodierung mit CIN 3.
- Die T4-Automatik nutzt den Port-Reset nicht (unverändert). Ihre
  README.Debian ist ab `ff802-cc-auto` 0.1.1 korrigiert.
