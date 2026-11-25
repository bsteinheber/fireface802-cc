# Integrationstest: Mixer-Initialisierung v3 (38 Register)

> **Stand 2026-11-23: bestanden mit `ff802-cc-auto 0.2.0~dev6`** (auf dem
> Kubuntu-Host installiert und aktiv). dev5 hatte einen Fehler
> (Playback 9–12 → Phones 9–12 nach dem Einschalten ohne Wirkung), der in
> dev6 mit einem zweiten Durchgang behoben ist.
> Der erste Release-Kandidat `ff802-cc-auto` 0.2.0 (nicht veröffentlicht) hatte denselben
> Funktionsstand wie dev6; das Release-Paket 0.2.1 (v0.5.0) erweitert die Liste auf
> 168 Register (v4, [mixer_v4.md](mixer_v4.md)), die ersten 38 unverändert.

## Ergebnis dev6 (2026-11-23, vom Anwender auf dem Host)

Installation mit `dpkg -i` nach erfolgreicher Prüfsummenkontrolle
(`ff802-v3-check.sh pre`), dann Aus- und Einschalten des Fireface.
`tools/hw-test/ff802-v3-check.sh post "10 min ago"`:

```
info installed: 0.2.0~dev6; journal lines: 100
ok   switch: CC command sent once
ok   switch: port reset triggered (disable = 1, 2000 ms, = 0)
ok   switch: re-enumerated in class-compliant mode (0424:3fdd)
ok   switch: result switched, no failed reset
ok   mixer: configuration v3 verified (38 entries, 0xEC858A44)
ok   mixer: entries 1..26 are v2 (0xC97A359C)
ok   mixer: 38 registers set in the last run, the last one Input 9 -> Phones 12
ok   mixer: the twelve Input 9 registers at 0x2666
ok   mixer: pass 2 sent: the same 38 values again (Playback -> Phones included)
ok   mixer: result done (38 of 38), no FAILED line
ok   mixer-result: done, 38/38, v3 0xEC858A44, base v2 0xC97A359C
ok   unit now in class-compliant mode at 3-2.4.1.1.3 (0424:3fdd)
ok   no mixer-failed notification pending
all checks passed
```

Hörprüfung (Playback auf Analog 1/2 … 7/8 und Phones 9/10, 11/12, Input 9
auf allen zwölf Ausgängen): vom Anwender als **erfolgreich** gemeldet.
Nach einem zweiten Aus- und Einschalten wiederholt: ebenfalls
**erfolgreich** (vom Anwender gemeldet). Der Fehler von dev5 trat damit in
zwei Läufen von dev6 nicht auf.

## Ergebnis dev5 (2026-11-23, vom Anwender auf dem Host)

| Prüfung | Ergebnis |
|---|---|
| Installation dev5, CC-Umschaltung, Port-Reset | ok (Journal: `Result: switched`, 19:32, 20:36, 20:42) |
| Initialisierung | alle 38 Nachrichten ohne Fehler an ALSA (`mixer: result done (38 of 38 registers)`) |
| Input 9 auf Phones 9–12 | hörbar |
| Playback (Musik) auf Phones 9/10, 11/12 | **nicht hörbar**, PipeWire-Pegel schlagen aus; nach erneutem Aus-/Einschalten **wiederholbar** |
| Test 1: Musik auf Analog 1/2 nach dem Einschalten | nicht auf Phones 9/10 (Kreuzverbindung bleibt aus) |
| `ff802-mixer-init --apply` (dieselben 38 Werte) danach | Musik auf Phones 9/10 wieder hörbar |
| Test 2: Einschalten, vor jeder Wiedergabe `--apply`, dann Musik | hörbar |
| Gegenprobe A: `sudo alsactl restore 1` im funktionierenden Zustand | weiterhin hörbar |
| Gegenprobe B: `systemctl --user restart wireplumber` | weiterhin hörbar |

