# Diagnose D3: Playback 1 → Phones 9 abschalten

> **Stand 2026-11-23: vorbereitet, nicht ausgeführt.** Es wurde keine
> MIDI-Nachricht an das Gerät gesendet. Betrifft nur das ursprüngliche
> RME Fireface 802 im Class-Compliant-Modus.

## Ausgangslage (vom Anwender gemeldet)

| Test | Nachricht | Ergebnis |
|---|---|---|
| D1 | `0x4269 = 0x0000` | Playback 10 → Phones 10 abgeschaltet |
| D1 Restore | `0x4269 = 0x9000` | Verbindung wiederhergestellt |
| D2 | `0x4261 = 0x0000` (mit `ff802-mixer-b4`) | Playback 2 → Phones 10 abgeschaltet |

Damit ist belegt: `0x0000` schaltet eine Matrixverbindung über den eigenen
Sendeweg ab, und die unerwünschte Ausgabe von „Analog Output 1/2“ auf den
Kopfhörern sind normale Matrixeinträge. Übrig ist bei Auswahl von „Analog
Output 1/2“ der linke Kanal auf Phones 9.

## Register und Nachricht

`0x4220` = `0x4000 | (8 << 6) | (0x20 + 0)`: Ausgangsindex 8 (Phones 9,
derselbe Block wie das bestätigte `0x4228`), Quelle Playback 1 (dieselbe
Quelle wie `0x4020`).

| Aufruf | Register = Wert | Wort | SysEx |
|---|---|---|---|
| `--apply` | `0x4220 = 0x0000` (aus) | `0x42200000` | `F0 00 20 0D 10 00 00 00 00 11 04 F7` |

Adresse und Wert haben zusammen drei gesetzte Bits, die Gesamtparität ist
ungerade, das Paritätsbit 31 bleibt 0. Geprüft mit dem Projekt-Codec, einer
unabhängigen Shell-Rechnung und im Test `mixer_d3`.

## Programm

[ff802_mixer_d3.c](../tools/hw-test/ff802_mixer_d3.c), CMake-Option
`FF802_BUILD_MIXER_D3` (Standard OFF), nie installiert. Erkennung und
Sendeweg wie B.4/D1. Ohne Option Dry-Run; `--apply` zeigt Gerät, Port,
Register, Wert und SysEx, verlangt `SEND` an einem Terminal und sendet genau
eine Nachricht. Keine Wiederherstellung, keine Wiederholung, keine anderen
Register.

## Hörtest

1. Lautstärke niedrig, Kopfhörer an Phones 9/10.
2. Senke „Analog Output 1/2“ nur links:
   `PIPEWIRE_NODE=<node.name> speaker-test -D pipewire -c 2 -s 1 -t sine -f 440 -l 2`
   – heute: links auf Phones 9 hörbar.
3. `--apply`, `SEND`.
4. Schritt 2 wiederholen – erwartet: Kopfhörer still. Zusätzlich `-s 2`:
   weiterhin still (D2).
5. Gegenprobe Senke „Headphones 9/10“ links und rechts: beide hörbar
   (Playback 9/10 → Phones 9/10 unverändert).

## Wiederherstellung

Der Wert vor dem Test ist unbekannt. Der Zielzustand dieser Verbindung ist
„aus“; eine Rückkehr zur unerwünschten Ausgabe ist nicht vorgesehen. Ein
späteres `0x4220 = 0x9000` wäre eine neue 0-dB-Verbindung, keine
Wiederherstellung.

## Grenze

Der Test betrifft nur Playback 1 → Phones 9. Weitere vom CC-Modus gesetzte
Kreuzverbindungen (andere Playback-Kanäle oder Ausgänge) erfasst er nicht;
dafür wäre die erweiterte Initialisierung („definierte Playback-Matrix“)
vorgesehen.
