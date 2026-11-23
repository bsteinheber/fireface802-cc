# Diagnose D1: Schaltet `0x0000` eine Matrixverbindung ab?

> **Stand 2026-11-23: vorbereitet, nicht ausgeführt.** Es wurde keine
> MIDI-Nachricht an das Gerät gesendet. Betrifft nur das ursprüngliche
> RME Fireface 802 im Class-Compliant-Modus.

## Zweck

B.4 (`0x4261 = 0x0000`) blieb ohne hörbare Wirkung. Ob das am Signalweg
oder am Wert lag, ist offen ([mixer_b4.md](mixer_b4.md)). D1 prüft den
Wert `0x0000` an einer Verbindung, die nachweislich besteht und hörbar
ist: **Playback 10 → Hardware Output 10 (Phones 10)**, Register `0x4269`,
von der Mixer-Initialisierung auf `0x9000` (0 dB) gesetzt und am Gerät
bestätigt.

## Nachrichten

| Aufruf | Register = Wert | Wort | SysEx |
|---|---|---|---|
| `--apply` | `0x4269 = 0x0000` (vermutlich aus) | `0xC2690000` | `F0 00 20 0D 10 00 00 00 24 13 0C F7` |
| `--restore` | `0x4269 = 0x9000` (0 dB) | `0xC2699000` | `F0 00 20 0D 10 00 00 20 26 13 0C F7` |

`0x4269` hat sechs gesetzte Bits, das Paritätsbit 31 ist deshalb gesetzt.
Geprüft mit dem Projekt-Codec, einer unabhängigen Shell-Rechnung und im
Test `mixer_d1`. (Ein früherer Untersuchungsbericht nannte irrtümlich
`… 24 13 04 F7` mit Paritätsbit 0; diese Nachricht wurde nie gesendet, der
Test schließt sie aus.)

## Programm

[ff802_mixer_d1.c](../tools/hw-test/ff802_mixer_d1.c), CMake-Option
`FF802_BUILD_MIXER_D1` (Standard OFF), nie installiert. Erkennung und
Sendeweg wie B.3/B.4. Ohne Option Dry-Run; `--apply` und `--restore`
zeigen Gerät, Port, Register, Wert und SysEx, verlangen `SEND` an einem
Terminal und senden je genau eine Nachricht. Keine automatische
Wiederherstellung, keine Wiederholung, keine anderen Register.

## Hörtest

1. Lautstärke niedrig, Kopfhörer an Phones 9/10.
2. Senke „Headphones 9/10“ nur rechts:
   `PIPEWIRE_NODE=<node.name> speaker-test -D pipewire -c 2 -s 2 -t sine -f 440 -l 2`
   – erwartet: rechts hörbar.
3. `--apply`, `SEND`.
4. Schritt 2 wiederholen.
5. `--restore`, `SEND`, Schritt 2 wiederholen: rechts wieder hörbar.

| Ergebnis nach Schritt 3 | Deutung |
|---|---|
| rechts **still** | `0x0000` schaltet eine Verbindung über den eigenen Sendeweg ab. B.4 ist dann ein Hinweis, dass Playback 2 nicht über `0x4261` auf Phones 10 kommt (oder dass dort zusätzlich ein anderer Weg besteht) |
| rechts **unverändert** | `0x0000` wirkt nicht als „aus“ (oder wird nicht übernommen); B.4 ist dann nicht aussagekräftig |
| rechts **leiser, aber hörbar** | `0x0000` ist nicht „aus“, sondern ein niedriger Pegel, oder es gibt einen zweiten Weg |

**Wiederherstellung:** Der Wert vor dem Test ist bekannt (`0x9000`, gesetzt
durch die Mixer-Initialisierung). `--restore` stellt ihn wieder her;
ebenso `ff802-mixer-init --apply` (setzt `0x4269 = 0x9000` mit den übrigen
23 Werten).
