# T4: Automatischer Port-Reset nach dem CC-Befehl

> **Stand 2026-11-23: am Gerät erfolgreich getestet (vom Anwender
> gemeldet: Umschaltung, Port-Reset, Wiedererkennung, Initialisierung).**
> Paket `ff802-cc-auto` 0.2.0~dev4 (unveröffentlicht). Betrifft nur das ursprüngliche RME
> Fireface 802 (nicht das 802 FS).
>
> **Nachtrag:** Ab 0.2.0~dev5 umfasst die Mixer-Initialisierung 38 Register
> (`ff802-mixer-init-v3`, [mixer_init.md](mixer_init.md)); Angaben zu 26
> Registern unten beziehen sich auf dev4.

## Ziel

Bisher sendete die Automatik den CC-Befehl `01 00 44 BD` und bat dann
darum, das USB-Kabel kurz zu trennen. `ff802-cc enable` setzt dagegen
nach dem Befehl den USB-Hub-Port des Geräts zurück (Hardwaretest
2026-11-19, [cc_disable_port_reset.md](cc_disable_port_reset.md)). Die
Automatik verwendet jetzt denselben Reset:

```
Gerät erkennen → CC-Befehl → Port-Reset (ff802_port_switch)
  → Warten auf 0424:3fdd am selben Port → neues udev-Ereignis
  → Bestätigung „cc-active“ → optional Mixer-Initialisierung (26 Register)
```

## Derselbe Reset

- Eine Implementierung: [src/usb_port.c](../src/usb_port.c),
  `ff802_port_switch()`. Nur diese Datei schreibt das Attribut `disable`
  (statische Prüfung `check_forbidden_usb_calls`).
- Dieselbe Zeitfolge: `FF802_PORT_OFF_MS` (2000) und `FF802_PORT_WAIT_MS`
  (15000) stehen jetzt in [usb_port.h](../include/ff802/usb_port.h) und
  werden von `ff802-cc enable` und von der Automatik verwendet
  (vorher als lokale Konstanten in `ff802_cc.c`, Werte unverändert).
- Dieselben Vorprüfungen wie `ff802-cc enable`: Port aufgelöst, sein
  `device`-Link zeigt genau auf dieses Gerät, `disable` schreibbar. Sie
  laufen **vor** dem Senden; ist der Reset nicht möglich, wird der Befehl
  trotzdem gesendet und wie bisher um Umstecken gebeten.
- `ff802_port_switch()` prüft unmittelbar vor dem Reset erneut, dass am
  Port noch genau die beschriebene Verbindung steckt (Kernelname, Bus,
  devnum, USB-ID); sonst kein Reset.

## Units und Rechte

| | `ff802-cc-auto-enable@<k>.service` (neu) | `ff802-cc-auto@<k>:<bus>:<devnum>.service` |
|---|---|---|
| udev | `add` von `2a39:3fcd` | `add` von `0424:3fdd` |
| Aufgabe | CC-Befehl, Port-Reset | Bestätigung, Mixer-Initialisierung |
| Geräteknoten | nur `char-usb_device` | nur `char-alsa` (vorher zusätzlich USB) |
| Schreibbar in `/sys` | genau `/sys/bus/usb/devices/%i/port/disable` | nichts |
| `ProtectKernelTunables` | `yes` | `yes` |
| Capabilities | keine | keine |
| `TimeoutStartSec` | 60 (vorher 30) | 60 (vorher 30) |

Alle übrigen Härtungen sind in beiden Units identisch (geprüft in
`auto_units`); `systemd-analyze security` ergibt für beide 1,2.

**Warum eine eigene Unit:** `ProtectKernelTunables=yes` macht `/sys`
schreibgeschützt. Eine Lockerung sollte genau eine Datei betreffen. Der
Pfad des Ports hängt vom Gerät ab; systemd kann ihn nur aus dem
Instanznamen bilden. Mit dem Kernelnamen als Instanz ergibt
`ReadWritePaths=-/sys/bus/usb/devices/%i/port/disable` über den
`port`-Link des Geräts genau die `disable`-Datei seines Hub-Ports.
Das Präfix `-`: fehlt der Pfad beim Start, startet die Unit trotzdem;
`/sys` bleibt dann ganz schreibgeschützt und der Reset wird übersprungen
(Vorprüfung „nicht schreibbar“).

