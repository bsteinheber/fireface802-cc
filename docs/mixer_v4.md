# Mixer-Konfiguration v4: vollständige 12×12-Playback-Matrix

> **Stand 2026-11-25: implementiert und automatisch getestet, am Gerät
> noch nicht geprüft.** Entwicklungspaket `ff802-cc-auto 0.2.1~dev1`.
> Betrifft nur das ursprüngliche RME Fireface 802 im CC-Modus.

## Anlass

Hardwaretest des Release-Kandidaten v0.5.0 (`ff802-cc-auto` 0.2.0,
Konfiguration v3), vom Anwender gemeldet (2026-11-25):

- Playback 3/4 war auf Analog 3/4 **und** auf Phones 11/12 zu hören.
- Playback 11/12 auf Phones 11/12, Phones 9/10 und Input 9 wie erwartet.
- `pw-link -l`: PipeWire leitet das Signal nur an die Kanäle von
  „Analog 3/4“ (AUX2/AUX3); keine PipeWire-Verbindung zu Phones 11/12.

Folgerung: eine im Mixer verbliebene Kreuzverbindung, mit hoher
Wahrscheinlichkeit Playback 3 → Phones 11 und/oder Playback 4 → Phones
12. v3 schaltet nur die beiden Kreuzverbindungen Playback 1/2 → Phones
9/10 ab. v4 legt die ganze 12×12-Matrix fest.

## Register und Werte

| Größe | Wert | Beleg |
|---|---|---|
| Adresse Playback p → Output o | `0x4000 \| (o−1) << 6 \| (0x20 + p−1)` | OSCMix-Fork `ctltoreg(MIX_LEVEL)` (Playback-Index `+ 0x20 − 30`), FireWire-Protokoll (Mixer-Schritt `0x40`, Stream ab `0x20`); am Gerät: `0x4020`, `0x4061`, `0x4228`, `0x4269`, `0x42AA`, `0x42EB` (OSCMix, 2026-11-21), `0x4220`, `0x4261` (D3, D2), `0x4229`, `0x4268` (von OSCMix mit `0x0000` geschrieben) |
| 0 dB | `0x9000` | am Gerät (B.3, v2/v3) |
| aus („−∞“) | `0x0000` | am Gerät: D1 (`0x4269` aus und wieder an), D2 (`0x4261`), D3 (`0x4220`); OSCMix setzt Pegel 0 als `0x0000` |
| Eindeutigkeit | Block je Ausgang 64 Quellen: Eingänge `0x00–0x1D`, Playback `0x20–0x3D`; Playback 1–12 = `0x20–0x2B` | Quellcode beider Projekte; Test `test_playback_matrix` |

`0x0000` ist am Gerät als **hörbar aus** belegt; ob es exakt −∞ dB ist,
ist nicht gemessen. Das FireWire-Projekt nennt `0x8000` „−65 dB“; dieser
Wert wird bewusst nicht verwendet.

## Die Liste v4 (168 Einträge)

| Nr. | Inhalt | Wert |
|---|---|---|
| 1–38 | Konfiguration v3, unverändert (1–26 = v2) | wie bisher |
| 39–168 | die übrigen 130 Playback p → Output o, p, o = 1…12, p ≠ o, ohne die zwei aus 1–2; nach Ausgang, dann Playback | `0x0000` |

Zusammen: 144 Matrixverbindungen, 12 mit 0 dB (p = o), 132 aus, jede
genau einmal, auch beide Richtungen innerhalb jedes Stereopaars
(z. B. Playback 1 → Analog 2 und Playback 2 → Analog 1). Die Verbindung
aus dem Befund steht an Nr. 149 (`0x42A2`, Playback 3 → Phones 11) und
Nr. 161 (`0x42E3`, Playback 4 → Phones 12).

Prüfsummen (FNV-1a, Adresse und Wert): alle 168 `0xC43D2879`, die ersten
38 `0xEC858A44` (v3), die ersten 26 `0xC97A359C` (v2); alle drei werden
vor jedem Lauf geprüft und in den Tests aus Literalen nachgerechnet.

