# Geräteerkennung (`ff802-detect`)

Dieses Dokument beschreibt, wie `ff802-detect` ein ursprüngliches
RME Fireface 802 erkennt und welchen Betriebsmodus es meldet.

## USB-Kennungen

| Modus | VID:PID | Status |
|-------|---------|--------|
| Proprietärer USB-Modus | `2a39:3fcd` | bestätigt, unter Linux erkannt |
| Class-Compliant-Modus | `0424:3fdd` | **vermutet**, unter Linux noch nicht bestätigt |

`0x2A39` ist die Hersteller-ID von RME. `0x0424` gehört Microchip (SMSC) und
wird von vielen fremden Geräten verwendet, etwa von USB-Hubs. Für den
CC-Modus wird deshalb nur die genaue Kombination `0424:3fdd` gezählt.

Geräte mit der RME-Hersteller-ID, aber einer anderen Produkt-ID werden als
„other RME device (not supported)“ aufgeführt, zum Beispiel ein 802 FS oder
ein anderes Fireface. Sie zählen nicht als Treffer.

Die Zuordnung erfolgt in `ff802_classify()` (`src/device_id.c`).

## Ablauf

```
libusb (nur Enumeration)          reine Logik, ohne libusb
┌───────────────────────┐         ┌─────────────────────────────┐
│ usb_enum.c            │ records │ detect.c / device_id.c      │
│  libusb_init          │ ──────► │  ff802_classify()           │
│  libusb_get_device_list         │  ff802_find_devices()       │
│  Deskriptor kopieren  │         │  ff802_report() -> stdout   │
│  libusb_exit          │         │  Rückgabewert               │
└───────────────────────┘         └─────────────────────────────┘
```

1. `ff802_usb_enumerate()` holt die Geräteliste. Für jedes Gerät werden
   Busnummer, Geräteadresse, Portpfad sowie `idVendor`, `idProduct` und
   `bcdDevice` aus dem Gerätedeskriptor in einen Datensatz
   `ff802_usb_dev` kopiert. Danach werden alle libusb-Ressourcen freigegeben.
2. `ff802_report()` ordnet jeden Datensatz einem Modus zu und gibt die
   Treffer aus.

Durch diese Trennung lässt sich die gesamte Erkennungslogik mit
simulierten Gerätelisten testen (`tests/test_detect.c`), ohne Hardware und
ohne libusb.

## Ausgabe

```
Scanned 26 USB device(s).
Bus 003 Device 018: ID 2a39:3fcd mode=proprietary port=3-2.4.1.1.3 bcdDevice=0.01
  Fireface 802, proprietary USB mode
Found 1 Fireface 802 device(s).
```

| Feld | Bedeutung |
|------|-----------|
| `Bus`, `Device` | Busnummer und Geräteadresse, wie bei `lsusb` |
| `ID` | VID:PID |
| `mode` | `proprietary`, `class-compliant` oder `rme-other` |
| `port` | Portpfad in sysfs-Schreibweise (`<bus>-<port>.<port>…`), entspricht `/sys/bus/usb/devices/<port>` |
| `bcdDevice` | Geräteversion aus dem Deskriptor (BCD) |

Die Geräteadresse ändert sich bei jedem erneuten Anstecken. Der Portpfad
bleibt dagegen gleich, solange das Gerät am selben Anschluss hängt.

## Rückgabewerte

| Wert | Bedeutung |
|------|-----------|
| 0 | mindestens ein Fireface 802 gefunden (beliebiger Modus) |
| 1 | Enumeration erfolgreich, aber kein Fireface 802 gefunden |
| 2 | Bedienfehler oder libusb-Fehler |

Wird überhaupt kein USB-Gerät gefunden, weist das Programm darauf hin, dass
wahrscheinlich kein Zugriff auf den USB-Bus besteht.

## Sicherheitseigenschaften

- Verwendet werden ausschließlich `libusb_init`, `libusb_exit`,
  `libusb_get_device_list`, `libusb_free_device_list`,
  `libusb_get_device_descriptor`, `libusb_get_bus_number`,
  `libusb_get_device_address`, `libusb_get_port_numbers` und
  `libusb_strerror`.
- Den Gerätedeskriptor hält libusb seit Version 1.0.16 beim Enumerieren im
  Speicher vor. Das Auslesen erzeugt keinen Verkehr auf dem USB-Bus.
- String-Deskriptoren (Hersteller, Produktname, Seriennummer) werden
  **nicht** gelesen, weil das ein geöffnetes Gerät und Control-Transfers
  erfordern würde.
- Der Test `check_forbidden_usb_calls` stellt sicher, dass Funktionen zum
  Öffnen, Beanspruchen und Übertragen nur im Kommunikationsmodul
  `src/usb_comm.c` vorkommen (siehe [usb_comm.md](usb_comm.md)). Ablösen von
  Kerneltreibern, Zurücksetzen und Konfigurationswechsel sind überall
  verboten. `ff802-detect` linkt das Kommunikationsmodul nicht. Der Test
  `detect_binary_symbols` prüft mit `nm`, dass die fertige Binärdatei weder
  `libusb_open` noch Claim- oder Transfer-Funktionen importiert.
- Root-Rechte sind nicht nötig. Die Enumeration liest nur Informationen,
  die für alle Benutzer lesbar sind.

## Hinweis zum Dev-Container

Im Dev-Container gibt es kein `/dev/bus/usb`. Es sind also keine
USB-Gerätedateien durchgereicht. Docker bindet `/sys` jedoch
schreibgeschützt ein. libusb liest daraus Topologie und Deskriptoren, daher
erkennt `ff802-detect` auch **im Container** die am Host angeschlossenen
Geräte. Ein Zugriff auf die Geräte selbst ist im Container nicht möglich.

## Offene Punkte

- Die CC-Kennung `0424:3fdd` muss unter Linux noch bestätigt werden, z. B.
  durch Umschalten unter Windows und anschließendes `ff802-detect` oder
  `lsusb` unter Linux.
- Im proprietären Modus meldet das Gerät `bcdDevice=0.01`. Ob dieser Wert
  mit der Firmware-Version zusammenhängt, ist unbekannt.
