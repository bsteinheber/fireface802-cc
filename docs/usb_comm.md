# USB-Kommunikation und Umschaltung (`ff802-cc`)

Dieses Dokument beschreibt, wie `ff802-cc` den Befehl zum Aktivieren des
Class-Compliant-Modus (CC-Modus) an ein ursprüngliches Fireface 802
überträgt. Außerdem behandelt es die Schutzmaßnahmen und die Risiken des
ersten echten Schreibzugriffs.

> **Stand:** Der Schreibpfad ist implementiert und mit einem simulierten
> USB-Backend getestet; `ff802-cc enable` wurde auf dem Host erfolgreich
> am Gerät ausgeführt. Im Standard-Build ist ein Schreibzugriff technisch
> unmöglich (siehe „Build-Sperre“).
>
> **Neu:** `ff802-cc disable` (CC → proprietär, Interface 3, mit
> libusb-Auto-Detach von `snd-usb-audio`) und der USB-Hub-Port-Reset nach
> `enable` sind in [cc_disable_port_reset.md](cc_disable_port_reset.md)
> beschrieben. Die Abschnitte unten beschreiben den Enable-Ablauf. Die dort
> genannten Verbote gelten weiter, mit einer einzigen Ausnahme:
> `libusb_set_auto_detach_kernel_driver` ist für Disable in
> `src/usb_comm_detach.c` erlaubt.

## Architektur

```
tools/ff802_cc.c          Kommandozeile, Bestätigung am Terminal, Ausgabe
        │
        ▼
src/cc_switch.c           Ablaufsteuerung (reines C, ohne libusb)
        │  ruft nur die Funktionstabelle ff802_usb_backend auf
        ▼
┌──────────────────────────┬───────────────────────────────┐
│ src/usb_comm.c           │ tests/fake_usb.c              │
│ libusb-Backend           │ simuliertes Backend für Tests │
│ (einziges Modul mit      │ (protokolliert jeden Aufruf,  │
│  open/claim/transfer)    │  Fehler beliebig injizierbar) │
└──────────────────────────┴───────────────────────────────┘
```

| Modul | Bibliothek | libusb | Aufgabe |
|-------|------------|--------|---------|
| `usb_backend.c` | `ff802_switch` | nein | Fehlercodes, Prüfung des Deskriptor-Layouts |
| `cc_switch.c` | `ff802_switch` | nein | Ablauf, Auswahl, Fehlerzuordnung, Exit-Codes |
| `usb_comm.c` | `ff802_comm` | ja | Umsetzung der Backend-Funktionen mit libusb |
| `ff802_cc.c` | Programm | – | Bedienung, Bestätigung |

`ff802-detect` linkt `ff802_comm` **nicht** und bleibt damit schreibfrei.

## Ablauf

| # | Schritt | `check` | `enable` | Abbruch bei |
|---|---------|:-------:|:--------:|-------------|
| 1 | Geräteliste lesen | ✓ | ✓ | libusb-Fehler |
| 2 | Zielgerät wählen: genau ein `2a39:3fcd` (oder `--device`) | ✓ | ✓ | keins, mehrere |
| 3 | Aktiven Konfigurationsdeskriptor prüfen (zwischengespeichert, kein Busverkehr) | ✓ | ✓ | Abweichung |
| 4 | Gerät öffnen | ✓ | ✓ | fehlende Rechte, getrennt |
| 5 | Kernel-Treiber auf Interface 1 abfragen | ✓ | ✓ | Treiber gebunden |
| 6 | Build-Sperre prüfen | – | ✓ | Schreiben nicht einkompiliert |
| 7 | Bestätigung: Benutzer tippt `ENABLE` | – | ✓ | andere Eingabe |
| 8 | Interface 1 beanspruchen (claim) | – | ✓ | belegt, Rechte |
| 9 | **Ein** Bulk-OUT-Transfer `01 00 44 BD` an `0x0C`, Timeout 1000 ms | – | ✓ | Timeout, unvollständig, Stall |
| 10 | Interface freigeben (release) | – | ✓ | nur Warnung |
| 11 | Gerät schließen | ✓ | ✓ | – |

Erwartet wird in Schritt 3: aktive Konfiguration `1`, Interface `1` mit
Alternate Setting `0`, Endpunkt `0x0C` vom Typ Bulk mit
`wMaxPacketSize = 512`. Ein Wert von 64 Bytes würde auf eine
Full-Speed-Verbindung hindeuten und führt ebenfalls zum Abbruch.

Es gilt:

- **Keine Wiederholung:** Ein fehlgeschlagener Transfer wird nie erneut
  versucht. Die Tests prüfen das für jeden Fehlerfall.
- **Kein Ablösen von Kernel-Treibern.** Ist ein Treiber gebunden, bricht
  das Programm ab.
