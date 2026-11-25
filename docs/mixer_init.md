# Mixer-Initialisierung im Class-Compliant-Modus

> **Stand 2026-11-25: Konfiguration `ff802-mixer-init-v4` (168 Register,
> Prüfsumme `0xC43D2879`), Entwicklungspaket `ff802-cc-auto 0.2.1~dev1`,
> am Gerät noch nicht geprüft ([mixer_v4.md](mixer_v4.md)).** Darin
> unverändert enthalten: `ff802-mixer-init-v3` (Einträge 1–38,
> `0xEC858A44`, am Gerät getestet mit 0.2.0~dev6/0.2.0) und
> `ff802-mixer-init-v2` (1–26, `0xC97A359C`).
> Betrifft nur das **ursprüngliche RME Fireface 802** (nicht das 802 FS)
> im USB-Class-Compliant-Modus (`0424:3fdd`). Nach der Installation ist die
> automatische Initialisierung **aus**.

## Ziel und Funktionsweise

Im CC-Modus entscheidet der interne DSP-Mixer des Fireface, welcher
USB-Wiedergabekanal an welcher Buchse ankommt
([mixer_routing.md](mixer_routing.md)). Die Mixer-Initialisierung stellt
eine definierte Grundkonfiguration her, ohne OSCMix, TotalMix FX oder
Windows:

- die beiden Kreuzverbindungen Playback 1 → Phones 9 und Playback 2 →
  Phones 10 **aus**: Der CC-Modus legt Playback 1/2 zusätzlich auf die
  Kopfhörer, so dass „Analog Output 1/2“ auch an Phones 9/10 zu hören war
  (Hardwaretests D2/D3),
- Playback 1…12 → Hardware Output 1…12 auf 0 dB,
- Ausgangspegel der Hardware Outputs 1…12 auf 0 dB
  (1–8 = Analog 1–8, 9–12 = Phones 9–12),
- Input 9 (Mic/Inst 9) → Hardware Output 1…12 mit 30 % linearer Amplitude
  (`0x2666`, etwa −10,5 dB), auf beiden Kanälen jedes Ausgangspaars dasselbe
  Monosignal (ab v3; Pegel am Gerät bestätigt im Hardwaretest E1,
  [mixer_e1.md](mixer_e1.md)),
- alle übrigen Verbindungen Playback p → Hardware Output o innerhalb
  von 1…12 (p ≠ o) **aus** (ab v4): Die 12×12-Matrix ist damit vollständig
  festgelegt, unabhängig von früheren TotalMix-Einstellungen (Anlass:
  Playback 3/4 war zusätzlich auf Phones 11/12 zu hören,
  [mixer_v4.md](mixer_v4.md)).

Das sind **genau 168 Registerzugriffe** (v4; v3: 38, v2: 26, v1: 24, ohne die
beiden Kreuzverbindungen von v2). Gesendet werden sie als RME-SysEx über den
ALSA-Sequencer an den MIDI-Port „Port 2“ des Geräts.

**Nicht verändert** werden Playback 13…30 (AES, ADAT) auf allen Ausgängen,
jede Quelle auf den Ausgängen 13…30, alle Eingangs-Routings außer
Input 9, die Eingangsverstärkung, Pan, Mute, Control Room,
Eingangsempfindlichkeiten, Clock und sonstige Geräteeinstellungen.
Innerhalb der 12×12-Playback-Matrix bleibt dagegen nichts übrig: Jede
Verbindung wird gesetzt (ab v4). Es wird nichts aus dem Gerät gelesen;
es gibt kein Rückgängig.

## Was am Gerät getestet und was nur simuliert ist