Nicht in der Liste: Playback 13–30 (AES, ADAT), Ausgänge 13–30,
Eingangs-Routings außer Input 9, Eingangsverstärkung, Mute, Pan.

## Übertragung

- 168 Nachrichten zu je 12 Byte, 10 ms Abstand: ein Durchgang etwa
  1,7 s, Datenrate etwa 1,2 kB/s (eine Nachricht dauert selbst bei
  MIDI-Geschwindigkeit, 31250 Bit/s, 3,8 ms, also weniger als der Abstand).
- Dienst: zwei Durchgänge, 5 s Pause, erneute Geräteprüfung davor:
  etwa 8,6 s. Zeitbudget der Unit (`TimeoutStartSec=60`): bis etwa 20 s
  Warten auf die Sperre (laufender Port-Reset) + bis 15 s Warten auf den
  MIDI-Port + 8,6 s ≈ 44 s.
- Handlauf `ff802-mixer-init`: ein Durchgang, etwa 1,9 s; er hält die
  Sperre so lange.
- Fehler: unverändert. Abbruch bei Nachricht n → `incomplete` mit
  n−1 übergebenen Nachrichten, Benachrichtigung, keine Wiederholung; ein
  Fehler im zweiten Durchgang → `incomplete` („pass 2: … failed; pass 1
  complete“). Bricht ein Lauf nach Nachricht 38 ab, ist genau v3 gesetzt.
- Ungeprüft: ob das Gerät 168 Nachrichten in Folge vollständig übernimmt.

## Automatisch geprüft

Gesamtsuite 85/85 (Build `build-v4-test`), darunter:

- `test_mixer_init` (25269 Prüfungen): 168 Einträge gegen Literale (die
  130 neuen unabhängig in der Shell berechnet), Vollständigkeit der
  Matrix (jede Verbindung genau einmal), 12 aktiv, 132 aus, beide
  Richtungen in allen Stereopaaren aus, `0x42A2`/`0x42E3` aus, Input 9
  und Ausgangspegel unverändert, v3- und v2-Präfix, drei Prüfsummen,
  nichts außerhalb der Matrix (Playback/Ausgang 13+), SysEx von
  `0x42A2 = 0x0000` aus Shell-Rechnung.
- `test_mixer_run` (1128): 168 Nachrichten, Abbruch bei 7, 27, 38, 39,
  168; zwei Durchgänge (336 Nachrichten, byte-gleich), Fehler im zweiten
  Durchgang bei Nachricht 7 und 168, Überspringen nach Neuverbindung.
- `auto_flow` (87), `mixer_init_tool` (45), `auto_switch`, `auto_control`,
  `auto_notify`: Dienst- und Handlauf byte-genau, Journal, `mixer-result`.
- `deb_auto_package`: die 168 Einträge aus den Formeln als Bytes in Dienst
  und `ff802-mixer-init`, drei Prüfsummen in der Shell.

## Hardwaretest (noch auszuführen)

Siehe den Bericht zur Bereitstellung bzw. die Befehle unten; Auswertung
mit `tools/hw-test/ff802-v3-check.sh post` (prüft jetzt v4).

1. Paket installieren, Fireface aus- und einschalten.
2. Prüfskript: v4-Konfiguration, 168 Register in beiden Durchgängen,
   132 Verbindungen aus (einschließlich `0x42A2`, `0x42E3`).
3. Hörprüfung: Playback 3/4 nur auf Analog 3/4, **nicht** mehr auf
   Phones 11/12; jedes Paar nur auf seinem Ausgang, links und rechts
   getrennt; Input 9 auf allen zwölf Ausgängen.
4. Zweites Aus- und Einschalten, Hörprüfung wiederholen.

## Rückweg

- Release-Kandidat (v3): `build-release-v0.5.0/ff802-cc-auto_0.2.0_amd64.deb`
  mit `dpkg -i`. Die von v4 ausgeschalteten 130 Verbindungen bleiben aus
  (v3 schreibt sie nicht); das ist der gewünschte Zustand.
- Ältere Stände: `build-deb-dev6`, `build-deb-dev4`.