- **Kein Reset und kein Wechsel von Konfiguration oder Alternate Setting.**
- **Ressourcen werden immer freigegeben:** Nach einem erfolgreichen Claim
  folgt immer ein Release, nach einem erfolgreichen Open immer ein Close.
- `check` beansprucht kein Interface und sendet nichts.

## Fehlerbehandlung

| Situation | Meldung | Exit |
|-----------|---------|:----:|
| kein Fireface 802 im proprietären Modus | `no Fireface 802 in proprietary mode found` | 1 |
| mehrere Geräte ohne `--device` | `more than one Fireface 802 found` | 1 |
| ungültige Kommandozeile, `enable` ohne Terminal | Usage-Text | 2 |
| Deskriptor weicht ab | `unexpected USB descriptor layout: …` | 3 |
| fehlende Zugriffsrechte (open oder claim) | `access to the USB device denied` | 3 |
| Kernel-Treiber auf Interface 1 | `a kernel driver is bound to interface 1` | 3 |
| Interface von anderem Programm belegt | `interface 1 is in use by another program` | 3 |
| Gerät getrennt bzw. Gerätedatei fehlt | `device was disconnected` | 3 |
| Timeout | `bulk transfer timed out` | 4 |
| weniger als 4 Bytes übertragen | `incomplete bulk transfer` | 4 |
| Endpunkt-Stall | `endpoint 0x0C stalled` | 4 |
| Benutzer bestätigt nicht | `aborted, not confirmed by user` | 5 |
| Build ohne Schreibfunktion | `USB writes are disabled in this build` | 5 |

Exit-Code 4 bedeutet: **Der Zustand des Geräts ist unbekannt.** Bei einem
Timeout kann das Gerät den Befehl trotzdem erhalten haben. Danach sollte
das Gerät aus- und wieder eingeschaltet und mit `ff802-detect` geprüft
werden, bevor ein neuer Versuch erfolgt.

## Build-Sperre (`FF802_ENABLE_USB_WRITE`)

Die CMake-Option `FF802_ENABLE_USB_WRITE` ist standardmäßig `OFF`. Dann

- werden `libusb_claim_interface`, `libusb_release_interface` und
  `libusb_bulk_transfer` nicht einkompiliert. Die Backend-Funktionen geben
  `FF802_USB_ERR_WRITE_DISABLED` zurück.
- bricht `ff802-cc enable` nach den Prüfungen ab, noch vor der Bestätigung.
- meldet `ff802-cc --version` `(USB writes disabled)`.
- stellt der Test `cc_binary_symbols` mit `nm` sicher, dass die fertige
  Binärdatei diese Funktionen tatsächlich nicht importiert.

Ein schreibfähiger Build entsteht erst durch eine bewusste Neukonfiguration
in einem eigenen Build-Verzeichnis:

```sh
cmake -S . -B build-write -DFF802_ENABLE_USB_WRITE=ON
cmake --build build-write
ctest --test-dir build-write --output-on-failure
```

Das ist erst nach ausdrücklicher Freigabe des ersten Schreibtests
vorgesehen.

## Statische Sicherheitsprüfungen

| Test | Prüft |
|------|-------|
| `check_forbidden_usb_calls` | Ablösen von Treibern, Reset, Konfigurations- und Alt-Setting-Wechsel, Control-, Interrupt- und asynchrone Transfers sowie String-Deskriptoren kommen in **keiner** Quelldatei vor. `open`, `claim`, `release`, `bulk_transfer` und `kernel_driver_active` kommen **nur** in `src/usb_comm.c` vor. `ff802/usb_comm.h` wird nur von `usb_comm.c` und `ff802_cc.c` eingebunden. |
| `detect_binary_symbols` | `ff802-detect` importiert weder `libusb_open` noch Claim- oder Transfer-Funktionen. |
| `cc_binary_symbols` | `ff802-cc` importiert keine verbotenen Funktionen und bei geschlossener Sperre keine Claim-, Release- oder Bulk-Funktionen. |
| `test_write_gate` | Das libusb-Backend meldet die konfigurierte Schreibfähigkeit. |

## Zugriffsrechte

`libusb_open` öffnet `/dev/bus/usb/BBB/DDD` zum Lesen und Schreiben.
Unter Kubuntu gehört diese Datei standardmäßig `root` mit den Rechten
`0664`. Ohne Root-Rechte meldet daher schon `ff802-cc check`
`access to the USB device denied`.

**Aktuelles Vorgehen (seit 2026-11-19):** `ff802-cc` läuft mit `sudo`.
Das gilt für `enable`, `enable --no-port-reset` und `disable`, wie bei
den Hardwaretests. `enable` braucht root außerdem für den USB-Hub-Port-Reset
(sysfs `disable`). Ohne Schreibrecht auf den Port lehnt `enable` ab, bevor
etwas gesendet wird. Es gibt dafür weder eine udev-Regel noch eine
polkit-Konfiguration.