| Funktion | Nachweis |
|---|---|
| Registeradressen und 0-dB-Werte (Tabelle unten) | am Gerät mit OSCMix bestätigt (zehn Register geschrieben, `0x2900` beobachtet); Playback 3–8 und Ausgangspegel 2–8 aus der bestätigten Formel |
| Erkennung von Gerät, Soundkarte und Port | **am Gerät** (B.1–B.4, D1–D3, `ff802-mixer-init`) |
| Sendeweg ALSA-Sequencer → `snd-seq-midi` → Gerät | **am Gerät** mit einzelnen Nachrichten (B.2, B.3, B.4, D1, D3), zwei Nachrichten (B.3-Stereo) und der **bisherigen Folge von 24 Nachrichten** (`ff802-mixer-init --apply`, 2026-11-22: Analog 1/2, Phones 9/10 und 11/12 danach stereo) |
| Wirkung eines einzelnen Routings ohne Stereokopplung | **am Gerät** (B.3: Playback 9 nur links, Playback 10 stumm) |
| `0x0000` schaltet eine Matrixverbindung ab | **am Gerät**: D1 `0x4269` (aus und mit `0x9000` wieder an), D2 `0x4261`, D3 `0x4220` |
| Die beiden neuen Einträge `0x4220`/`0x4261` = `0x0000` | **am Gerät** einzeln (D3, D2) mit denselben Bytes wie in der Liste |
| **Folge von 26 Nachrichten** (v2) | **am Gerät** (0.2.0~dev4, automatisch nach CC-Umschaltung und Port-Reset) |
| `0x4088 = 0x2666` (Input 9 → Analog 3, 30 %) | **am Gerät** (E1, 2026-11-23: deutlich hörbar, Analog 4 still, mit `0x0000` wieder still) |
| Input 9 → alle zwölf Ausgänge | **am Gerät** (Integrationstest v3, dev6: auf allen zwölf hörbar, Hörprüfung) |
| **Folge von 38 Nachrichten** (v3) | **am Gerät**: dev5 mit einem Durchgang unvollständig wirksam (Playback → Phones), dev6 mit zwei Durchgängen bestanden ([mixer_v3_integration.md](mixer_v3_integration.md)) |
| **Folge von 168 Nachrichten** (v4, vollständige 12×12-Matrix) | **nur simuliert**; Hardwaretest steht aus ([mixer_v4.md](mixer_v4.md)) |
| Lauf im systemd-Dienst (Härtung, `DeviceAllow=char-alsa`, Boot) | **nur simuliert** (Dienstaufruf mit Simulationsbuild, `systemd-analyze`) |
| Wartezeiten, Sperre, gleichzeitige Ereignisse, Fehlerfälle, Benachrichtigung | **nur simuliert** |
| Paketbau, Installation, Upgrade, Entfernung | in isolierter Wurzel getestet, nicht auf dem Host |

## Registerliste