**Warum die CC-Unit ihren Instanznamen behält:** Das Gerät kommt nach dem
Reset am selben Port mit neuer devnum zurück. Hätte die CC-Unit nur den
Kernelnamen als Instanz, würde systemd das neue Ereignis mit der noch
laufenden Instanz zusammenlegen und die Mixer-Initialisierung fiele aus.
Mit `<k>:<bus>:<devnum>` ist es immer eine neue Instanz; die getrennten
Unit-Namen verhindern dasselbe für die Enable-Unit.

## Erkennung der Re-Enumeration

`ff802_port_switch()` liest nach `disable=0` alle 100 ms
`/sys/bus/usb/devices/<k>` bis zu 15 s:

| Ergebnis | Bedeutung | Ergebnis der Automatik | Exit |
|---|---|---|---|
| USB-ID `0424:3fdd` am Port | zurück im CC-Modus (neue devnum) | `switched` | 0 |
| nichts lesbar nach 15 s | nicht wieder enumeriert | `not-reenumerated` | 5 |
| andere USB-ID am Ende | zurück, aber nicht CC | `reenumerated-not-cc` | 5 |
| `disable=1` fehlgeschlagen, Port fremd, Gerät geändert | kein Reset | `reset-failed` | 5 |
| `disable=0` fehlgeschlagen | Port evtl. abgeschaltet | `reset-failed` | 5 |

Eine erfolgreiche Reset-Anforderung allein gilt nicht als Erfolg.
`switched` heißt: die USB-ID am Port ist die des CC-Modus. Die
vollständige Identität (Hersteller, Produkt, Geschwindigkeit) prüft
danach die CC-Unit des neuen Ereignisses, bevor sie „cc-active“ meldet
und den Mixer initialisiert. Die Initialisierung wartet zusätzlich bis
15 s auf Soundkarte und MIDI-Port „Port 2“ dieser Verbindung.

## Nebenläufigkeit

- Alle Läufe beider Units sind über `flock` auf `/run/ff802-cc-auto/lock`
  serialisiert. Die Enable-Unit hält die Sperre während Befehl, Reset und
  Warten (höchstens etwa 2 s + 2 s + 15 s).
- Das udev-Ereignis des zurückgekehrten Geräts startet die CC-Unit; sie
  wartet auf die Sperre. Die Enable-Unit wartet nie auf die CC-Unit,
  daher keine Verklemmung (Test P8). 60 s Zeitlimit decken Wartezeit auf
  die Sperre und 15 s MIDI-Wartezeit ab.
- Mehrere `add`-Ereignisse für dieselbe proprietäre Verbindung: während
  des Laufs legt systemd sie zusammen; danach verhindert der
  Verbindungseintrag in `attempts/` einen zweiten Versuch.
- Gerät verschwindet während des Resets: das ist der erwartete Ablauf;
  der Port bleibt Teil des Hubs, `disable=0` wird immer versucht (Signale
  während des Zyklus blockiert).

## Keine Wiederholungsschleife

Endet ein Reset nicht im CC-Modus, schreibt die Automatik
`/run/ff802-cc-auto/reset-blocked`. Solange die Datei existiert, gibt es
keinen automatischen Reset mehr; eine neue Verbindung erhält den Befehl
weiterhin einmal und die Bitte um Umstecken. Ein Gerät, das proprietär
zurückkommt, löst so höchstens noch einen Befehl ohne Reset aus (Test P3).
Die Sperre endet mit dem Neustart (`/run`) oder mit `ff802-cc-auto on`.

## Journal (`journalctl -t ff802-cc-auto`)

