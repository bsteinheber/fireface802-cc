# T4: USB-Modusverwaltung des Fireface 802 unter Kubuntu

> **Stand:** Im Dev-Container vollständig entwickelt und getestet
> (Simulation von sysfs, USB, udev/systemd-Aufrufen und Desktop). Auf dem
> Host ist `ff802-cc-auto` 0.1.0 **installiert und aktiviert**. Die
> automatische Aktivierung des Class-Compliant-Modus wurde auf dem Host
> **erfolgreich getestet**. Nach dem Umschaltbefehl ist gegebenenfalls ein
> physisches Trennen und Wiederanschließen der USB-Verbindung erforderlich
> (siehe [Ereignisablauf](#ereignisablauf-automatik-an-gerät-proprietär)).
> Welche Einzelschritte des [Host-Testplans](#host-testplan) ausgeführt
> wurden, ist nicht dokumentiert.

T4 hat zwei Teile:

| Teil | Funktion | Stand |
|---|---|---|
| A | Automatischer Wechsel proprietär → Class-Compliant beim Anschließen und beim Systemstart | implementiert (Paket `ff802-cc-auto`) |
| B | `ff802-cc disable`: Class-Compliant → proprietär | seit 2026-11-19 als eigener, manueller Befehl umgesetzt (nicht Teil der Automatik), siehe [cc_disable_port_reset.md](cc_disable_port_reset.md); verweigert bei eingeschalteter Automatik |

Die Automatik schaltet **ausschließlich** in Richtung Class-Compliant.

## USB-Betriebsarten

| Betriebsart | USB-ID | Erkennung (sysfs) | Was T4 tut |
|---|---|---|---|
| proprietär (RME-Treiber) | `2a39:3fcd` | Hersteller `RME`, Produkt `Fireface 802 (<Seriennummer>)`, 480 Mbit/s | sendet `01 00 44 BD` einmal, wenn die Automatik an ist |
| Class-Compliant | `0424:3fdd` | wie oben (`0424` ist Microchip, deshalb zählen die Texte mit) | nichts; nur Bestätigung, wenn vorher umgeschaltet wurde |
| alles andere (802 FS, andere RME, andere `0424`) | – | – | nichts |

Bus- und Gerätenummer ändern sich bei jedem Anschluss. Sie dienen nur
dazu, einen einzelnen Anschlussvorgang zu kennzeichnen, nie zur Auswahl
eines Geräts.

## Architektur

```
 Kernel (USB add)                                       Benutzersitzung (Plasma)
      │                                                         ▲
      ▼                                                         │ notify-send / busctl
 udev: 70-ff802-cc-auto.rules                      ff802-cc-notify (Benutzer-Unit)
   nur TAG+="systemd",                               ▲  zeigt jedes Ereignis einmal,
   SYSTEMD_WANTS=ff802-cc-auto@<port>:<bus>:<dev>    │  prüft Relevanz, kein USB
      │   (führt nichts aus)                         │
      ▼                                              │ PathChanged
 systemd: ff802-cc-auto@.service (oneshot, root     │
   ohne Capabilities, nur usb_device-Knoten,         │
   kein Netz, 30 s)                                  │
      │                                              │
      ▼                                              │
 /usr/libexec/ff802-cc/ff802-cc-autoswitch --service %i
   Sperre → sysfs → Identität → Automatik an? → Anschluss vermerken
   → USB-Backend → Deskriptor, Öffnen, Kerneltreiber
   → erneute Prüfung direkt vor dem Schreiben → 1 Bulk-Transfer
   → /run/ff802-cc-auto/event ──────────────────────┘

 ff802-cc-auto on|off|status  →  /var/lib/ff802-cc-auto/mode  (polkit/pkexec)
```

Begründung der Wahl:

- **udev führt nichts aus.** USB-Schreibzugriffe und lange Prozesse
  gehören nicht in udev. `SYSTEMD_WANTS` übergibt an systemd. Beim
  Systemstart erzeugt das Coldplug von udev die gleichen `add`-Ereignisse,
  dadurch sind bereits angeschlossene Geräte abgedeckt.
- **systemd führt kontrolliert und stark abgeschirmt aus.** Es gibt eine
  Instanz pro Anschluss, ohne Wiederholung (`Restart` fehlt) und mit
  Zeitlimit. `systemd-analyze security` bewertet die Unit mit 1,2 („OK“).
- **Die Benachrichtigung ist entkoppelt.** Der Systemdienst schreibt nur
  eine Datei. Die Benutzersitzung liest sie selbst. Es gibt also keine
  festen UIDs, Home-Verzeichnisse oder D-Bus-Adressen, und root greift
  nie in eine Benutzersitzung. Eine Umschaltung beim Start vor der
  Anmeldung wird nach der Anmeldung angezeigt, solange sie noch relevant
  ist. Ein Fehler der Benachrichtigung kann keinen Schreibzugriff
  auslösen.
- **Die Logik liegt in C ohne libusb** (`src/auto_*.c`). Sie ist mit
  simuliertem sysfs und USB vollständig testbar. Den eigentlichen Ablauf
  (Deskriptorprüfung, Öffnen, Claim, Write, Release) übernimmt
  unverändert `cc_switch.c`, das schon `ff802-cc enable` verwendet.

## Ereignisablauf (Automatik an, Gerät proprietär)

1. Das Fireface wird angeschlossen, oder es ist beim Start schon
   angeschlossen. udev startet `ff802-cc-auto@3-2.4.1.1.3:003:019.service`.
2. Der Dienst nimmt die Sperre `/run/ff802-cc-auto/lock`. Gleichzeitige
   Ereignisse laufen dadurch nacheinander.
3. Er liest sysfs: Ist das noch derselbe Anschluss (Bus und Gerätenummer
   wie im Ereignis)? Passen USB-ID, `RME`, `Fireface 802 (…)` und
   480 Mbit/s?
4. Er liest `/var/lib/ff802-cc-auto/mode`. Nur genau `on` gilt als an.
5. Er legt `/run/ff802-cc-auto/attempts/<port>_<bus>_<dev>` an. Existiert
   der Eintrag schon, endet der Lauf: höchstens ein Versuch pro
   Anschluss, auch nach Fehlern.
6. Erst jetzt wird libusb initialisiert. Der Dienst wählt das Gerät
   anhand von Bus und Adresse aus dem Ereignis, prüft den Deskriptor
   (Konfiguration 1, Interface 1, Bulk OUT `0x0C`, 512 Byte), öffnet es
   und prüft, ob kein Kerneltreiber gebunden ist.
7. Unmittelbar vor Claim und Transfer prüft er erneut: Die Automatik ist
   noch an, sysfs zeigt denselben Anschluss unverändert und weiterhin
   proprietär, das geöffnete Gerät hat denselben Port, und die Bytes sind
   genau `01 00 44 BD`.
8. Er führt einen Bulk-Transfer aus (Timeout 1 s), danach Release und
   Close.
9. Er schreibt `pending` und das Ereignis `prepared`. Plasma zeigt
   **„Fireface 802 – Class-Compliant-Modus vorbereitet“**: „Bitte die
   USB-Verbindung des Fireface kurz trennen und wieder anschließen.“
10. Nach dem erneuten Anschließen erscheint das Gerät als `0424:3fdd`.
    Der Dienst findet `pending`, schreibt `cc-active`, und Plasma zeigt
    kurz „Class-Compliant-Modus aktiv“. Die T3-Konfiguration
    (`ff802-ucm-profile`) greift unverändert.

Die physische Trennung ist nötig: Laut RME-Handbuch setzt der Befehl die
Option „Activate CC mode on disconnect“. T4 versucht weder einen
USB-Reset noch eine Stromunterbrechung.

## Sicherheitskonzept

| Anforderung | Umsetzung | Test |
|---|---|---|
| Keine Schreibzugriffe aus udev | Regel ohne `RUN`/`PROGRAM`/`IMPORT` | `auto_units` |
| Keine unbekannten Geräte | exakte ID + `RME` + Produkttext + High Speed | `test_auto_sysfs`, `test_auto_switch`, `auto_flow` 7/8 |
| Kein anderes Fireface-Modell | 802 FS über ID und Produkttext ausgeschlossen | dto. |
| Höchstens ein Versuch pro Anschluss | Eintrag vor jedem USB-Zugriff, unter Sperre | `auto_flow` 6/18, 10 gleichzeitige Ereignisse → 1 Transfer |
| Keine Wiederholung | `cc_switch` sendet einmal; Dienst ohne `Restart` | `test_auto_switch`, `auto_units` |
| Identität vor dem Schreiben erneut prüfen | Bestätigungs-Callback prüft sysfs, Port, Modus, Bytes | `test_auto_switch` (Umstecken, Entfernen, CC, „off“ zwischen Erkennung und Write) |
| Keine Rückumschaltung durch die Automatik | kein Codepfad in `ff802-cc-autoswitch`; `ff802-cc disable` ist ein eigener, manueller Befehl | `autoswitch_binary_symbols`, Quelltext |
| Keine Reset-/Detach-/Control-Aufrufe | statische Quelltextprüfung, `nm` der Programme | `check_forbidden_usb_calls`, `*_binary_symbols`, `deb_auto_package` |
| Installation ≠ Aktivierung | Standard „off“, `postinst` ohne `udevadm trigger` oder Start | `deb_auto_package` |
| Keine erweiterten Zugriffsrechte | keine `uaccess`- oder `MODE`-Regel; nur der Systemdienst schreibt | `auto_units`, `deb_auto_package` |
| Keine sudoers-Änderung | nichts unter `/etc` | `deb_auto_package` |
| Simulation kann nicht echt schreiben | Testprogramm ohne libusb (`nm`), nur unter privater Wurzel | `autoswitch_sim_no_libusb`, `auto_flow` |
| Nicht-interaktive Schnittstelle abgesichert | `--service` nur mit `INVOCATION_ID` und als root, in `/usr/libexec` | `auto_flow` |

## Automatik ein- und ausschalten (getrennt von `ff802-cc disable`)

| Befehl | Wer | Wirkung |
|---|---|---|
| `ff802-cc-auto status` | jeder | Zustand, angeschlossenes Fireface mit Bewertung, letzte Aktion; nur lesend |
| `ff802-cc-auto on` | Administrator (pkexec `auth_admin_keep` oder `sudo`) | Automatik an; gilt ab dem nächsten Anschluss oder Systemstart |
| `ff802-cc-auto off` | der aktive lokale Benutzer, **ohne Passwort** | Automatik aus, dauerhaft (Neustart, Umstecken) |

- `off` ändert den USB-Modus des Geräts **nicht**. Es verhindert nur,
  dass Linux umschaltet. Das ist etwas anderes als `ff802-cc disable`,
  das den Modus des Geräts ändern würde und nicht verfügbar ist.
- Zustand: `/var/lib/ff802-cc-auto/mode`. Nur genau `on` gilt als an.
  Fehlt die Datei oder steht etwas anderes darin, ist die Automatik aus.
- `off` wirkt auch während eines laufenden Ereignisses: Der Zustand wird
  direkt vor dem Schreiben noch einmal gelesen.

### Zusammenspiel mit dem proprietären Modus

So bleibt das Fireface für Entwicklung und Hardwaretests unter Linux im
proprietären Modus:

1. `ff802-cc-auto off` (ohne sudo möglich)
2. Gerät zurücksetzen: an Windows/macOS mit TotalMix FX anschließen, oder
   ARC (Prog + B (Mono) beim Einschalten)
3. Unter Linux anschließen: Die Automatik schaltet nicht um, auch nach
   Neustarts nicht.
4. Später wieder: `ff802-cc-auto on` (Administrator)

`ff802-cc disable` (seit 2026-11-19) verweigert, solange die Automatik an
ist. Sonst würde sie das Gerät nach dem Wechsel sofort zurückschalten.

## Benachrichtigungen

- **„Fireface 802 – Class-Compliant-Modus vorbereitet“** (kritisch, bleibt
  stehen): Bitte die USB-Verbindung kurz trennen und wieder anschließen.
- **„Fireface 802 – Class-Compliant-Modus aktiv“**: kurze Bestätigung
  nach dem erneuten Anschließen.
- **„Fireface 802 – automatische Umschaltung nicht erfolgt“** mit dem
  Grund, etwa „transfer failed … device state unknown“.

Regeln:

- Jedes Ereignis erscheint höchstens einmal pro Sitzung.
- „vorbereitet“ und „fehlgeschlagen“ erscheinen nur, solange derselbe
  Anschluss noch besteht.
- Eine Bestätigung, die älter als zwei Minuten ist, entfällt.
- Ist keine Sitzung oder kein Benachrichtigungsdienst vorhanden, wird das
  protokolliert. Es folgt keine Wiederholung und kein Einfluss auf USB.

## Diagnose und Protokollierung

```sh
ff802-cc-auto status                       # Zustand, Gerät, letzte Aktion
journalctl -t ff802-cc-auto -b             # Systemdienst
journalctl --user -t ff802-cc-notify -b    # Benachrichtigungen
systemctl list-units --all 'ff802-cc-auto@*'
cat /run/ff802-cc-auto/last-result /run/ff802-cc-auto/event
```

Jeder Lauf protokolliert den Anschluss, die Bewertung, jeden Schritt von
`cc_switch` und das Ergebnis. Exit-Codes von `ff802-cc-autoswitch
--service`:

| Exit | Bedeutung |
|---|---|
| 0 | aus, schon behandelt, CC-Modus oder gesendet |
| 2 | Bedienfehler, nicht von systemd gestartet |
| 3 | nichts gesendet (abgelehnt oder Fehler vor dem Schreiben) |
| 4 | Schreiben fehlgeschlagen, Gerätezustand unbekannt: Gerät aus- und einschalten, `ff802-cc-auto status` |

## Installation (auch auf einem neu aufgesetzten Kubuntu)

Voraussetzung: T3 (`ff802-ucm-profile`) ist installiert. Die beiden
Pakete sind unabhängig voneinander.

1. Im Dev-Container bauen (keine Werkzeuge auf dem Host nötig):

   ```sh
   cmake -S . -B build && cmake --build build --target deb-auto
   # -> build/deb/ff802-cc-auto_0.1.0_amd64.deb
   ```

2. Auf dem Host installieren. Ein Administratorkonto ist nötig, weil das
   Benutzerkonto kein sudo hat:

   ```sh
   sudo apt install ./ff802-cc-auto_0.1.0_amd64.deb
   ```

   `postinst` lädt nur die udev-Regeln und die systemd-Units neu. Die
   Automatik ist danach **aus**; es wird nichts gesendet.
3. Prüfen: `ff802-cc-auto status`
4. Aktivieren: `ff802-cc-auto on` (polkit fragt nach einem Administrator)

## Vollständiger Rollback

```sh
ff802-cc-auto off              # sofort: keine weitere Umschaltung
sudo apt purge ff802-cc-auto   # entfernt Programm, Regel, Units, Richtlinie, Zustand
```

`postrm` löscht `/var/lib/ff802-cc-auto` und `/run/ff802-cc-auto` und lädt
udev und systemd neu. Benutzer-Units in noch laufenden Sitzungen enden mit
der Abmeldung. `ff802-ucm-profile` und der USB-Modus des Geräts bleiben
unberührt. Zurück in den proprietären Modus geht es über TotalMix FX
oder die ARC.

## Testverfahren (Container)

Alle Tests laufen ohne Fireface und ohne `/dev/bus/usb`:

```sh
cmake -S . -B build && cmake --build build && ctest --test-dir build
```

| Nr. | Szenario | Test |
|---|---|---|
| 1 | bereits im CC-Modus | `test_auto_switch`, `auto_flow` |
| 2 | proprietärer Modus | `test_auto_switch`, `auto_flow` |
| 3 | erfolgreicher Enable-Transfer | `test_auto_switch`, `auto_flow` (genaue Aufruffolge) |
| 4 | fehlgeschlagener Transfer | `test_auto_switch` (Timeout, Stall, I/O, unvollständig), `auto_flow` |
| 5 | Gerät während der Ausführung entfernt | vor dem Lauf, zwischen Erkennung und Write, während des Writes |
| 6 | mehrfache Ereignisse | `test_auto_switch`, `auto_flow` |
| 7 | unbekanntes Gerät | `test_auto_switch`, `auto_flow` |
| 8 | nicht eindeutig identifizierbar | falsche Texte, falsche Geschwindigkeit, veraltetes Ereignis, anderer Port |
| 9 | keine KDE-Sitzung | `auto_notify` (kein `XDG_RUNTIME_DIR`, kein Benachrichtigungsdienst, Anmeldung später) |
| 10 | Wiederanschluss im CC-Modus | `test_auto_switch`, `auto_flow` |
| 11 | Automatik deaktiviert | `test_auto_switch`, `auto_flow`, `auto_control` |
| 12 | nach Neustart weiter deaktiviert | dto. (`/run` geleert) |
| 13 | manuelles Disable bei aktiver Automatik | `test_disable_check`: verweigert, verweist auf `ff802-cc-auto off` |
| 14 | kein Re-Enable nach bewusster Rückkehr | `auto_control` (off, Umstecken, Neustart) |
| 15 | manuelles Enable weiterhin funktionsfähig | `test_cc_switch`, `cc_cli_*` (unverändert) |
| 16 | Abbruch der interaktiven Bestätigung | `test_cc_switch` (`test_enable_not_confirmed`), `test_usb_backend` |
| 17 | fehlgeschlagener Disable-Transfer | `test_cc_disable` (Timeout, Stall, I/O, unvollständig, Gerät weg) |
| 18 | gleichzeitige Ereignisse | `test_auto_sysfs` (8 Prozesse), `auto_flow` (10 Ereignisse, veraltetes und aktuelles) |
| 19 | keine Schreibzugriffe beim Paketbau | `deb_auto_package` (Stolperdrähte, statische Prüfung) |
| 20 | Deaktivierung und Rollback | `deb_auto_package`, `auto_control` |

## Bekannte Einschränkungen und Risiken

- **Proprietäre sysfs-Texte unbestätigt.** Hersteller und Produkt sind
  im CC-Modus bekannt (`RME`, `Fireface 802 (24047128)`). Im proprietären
  Modus müssen sie vor `on` mit `ff802-cc-auto status` geprüft werden.
  Weichen sie ab, verweigert die Automatik sicher und meldet den Grund.
- **Pfadeinheiten** (`PathChanged`), polkit und `pkexec` konnten im
  Container nicht real laufen. Geprüft sind die Units mit
  `systemd-analyze verify`, die Logik mit Test-Doubles.
- **`pkexec`** ist unter Kubuntu nicht garantiert installiert
  (`Recommends`). Ohne pkexec braucht `on`/`off` einen Administrator
  mit sudo.
- **Tiefe Hub-Kette** (`3-2.4.1.1.3`): Ein Timeout endet mit
  „Zustand unbekannt“ und ohne Wiederholung.
- **Neustart ohne Umstecken:** Nach einem Neustart ist es ein neuer
  Anschluss. Ist das Gerät noch proprietär, sendet die Automatik einmal
  erneut. Das ist harmlos, denn die Option ist bereits gesetzt.
- **Ruhezustand:** Laut Handbuch wechselt das Gerät dabei nicht in den
  CC-Modus, weil keine Trennung stattfindet.
- **`ff802-cc disable`** ist seit 2026-11-19 umgesetzt, siehe
  [cc_disable_port_reset.md](cc_disable_port_reset.md). Am Gerät ist es in der
  integrierten Form noch nicht getestet.

## Host-Testplan

Die automatische Aktivierung wurde auf dem Host erfolgreich getestet.
Welche der folgenden Schritte dabei im Einzelnen ausgeführt wurden, ist
nicht dokumentiert.

1. **Nur lesend**, Fireface im proprietären Modus (nach Windows):
   `lsusb -d 2a39:3fcd` und
   `cat /sys/bus/usb/devices/<port>/{manufacturer,product,speed}`. Erwartet
   werden `RME`, `Fireface 802 (24047128)` und `480`.
2. Paket installieren (Administrator), dann `ff802-cc-auto status`:
   Die Automatik ist aus, das Gerät wird als „proprietary mode“ bewertet.
3. `ff802-cc-auto on`, danach einmal USB trennen und wieder anschließen.
   Erwartet: genau ein Transfer laut `journalctl -t ff802-cc-auto -b` und
   die Plasma-Meldung „vorbereitet“.
4. USB trennen und wieder anschließen. Erwartet: `0424:3fdd`, die
   Meldung „aktiv“, T3 `verify` besteht.
5. Weitere Ereignisse auf demselben Anschluss (`udevadm trigger` durch
   einen Administrator): kein weiterer Transfer.
6. Systemstart mit proprietärem Gerät, ohne Anmeldung: Der Transfer
   erfolgt beim Start, die Meldung erscheint nach der Anmeldung.
7. `ff802-cc-auto off` als Benutzer ohne Passwort, Gerät über Windows
   zurücksetzen, unter Linux anschließen: kein Transfer.
8. Rollback: `sudo apt purge ff802-cc-auto`, danach `ff802-cc-auto`
   nicht mehr vorhanden und `/var/lib/ff802-cc-auto` entfernt.