| Nr. | Register | Wert | Funktion | SysEx |
|---|---|---|---|---|
| 1 | `0x4220` | `0x0000` | Playback 1 → Phones 9, aus | `F0 00 20 0D 10 00 00 00 00 11 04 F7` |
| 2 | `0x4261` | `0x0000` | Playback 2 → Phones 10, aus | `F0 00 20 0D 10 00 00 00 04 13 04 F7` |
| 3 | `0x4020` | `0x9000` | Playback 1 → Analog 1, 0 dB | `F0 00 20 0D 10 00 00 20 02 01 0C F7` |
| 4 | `0x4061` | `0x9000` | Playback 2 → Analog 2, 0 dB | `F0 00 20 0D 10 00 00 20 06 03 0C F7` |
| 5 | `0x40A2` | `0x9000` | Playback 3 → Analog 3, 0 dB | `F0 00 20 0D 10 00 00 20 0A 05 0C F7` |
| 6 | `0x40E3` | `0x9000` | Playback 4 → Analog 4, 0 dB | `F0 00 20 0D 10 00 00 20 0E 07 0C F7` |
| 7 | `0x4124` | `0x9000` | Playback 5 → Analog 5, 0 dB | `F0 00 20 0D 10 00 00 20 12 09 0C F7` |
| 8 | `0x4165` | `0x9000` | Playback 6 → Analog 6, 0 dB | `F0 00 20 0D 10 00 00 20 16 0B 0C F7` |
| 9 | `0x41A6` | `0x9000` | Playback 7 → Analog 7, 0 dB | `F0 00 20 0D 10 00 00 20 1A 0D 0C F7` |
| 10 | `0x41E7` | `0x9000` | Playback 8 → Analog 8, 0 dB | `F0 00 20 0D 10 00 00 20 1E 0F 0C F7` |
| 11 | `0x4228` | `0x9000` | Playback 9 → Phones 9, 0 dB | `F0 00 20 0D 10 00 00 20 22 11 0C F7` |
| 12 | `0x4269` | `0x9000` | Playback 10 → Phones 10, 0 dB | `F0 00 20 0D 10 00 00 20 26 13 0C F7` |
| 13 | `0x42AA` | `0x9000` | Playback 11 → Phones 11, 0 dB | `F0 00 20 0D 10 00 00 20 2A 15 0C F7` |
| 14 | `0x42EB` | `0x9000` | Playback 12 → Phones 12, 0 dB | `F0 00 20 0D 10 00 00 20 2E 17 0C F7` |
| 15 | `0x1E00` | `0x0000` | Pegel Analog 1, 0 dB | `F0 00 20 0D 10 00 00 00 00 70 09 F7` |
| 16 | `0x1F00` | `0x0000` | Pegel Analog 2, 0 dB | `F0 00 20 0D 10 00 00 00 00 78 01 F7` |
| 17 | `0x2000` | `0x0000` | Pegel Analog 3, 0 dB | `F0 00 20 0D 10 00 00 00 00 00 02 F7` |
| 18 | `0x2100` | `0x0000` | Pegel Analog 4, 0 dB | `F0 00 20 0D 10 00 00 00 00 08 0A F7` |
| 19 | `0x2200` | `0x0000` | Pegel Analog 5, 0 dB | `F0 00 20 0D 10 00 00 00 00 10 0A F7` |
| 20 | `0x2300` | `0x0000` | Pegel Analog 6, 0 dB | `F0 00 20 0D 10 00 00 00 00 18 02 F7` |
| 21 | `0x2400` | `0x0000` | Pegel Analog 7, 0 dB | `F0 00 20 0D 10 00 00 00 00 20 0A F7` |
| 22 | `0x2500` | `0x0000` | Pegel Analog 8, 0 dB | `F0 00 20 0D 10 00 00 00 00 28 02 F7` |
| 23 | `0x2600` | `0x0000` | Pegel Phones 9, 0 dB | `F0 00 20 0D 10 00 00 00 00 30 02 F7` |
| 24 | `0x2700` | `0x0000` | Pegel Phones 10, 0 dB | `F0 00 20 0D 10 00 00 00 00 38 0A F7` |
| 25 | `0x2800` | `0x0000` | Pegel Phones 11, 0 dB | `F0 00 20 0D 10 00 00 00 00 40 0A F7` |
| 26 | `0x2900` | `0x0000` | Pegel Phones 12, 0 dB | `F0 00 20 0D 10 00 00 00 00 48 02 F7` |
| 27 | `0x4008` | `0x2666` | Input 9 → Analog 1, 30 % | `F0 00 20 0D 10 00 66 4C 20 00 04 F7` |
| 28 | `0x4048` | `0x2666` | Input 9 → Analog 2, 30 % | `F0 00 20 0D 10 00 66 4C 20 02 0C F7` |
| 29 | `0x4088` | `0x2666` | Input 9 → Analog 3, 30 % | `F0 00 20 0D 10 00 66 4C 20 04 0C F7` |
| 30 | `0x40C8` | `0x2666` | Input 9 → Analog 4, 30 % | `F0 00 20 0D 10 00 66 4C 20 06 04 F7` |
| 31 | `0x4108` | `0x2666` | Input 9 → Analog 5, 30 % | `F0 00 20 0D 10 00 66 4C 20 08 0C F7` |
| 32 | `0x4148` | `0x2666` | Input 9 → Analog 6, 30 % | `F0 00 20 0D 10 00 66 4C 20 0A 04 F7` |
| 33 | `0x4188` | `0x2666` | Input 9 → Analog 7, 30 % | `F0 00 20 0D 10 00 66 4C 20 0C 04 F7` |
| 34 | `0x41C8` | `0x2666` | Input 9 → Analog 8, 30 % | `F0 00 20 0D 10 00 66 4C 20 0E 0C F7` |
| 35 | `0x4208` | `0x2666` | Input 9 → Phones 9, 30 % | `F0 00 20 0D 10 00 66 4C 20 10 0C F7` |
| 36 | `0x4248` | `0x2666` | Input 9 → Phones 10, 30 % | `F0 00 20 0D 10 00 66 4C 20 12 04 F7` |
| 37 | `0x4288` | `0x2666` | Input 9 → Phones 11, 30 % | `F0 00 20 0D 10 00 66 4C 20 14 04 F7` |
| 38 | `0x42C8` | `0x2666` | Input 9 → Phones 12, 30 % | `F0 00 20 0D 10 00 66 4C 20 16 0C F7` |
| 39–168 | `0x4000 \| (o−1) << 6 \| (0x20 + p−1)` | `0x0000` | Playback p → Output o, aus, für alle p ≠ o aus 1…12 außer den Einträgen 1–2; nach Ausgang o, dann Playback p (z. B. 39 = `0x4021` Playback 2 → Analog 1, 149 = `0x42A2` Playback 3 → Phones 11, 168 = `0x42EA` Playback 11 → Phones 12) | `ff802-mixer-init --list` |