| Zustand | Zeile |
|---|---|
| Reset geplant / nicht möglich | `Port reset planned after the command: …/disable.` / `No automatic port reset: <Grund>. …` |
| CC-Befehl gesendet | `CC command sent: 01 00 44 BD to <k> (bus B, devnum D), 4 bytes.` |
| Reset ausgelöst | `Port reset triggered: …/disable = 1, 2000 ms, = 0; waiting up to 15000 ms for 0424:3fdd at <k>.` |
| Reset fehlgeschlagen | `Port reset failed: …` (bei `disable=0` mit dem Befehl zum Wiedereinschalten) |
| nicht wieder enumeriert | `Port reset done, device not re-enumerated at <k> within 15000 ms.` |
| enumeriert, nicht CC | `Port reset done, re-enumerated at <k> as VVVV:PPPP devnum N: not class-compliant mode.` |
| CC bestätigt | `Re-enumerated in class-compliant mode: <k> 0424:3fdd bus B devnum N (was devnum D). …` |
| Mixer-Konfiguration | `mixer: configuration ff802-mixer-init-v2, 26 entries, checksum 0xC97A359C (verified).` |
| Mixer ok / fehlgeschlagen | `mixer: result done (26 of 26 registers): …` / `mixer: result <outcome> (n of 26 registers): …` |
| Gesamtergebnis | `Result: <Ergebnis>: <Detail>` (switched, reset-failed, not-reenumerated, reenumerated-not-cc, sent, …) |

## Meldungen

- Erfolg: keine Aufforderung zum Umstecken; nach der Rückkehr die
  bestehende Bestätigung „Fireface 802 – Class-Compliant-Modus aktiv“.
- Reset nicht möglich: „Class-Compliant-Modus vorbereitet – Der
  automatische USB-Neustart war nicht möglich. Bitte die USB-Verbindung …
  kurz trennen und wieder anschließen.“
- Reset ohne CC-Ergebnis: „automatische Umschaltung nicht bestätigt“ mit
  Grund und Hinweis zum Umstecken (nicht angezeigt, wenn das Gerät
  inzwischen im CC-Modus ist).
- `ff802-cc-auto on`: Hinweis, dass ein angeschlossenes Gerät einmal
  umgesteckt (oder neu gestartet) werden muss, danach kein weiteres
  Umstecken.

## Mixer-Protokoll

`FF802_MIXER_INIT_ID` = `ff802-mixer-init-v2` (v1: 24 Einträge, v2: 26).
Journal und `/run/ff802-cc-auto/mixer-result` enthalten Konfiguration,
Anzahl (26) und die Prüfsumme der einkompilierten Tabelle
(`config=`, `entries=`, `checksum=0xC97A359C`); `ff802-mixer-init --list`
zeigt dieselbe Zeile. Die 26 Register sind unverändert (einschließlich
`0x4220 = 0x0000` und `0x4261 = 0x0000`).

## Automatisch getestet (ohne Hardware)

- `auto_flow`: P1–P8 mit simuliertem Port (Fixture-sysfs, simulierte
  Re-Enumeration, keine echten Wartezeiten): Erfolg mit Folgeereignis und
  26 Registern, nicht zurück, proprietär zurück und keine Schleife,
  Aufheben der Sperre, `disable=0` scheitert, Port nicht schreibbar,
  fremder Port, Automatik aus, CC-Ereignis während des Resets.
- `auto_units`: beide Units, Rechte, identische Härtung,
  `systemd-analyze verify` und `security`.
- `auto_notify`, `auto_switch`, `mixer_init`, `mixer_init_tool`.
- `deb_auto_package`: Version und Reset-Texte des paketierten Programms,
  eine Reset-Implementierung mit derselben Zeitfolge, Journaltexte,
  Rechte der paketierten Units, die 26 Register (aus den Formeln) mit
  Reihenfolge und Prüfsumme als Bytes in Dienst und `ff802-mixer-init`.

## Noch nicht am Gerät oder Host geprüft

- Ob systemd 259 auf Kubuntu 26.04 `ReadWritePaths=` über die Symlinks
  `/sys/bus/usb/devices/<k>` und `port` auflöst und die eine Datei
  trotz `ProtectKernelTunables=yes` schreibbar macht. Wenn nicht: der
  Reset wird übersprungen („not writable“), der Befehl gesendet, um
  Umstecken gebeten (das bisherige Verhalten).
- Der Reset selbst durch die Automatik, die Rückkehr als `0424:3fdd`
  innerhalb von 15 s, die Reihenfolge der Ereignisse, die Benachrichtigung
  und die Mixer-Initialisierung nach dem automatischen Reset.
- Verhalten an Hubs, an denen `disable` fehlt oder nicht wirkt.