Bewertung: Die Werte sind richtig (der Handlauf wirkt), nichts setzt sie
im Betrieb zurück (A, B, Test 1). Ein Teil eines einzelnen Durchgangs
unmittelbar nach der Wiedererkennung blieb ohne Wirkung. In beiden Läufen
mit Zeitstempeln (20:36, 20:42) fiel eine WirePlumber-Meldung zum neuen
Gerät 10–20 ms vor die vier Phones-Nachrichten (Nr. 11–14). Der
Mechanismus ist **nicht belegt** (kein Rücklesen möglich).

Korrektur in dev6: Der Dienst sendet die unveränderten 38 Werte ein
zweites Mal, 5 s nach dem ersten vollständigen Durchgang, über dieselbe
Verbindung, nach erneuter Prüfung des Geräts. Der Handlauf bleibt bei
einem Durchgang.

## Pakete

| | Paket | SHA-256 |
|---|---|---|
| neu (dev6) | `build-deb-dev6/ff802-cc-auto_0.2.0~dev6_amd64.deb` | `0ad3512495b54cef07f952f79b47be3104bed1787ee09eedbd4093e2a7a791c5` |
| Referenz (installiert) | `build-deb-dev4/ff802-cc-auto_0.2.0~dev4_amd64.deb` | `f983b5df0aed25a5376c5a4e50dc9bcf0e51f5ff5e619d245c87cd39306574fb` |

Programme im neuen Paket: `ff802-cc-autoswitch`
`abe0d27360cfc689d3000109c6812f24529f69eab7a091e846d3bfd3f2a27c6a`,
`ff802-mixer-init`
`dc1f864281beb4e17537ae436ce5700085dfb977c82acb96c381ceab48458f7e`.

Unterschied zu dev4: nur die Mixerliste (38 statt 26 Einträge, zwei
Durchgänge im Dienst,
Journal- und `mixer-result`-Angaben, Texte). Units, udev-Regel, Rechte,
CC-Umschaltung und Port-Reset sind unverändert.

## Sicherheit vor dem Test

- Input 9 liegt danach mit −10,5 dB auf **allen zwölf** Ausgängen, auch
  auf den Lautsprechern an Analog 1–8: an Input 9 kein Mikrofon vor
  Lautsprechern (Rückkopplung), Quelle zunächst leise oder ausgesteckt.
- Phones 9–12 haben weiterhin Ausgangspegel 0 dB: Kopfhörer ab,
  Verstärker leise, bevor das Gerät eingeschaltet wird.
- Die Eingangsverstärkung von Input 9 (Regler am Gerät) bleibt wie sie ist.

## Ablauf

0. Paket prüfen und installieren (nach Freigabe):
   ```sh
   cd <Projektverzeichnis>    # der lokale Klon von fireface802-cc
   tools/hw-test/ff802-v3-check.sh pre    # beide Pakete, SHA-256, Zustand (nur lesend)
   sudo dpkg -i build-deb-dev6/ff802-cc-auto_0.2.0~dev6_amd64.deb   # nur dieses Paket
   ff802-cc-auto status                 # Automatik: on (bleibt erhalten)
   ff802-cc-auto mixer-init status      # mixer init: on (bleibt erhalten)
   ff802-mixer-init --list | head -3    # ff802-mixer-init-v3, 38 entries, 0xEC858A44
   ```
1. Fireface ausschalten (Gerät soll beim Einschalten proprietär starten,
   z. B. nach Windows; startet es im CC-Modus, entfällt Schritt 3).
2. Fireface einschalten.
3. Automatische CC-Umschaltung mit Port-Reset abwarten (einige Sekunden):
   ```sh
   journalctl -t ff802-cc-auto -b --since "-5 min"
   ```
   Erwartet: `CC command sent`, `Port reset triggered`,
   `Re-enumerated in class-compliant mode`, `Result: switched`.