Die Einträge 1–38 sind die Konfiguration v3 (eigene Prüfsumme
`0xEC858A44`), die Einträge 1–26 die Konfiguration v2, unverändert in Inhalt und
Reihenfolge (eigene Prüfsumme `0xC97A359C`, ebenfalls vor jedem Lauf
geprüft); 3–26 sind die ursprüngliche Liste v1. Die Einträge 27–38 haben
die Adresse `0x4000 | (k−1) << 6 | 8` (Quellindex 8 = Mic/Inst 9) und den
Wert `0x2666` = `lround(0,3 · 0x8000)`. Die Liste ist eine Konstante in
[mixer_init.c](../src/mixer_init.c). Es wird zur Laufzeit keine Adresse
berechnet; vor jedem Lauf werden die Einträge gegen die Formeln und alle drei
Prüfsummen geprüft. `ff802-mixer-init --list` gibt genau diese Tabelle aus.

**Reihenfolge:** zuerst der getestete v3-Teil unverändert (die beiden
Kreuzverbindungen aus, die direkten Routings, die Ausgangspegel, Input 9),
dann die übrigen 130 Kreuzverbindungen aus. Bricht ein Lauf nach 26
Nachrichten ab, ist der v2-Teil vollständig, nach 38 genau v3; die
zusätzlichen Abschaltungen senken nur Pegel.

## MIDI-Protokoll und Kodierung

- **Registerwort (32 Bit):** Bit 31 Paritätsbit (ungerade Gesamtparität),
  Bits 30…16 Adresse, Bits 15…0 Wert ([reg_codec.c](../src/reg_codec.c)).
- **SysEx:** `F0 00 20 0D 10 00` (RME), das Wort in fünf 7-Bit-Gruppen,
  niederwertigste zuerst, `F7` – derselbe Rahmen wie beim getesteten
  Verlassen des CC-Modus (`0x3D44`).
- **Routing** Playback k → Output k: `0x4000 | (k−1)<<6 | (0x20+k−1)`,
  0 dB = `0x9000`.
- **Ausgangspegel** Output k: `0x1E00 + (k−1)<<8`, vorzeichenbehaftet in
  0,1 dB, 0 dB = `0x0000` (−20 dB = `0xFF38`, B.2).
- **Transport:** ALSA-Sequencer. Der Port wird ohne feste Nummern gefunden:
  Soundkarte der USB-Verbindung aus sysfs (`<k>:1.<n>/sound/card<N>` mit
  `midiC<N>D0`), Kernel-Client mit dieser Kartennummer, dessen Port 1 mit
  einem Namen auf „Port 2“. Das Programm meldet sich als Client
  „ff802-cc-auto“ an, verbindet seinen Port mit dem Geräteport und sendet
  je Register ein direkt zugestelltes SysEx-Ereignis.

## Ablauf einer Initialisierung

1. Registertabelle prüfen (Formeln, Prüfsumme).
2. Soundkarte der Verbindung abwarten (alle 100 ms, höchstens 15 s).
3. Sequencer öffnen, Port „Port 2“ dieser Karte abwarten (gleiche Frist).
4. Nur manuell: Nachfrage `SEND`.
5. Verbindung erneut prüfen: gleiche Bus- und Gerätenummer, weiterhin
   CC-Modus, gleiche Karte.
6. Mit dem Port verbinden.
7. Die 168 Nachrichten in Tabellenreihenfolge senden, 10 ms Abstand (etwa
   1,7 s je Durchgang), eine
   Protokollzeile je Register.
8. 200 ms warten, dann die Verbindung trennen (der Kernel leert dabei den
   MIDI-Ausgang) und den Sequencer schließen.

Zuerst die Routings, dann die Ausgangspegel: Bricht ein Lauf ab, bleiben
die Ausgangspegel unverändert.