Das Projekt installiert **keine** udev-Regel. Eine Regel ersetzt `sudo`
nur für den USB-Zugriff, nicht für den Port-Reset. Falls sie nach eigener
Entscheidung auf dem Host eingerichtet werden soll, ist eine mögliche
Variante (Datei `/etc/udev/rules.d/70-fireface802.rules`):

```
# RME Fireface 802, proprietary USB mode: access for the logged-in user
SUBSYSTEM=="usb", ATTR{idVendor}=="2a39", ATTR{idProduct}=="3fcd", TAG+="uaccess"
```

Danach: `sudo udevadm control --reload` und das Gerät neu anstecken.
Diese Änderung am Host wird vom Projekt nicht automatisch vorgenommen.

## Bedienung

```sh
# Diagnose: prüft alles, beansprucht nichts, sendet nichts
build/ff802-cc check

# Bei mehreren Geräten: Auswahl nach Bus und Adresse aus ff802-detect
build/ff802-cc --device 3:18 check

# Umschaltung (nur in einem Build mit FF802_ENABLE_USB_WRITE=ON wirksam)
build/ff802-cc enable
```

`enable` verlangt ein interaktives Terminal (stdin). Es gibt bewusst keine
Option wie `--yes`. Vor dem Senden zeigt das Programm Zielgerät, Interface,
Endpunkt und Bytes an. Nur die exakte Eingabe `ENABLE` löst die
Übertragung aus.

## Risiken beim ersten echten Schreibzugriff

1. **Unvollständige Protokollkenntnis.** Der Befehl stammt aus einer
   Windows-Aufzeichnung. Möglicherweise erwartet das Gerät vorher eine
   Initialisierung durch den Windows-Treiber (z. B. Statusabfragen oder
   andere Registerzugriffe). Dann könnte es den Befehl ignorieren oder
   anders auswerten.
2. **Rückweg.** Ob die Einstellung dauerhaft im Gerät gespeichert wird und
   wie sie zurückgesetzt wird, ist unbekannt. Im CC-Modus meldet sich das
   Gerät vermutlich als `0424:3fdd`. Dieses Werkzeug kann es dort nicht
   ansprechen. Vor dem Test sollte ein Rückweg geklärt sein, z. B. über den
   Windows-Treiber oder ein Gerätemenü.
   *T4:* Geklärt, siehe [t4_disable.md](t4_disable.md). TotalMix FX setzt das
   Gerät auf Windows oder macOS automatisch zurück; alternativ die ARC
   (Prog + B (Mono) beim Einschalten). Einen belegten USB-Befehl dafür gab es
   nicht.
   *Seit 2026-11-19:* `ff802-cc disable` schaltet per USB-MIDI-SysEx zurück
   (am Gerät getestet), siehe [cc_disable_port_reset.md](cc_disable_port_reset.md).
3. **CC-Kennung unbestätigt.** Dass `0424:3fdd` die CC-Kennung ist und
   `snd-usb-audio` das Gerät unterstützt, ist unter Linux nicht geprüft.
   *Inzwischen bestätigt:* Nach der Umschaltung meldet sich das Gerät als
   `0424:3fdd` und wird von `snd-usb-audio` als Karte eingebunden (T1).
4. **Alternate Setting.** Das Werkzeug setzt kein Alternate Setting. Es
   geht davon aus, dass Interface 1 nach der Enumeration auf Setting 0
   steht. Hat der Windows-Treiber vorher ein Setting aktiv gewählt, weicht
   die Situation ab.
5. **Unklarer Zustand nach Fehlern.** Ein Timeout schließt nicht aus, dass
   der Befehl angekommen ist. Deshalb wird nicht wiederholt.
6. **Lange Hub-Kette.** Das Gerät hängt derzeit an Port `3-2.4.1.1.3`,
   also hinter vier Hubs. Für den ersten Test ist ein direkter Anschluss
   am Rechner ratsam, um Timeouts und Verbindungsabbrüche
   unwahrscheinlicher zu machen.
7. **Firmware-Unterschiede.** Das Gerät meldet `bcdDevice=0.01`. Ob die
   Windows-Aufzeichnung mit derselben Firmware entstand, ist unbekannt.
8. **Zugriffsrechte.** Für den Test ist eine Änderung am Host nötig, etwa
   eine udev-Regel. Sie sollte bewusst eingerichtet und danach wieder
   entfernt oder dokumentiert werden.
9. **Schreibfähiger Build.** Der Build mit `FF802_ENABLE_USB_WRITE=ON`
   sollte in einem eigenen Verzeichnis entstehen und nach dem Test
   entfernt werden, damit er nicht versehentlich weiterverwendet wird.