4. Initialisierung der 38 Register prüfen:
   ```sh
   tools/hw-test/ff802-v3-check.sh post "15 min ago"   # alle Journal- und Ergebnisprüfungen (nur lesend)
   journalctl -t ff802-cc-auto -b --since "-5 min" | grep '^.*mixer:'
   cat /run/ff802-cc-auto/mixer-result
   ```
   Erwartet:
   - `mixer: configuration ff802-mixer-init-v3, 38 entries, checksum 0xEC858A44 (verified).`
   - `mixer: entries 1..26 are ff802-mixer-init-v2, checksum 0xC97A359C (verified).`
   - 38 Zeilen `mixer: set …`, die letzte
     `mixer: set 0x42C8 = 0x2666 (Input 9 -> Phones 12, 30 %) [38/38]`
   - `mixer: pass 1 of 2 complete; pass 2 (the same 38 values) in 5000 ms.`,
     danach 38 Zeilen `mixer: pass 2: set …`
   - `mixer: result done (38 of 38 registers): …, twice`
   - `mixer-result`: `outcome=done`, `written=38/38`,
     `config=ff802-mixer-init-v3`, `checksum=0xEC858A44`,
     `base=ff802-mixer-init-v2`, `base_checksum=0xC97A359C`
5. Playback-Ausgänge wie bisher (je Kanal einzeln, leise):
   ```sh
   wpctl status                                   # Senken des Fireface
   PIPEWIRE_NODE=<node.name> speaker-test -D pipewire -c 2 -s 1 -t sine -l 1   # nur links
   PIPEWIRE_NODE=<node.name> speaker-test -D pipewire -c 2 -s 2 -t sine -l 1   # nur rechts
   ```
   für Analog 1/2, 3/4, 5/6, 7/8, Phones 9/10, 11/12. Erwartet wie bei
   v2: nur der gewählte Kanal; Analog 1/2 nicht auf Phones 9/10.
6. Input 9: gleichmäßige Quelle an Input 9, dann an jedem Ausgang
   (Analog 1–8, Phones 9–12) prüfen: hörbar, links und rechts eines Paars
   gleich (Monosignal), deutlich unter Vollaussteuerung, kein Unterschied
   zwischen den Ausgängen. Bei laufender Wiedergabe (Schritt 5) liegen
   Playback und Input 9 gemischt auf den Ausgängen.

Ergebnis bitte mit Journalauszug und `mixer-result` melden.

## Rückweg

- Nur die Initialisierung abschalten (Matrixwerte bleiben, bis zur
  nächsten Änderung): `ff802-cc-auto mixer-init off`.
- Zurück zur Konfiguration v2 (Referenzpaket dev4, unverändert in
  `build-deb-dev4/`):
  ```sh
  sudo dpkg -i build-deb-dev4/ff802-cc-auto_0.2.0~dev4_amd64.deb
  ```
  Die v2-Initialisierung schreibt die zwölf Input-9-Register **nicht**;
  sie blieben auf `0x2666`. Ein Programm, das alle zwölf auf `0x0000`
  setzt, gibt es noch nicht (nur `ff802-mixer-e1 --restore` für `0x4088`).
  Vor einem Downgrade müsste es erstellt und getestet werden.

## Automatisch geprüft (ohne Hardware)

Gesamtsuite im Build `build-e1-test`: 85/85, darunter

- `test_mixer_init` (3170 Prüfungen): 38 Einträge, Adressen, Werte,
  Reihenfolge, Arten, v2 unverändert, beide Prüfsummen aus Literalen
  nachgerechnet, Input-9-Formel und Monopaare, nichts anderes in der Liste;
- `test_mixer_run` (286): 38 Nachrichten in Reihenfolge, Abbruch bei
  Nachricht 7, 27 (v2-Teil vollständig) und 38, keine Wiederholung;
- `mixer_init_tool`, `auto_flow` (84), `auto_switch`, `auto_control`,
  `auto_notify`: Dienst und Handlauf mit 38 Nachrichten byte-genau,
  Journal, `mixer-result`, gleichzeitige Ereignisse, Port-Reset-Folge;
- `deb_auto_package` (38): Tabelle aus den Formeln als Bytes in Dienst
  und `ff802-mixer-init`, beide Prüfsummen in der Shell nachgerechnet,
  reproduzierbarer Bau, Installation/Upgrade/Entfernung in isolierter
  Wurzel, kein Hardwaretestprogramm im Paket.

Nicht geprüft: die Wirkung der Input-9-Register außer `0x4088` am Gerät
und die Folge von 38 Nachrichten am Gerät.