## Integration in die CC-Umschaltung

Die Initialisierung läuft im bestehenden Dienst `ff802-cc-auto@.service`
im Zweig für ein Gerät im CC-Modus ([auto_switch.c](../src/auto_switch.c)),
unabhängig davon, ob die automatische Umschaltung an ist:

| Situation | Ablauf |
|---|---|
| Gerät startet im CC-Modus oder wird im CC-Modus angesteckt | udev „add“ → Dienst → Initialisierung |
| Gerät im proprietären Modus, Automatik an | `ff802-cc-auto-enable@` sendet den CC-Befehl und setzt den Port zurück (ab 0.2.0~dev4, [t4_port_reset.md](t4_port_reset.md)); neue Verbindung → „add“ → Initialisierung, nach dem Ende des Resets (Sperre) |
| `ff802-cc enable` (Port-Reset) | neue Verbindung → „add“ → Initialisierung |
| Rechnerneustart mit angeschlossenem Gerät | Coldplug-„add“ beim Booten → Initialisierung (kein angemeldeter Benutzer nötig) |
| Veraltetes Ereignis (Gerät inzwischen neu verbunden) | abgelehnt, keine MIDI-Aktion; das Ereignis der neuen Verbindung initialisiert |
| Mehrere oder gleichzeitige Ereignisse | über die Sperre `/run/ff802-cc-auto/lock` nacheinander; jeder Lauf setzt dieselben 168 Werte |

Es gibt bewusst keinen Eintrag „schon initialisiert“ je Verbindung: Die
Werte sind absolut, eine Wiederholung ändert nichts, und eine
wiederverwendete USB-Gerätenummer kann eine nötige Initialisierung nie
verhindern. Innerhalb eines Laufs wird nichts wiederholt.

**Zwei Durchgänge im Dienst (ab 0.2.0~dev6):** Nach dem ersten
vollständigen Durchgang wartet der Dienst 5 s, prüft die Verbindung
erneut und sendet dieselben 168 Werte noch einmal über dieselbe
Verbindung (Journal: `mixer: pass 2: set …`, Ergebnis „…, twice“). Grund:
Im Integrationstest von dev5 blieb Playback 9–12 → Phones 9–12 nach dem
Einschalten wiederholbar ohne Wirkung, obwohl alle Nachrichten ohne Fehler
übergeben wurden; derselbe Satz später gesendet wirkte immer
([mixer_v3_integration.md](mixer_v3_integration.md)). Der Mechanismus ist
nicht belegt. Ein Fehler im zweiten Durchgang gilt als `incomplete`; ist
das Gerät in der Pause verschwunden oder neu verbunden, entfällt der
zweite Durchgang. Der Handlauf `ff802-mixer-init` sendet einmal.

## Fehlerbehandlung

| Ergebnis | Bedeutung | Exit-Code des Dienstes |
|---|---|---|
| `done` | alle 168 Nachrichten an ALSA übergeben | 0 |
| `no-port` | Karte oder Port nicht innerhalb von 15 s | 3 |
| `refused` | Port nicht eindeutig, Name passt nicht, Verbindung geändert, Tabellenprüfung | 3 |
| `not-sent` | Sequencer, Portliste oder Verbindung nicht möglich (z. B. belegt) | 3 |
| `disabled-build` | Programm ohne `FF802_ENABLE_MIXER_WRITE` gebaut | 3 |
| `incomplete` | Senden bei Nachricht n gescheitert; n−1 übergeben, Rest nicht | 4 |

Vor dem Senden steht im Journal die Konfiguration
(`mixer: configuration ff802-mixer-init-v4, 168 entries, checksum
0xC43D2879 (verified).`, `mixer: entries 1..38 are ff802-mixer-init-v3,
checksum 0xEC858A44 (verified).` und `mixer: entries 1..26 are ff802-mixer-init-v2,
checksum 0xC97A359C (verified).`); `mixer-result` enthält dieselben Angaben
(`config=`, `entries=`, `checksum=`, `base=`, `base_entries=`,
`base_checksum=`; `base` ist v3). Die Kennung ändert sich mit jeder
Änderung der Liste (v1: 24 Einträge, v2: 26, v3: 38, v4: 168).

Jeder Lauf schreibt `/run/ff802-cc-auto/mixer-result` und je Register eine
Journalzeile (`journalctl -t ff802-cc-auto`). Ein Fehlschlag erzeugt
zusätzlich `/run/ff802-cc-auto/mixer-event` und damit die
Desktop-Benachrichtigung „Fireface 802 – Mixer-Initialisierung
fehlgeschlagen“, getrennt von „Class-Compliant-Modus aktiv“. Die
CC-Umschaltung selbst ist davon nicht betroffen (`last-result` bleibt
`cc-active`). „done“ bestätigt die Übergabe an ALSA, nicht, dass das Gerät
die Werte übernommen hat.

## Sicherheitsmechanismen

- **Zwei Schalter:** Schreibfunktion beim Bauen (`FF802_ENABLE_MIXER_WRITE`,
  CMake-Standard OFF, nur der Paketbau setzt ON) und Laufzeitschalter
  `/var/lib/ff802-cc-auto/mixer-init` (nach der Installation aus).
- **Getrennt von den Testprogrammen:** `ff802-mixer-b2`/`-b3` haben eigene
  Optionen (`FF802_BUILD_MIXER_B2/B3`, Standard OFF), werden nie
  installiert und sind nicht im Paket.
- **Feste Liste:** keine frei wählbare Adresse oder Wert; Prüfsumme;
  Quelltextprüfung (`check_mixer_sources`): ALSA-Aufrufe nur in
  `midi_seq_alsa.c`, SysEx-Rahmen nur in den Protokoll- und Testmodulen.
- **Nur das richtige Gerät:** Identität aus sysfs (USB-ID, „RME“,
  Produkt „Fireface 802“, High Speed), nur im CC-Modus; manuell bei
  mehreren Geräten keine stille Auswahl.
- **Keine unkontrollierten Wiederholungen**, keine anderen Register, kein
  Zugriff im proprietären Modus (dort nur die bestehende Umschaltung).
- **Sperre** zwischen Dienst und manuellem Lauf.
- **Dienst-Härtung** unverändert bis auf `DeviceAllow=char-alsa rw`.

## Aktivierung und Deaktivierung

```sh
ff802-cc-auto mixer-init status     # Zustand, Build-Schalter, letztes Ergebnis
ff802-cc-auto mixer-init on         # Administrator; gilt ab der nächsten Verbindung
ff802-cc-auto mixer-init off        # aktiver Benutzer ohne Passwort
```

`on` sendet nichts sofort. Die automatische Umschaltung
(`ff802-cc-auto on|off`) ist davon unabhängig.

## Manuelle Ausführung

```sh
ff802-mixer-init --list                         # die 26 Nachrichten, kein Gerätezugriff
ff802-mixer-init --dry-run                      # Gerät, Karte, Port; sendet nichts
ff802-mixer-init --apply                        # nach Eingabe von SEND: 26 Nachrichten
ff802-mixer-init --apply --connection 3-2.4.1.1.3   # bei mehreren Geräten
```

Gleichwertig: `ff802-cc-auto mixer-init dry-run` bzw. `… apply`. Läuft als
normaler Benutzer der Sitzung, unabhängig von `mixer-init on`.

## Verifikation

1. `ff802-mixer-init --dry-run` muss Karte und Port „… Port 2“ melden.
2. Nach `--apply` bzw. einem automatischen Lauf: `ff802-cc-auto mixer-init
   status` zeigt `outcome=done`, `written=26/26`.
3. Akustisch je Ausgangspaar mit einem einkanaligen Signal (ohne die
   Standard-Senke zu ändern):

   ```sh
   wpctl status                                  # ID der Senke
   wpctl inspect <ID> | grep node.name
   PIPEWIRE_NODE=<node.name> speaker-test -D pipewire -c 2 -s 1 -t sine -f 440 -l 1   # nur links
   PIPEWIRE_NODE=<node.name> speaker-test -D pipewire -c 2 -s 2 -t sine -f 440 -l 1   # nur rechts
   ```

   Links nur links, rechts nur rechts, beide gleich laut. Ist auf der
   anderen Seite etwas zu hören, besteht eine gekreuzte Verbindung, die die
   Initialisierung nicht löscht.

## Installation und Wartung

**Bauen** (Dev-Container oder Kubuntu mit `cmake`, `gcc`,
`libusb-1.0-0-dev`, `libasound2-dev`, `dpkg-dev`):

```sh
cmake -S . -B build
cmake --build build --target deb-auto      # build/deb/ff802-cc-auto_0.2.0~dev3_amd64.deb
```

Der Paketbau ist reproduzierbar und führt keines der Programme aus.

**Installieren und aktivieren** (auf dem Host, erst nach Freigabe):

```sh
sudo apt install ./build/deb/ff802-cc-auto_0.2.0~dev3_amd64.deb
ff802-cc-auto mixer-init dry-run
ff802-cc-auto mixer-init on
```

**Status und Protokoll:**

```sh
ff802-cc-auto status
ff802-cc-auto mixer-init status
journalctl -t ff802-cc-auto -b                  # Dienst, eine Zeile je Register
journalctl --user -t ff802-cc-notify -b         # Benachrichtigungen
systemctl list-units 'ff802-cc-auto@*'
```

**Deinstallieren:**

```sh
ff802-cc-auto mixer-init off                     # optional, sofort wirksam
sudo apt purge ff802-cc-auto                     # entfernt auch /var/lib/ff802-cc-auto
```

**Installierte Dateien** (zusätzlich zu den bisherigen von
`ff802-cc-auto`):

| Datei | Zweck |
|---|---|
| `/usr/bin/ff802-mixer-init` | manuelle Initialisierung |
| `/usr/libexec/ff802-cc/ff802-cc-autoswitch` | Dienstprogramm, jetzt mit Mixer-Initialisierung |
| `/usr/libexec/ff802-cc/ff802-cc-auto-mixer-on`, `-mixer-off` | Root-Hilfsprogramme für `mixer-init on/off` |
| `/usr/lib/systemd/system/ff802-cc-auto@.service` | Dienst (`DeviceAllow=char-alsa rw`) |
| `/usr/lib/systemd/system/ff802-cc-auto-enable@.service` | CC-Befehl und Port-Reset (proprietärer Modus, ab 0.2.0~dev4) |
| `/usr/lib/systemd/user/ff802-cc-notify.path` | beobachtet auch `mixer-event` |
| `/usr/share/polkit-1/actions/org.ff802cc.auto.policy` | Aktionen `mixer-on` (Administrator), `mixer-off` (aktiver Benutzer) |
| `/var/lib/ff802-cc-auto/mixer-init` | Laufzeitschalter (zur Laufzeit angelegt) |
| `/run/ff802-cc-auto/mixer-result`, `mixer-event` | letztes Ergebnis, Benachrichtigung (zur Laufzeit) |

Bei ausgeschalteter Mixer-Initialisierung verhält sich das Paket wie
bisher: Der Dienst öffnet weder den Sequencer noch schreibt er Mixerwerte.

## Bekannte Einschränkungen

- **Nur zwei Querverbindungen:** Abgeschaltet werden nur Playback 1 →
  Phones 9 und Playback 2 → Phones 10. Andere gekreuzte Routings und
  Eingangs-Routings bleiben, wie sie sind. Ob der CC-Modus weitere
  Kreuzverbindungen setzt (andere Kanäle, Ausgänge über 12), ist nicht
  untersucht.
- **Keine Rückmeldung:** Das Gerät wird nicht gelesen; Erfolg heißt
  „an ALSA übergeben“.
- **Gehör:** 0 dB an Phones 9–12 kann laut sein; jede Verbindung setzt
  die Ausgangspegel zurück, manuelle Änderungen an diesen 26 Werten gehen
  verloren. Ob der Lautstärkeknopf am Gerät auf `0x2600`–`0x2900` wirkt,
  ist nicht geklärt.
- **Nicht am Gerät getestet:** die neue Folge von 26 Nachrichten als
  Ganzes (die bisherigen 24 und die beiden neuen einzeln sind bestätigt),
  die Wirkung der abgeleiteten Register einzeln (Playback 3–8,
  Ausgangspegel 2–8), der Lauf in der gehärteten Unit und beim Booten.
- **Stereokopplung:** B.3 zeigte für das Paar 9/10 keine Kopplung bei
  Routing-Schreibzugriffen; bei B.2 wurden beide Kopfhörerkanäle subjektiv
  leiser. Für die anderen Paare ist das Verhalten nicht geprüft.
- **Abhängig von alsa-lib** (`libasound2t64`) und dem Kernel-Sequencer
  (`snd-seq`, `snd-seq-midi`).
