# Hardwaretest E1: Input 9 → Analog Output 3 mit 30 % linear

> **Stand 2026-11-23: am Gerät ausgeführt, zweiter Lauf PASSED (Exit 0, vom Anwender
> gemeldet).** `0x2666` ist damit die Pegelkodierung der Konfiguration
> `ff802-mixer-init-v3` ([mixer_init.md](mixer_init.md)). Betrifft nur das
> ursprüngliche RME Fireface 802 im Class-Compliant-Modus.

## Zweck

Vor der Erweiterung der Mixer-Initialisierung um Input 9 → Ausgänge 1–12
(geplant als `ff802-mixer-init-v3`, 38 Register) prüft E1 an **einem**
Register, ob das Gerät den Wert `0x2666` wie erwartet als 30 % lineare
Amplitude (≈ −10,5 dB) versteht. Die 26-Register-Konfiguration
`ff802-mixer-init-v2` bleibt unverändert und aktiv.

## Register und Nachrichten

| | Register = Wert | Wort | SysEx |
|---|---|---|---|
| OFF (Ausgangs- und Endzustand) | `0x4088 = 0x0000` | `0x40880000` | `F0 00 20 0D 10 00 00 00 20 04 04 F7` |
| TEST | `0x4088 = 0x2666` | `0xC0882666` | `F0 00 20 0D 10 00 66 4C 20 04 0C F7` |

- `0x4088 = 0x4000 | (2 << 6) | 8`: Ausgang 2 = Analog 3, Quelle 8 =
  Mic/Inst 9 (OSCMix `device_ff802.c` `inputs[8]`, `ctltoreg(MIX_LEVEL)`;
  FireWire-Protokoll: Mixer-Schritt `0x40`, 8 Line + 4 Mic).
- `0x2666 = lroundf(0.3 · 0x8000)` (OSCMix `setmixlevel()`, Bereich bis
  0,5 linear). **Am Gerät nicht bestätigt** – das prüft E1.
- `0x0000` = aus ist am Gerät belegt (D1–D3).
- SysEx dreifach geprüft: Programm gegen den Register-Codec, Testskript
  mit eigener Shell-Rechnung, lokale Rechnung mit `ff802_rme_sysex_reg()`.

## Sicherheit

- Genau diese zwei Nachrichten im Programm, nur Register `0x4088`; keine
  Eingabe von Adressen oder Werten, keine Tabelle, kein Refresh, keine
  Eingangsverstärkung, kein Ausgangspegel (geprüft im Test `mixer_e1`
  und in `mixer_sources`).
- Schreibzugriffe erst nach `SEND` am Terminal; jede Nachricht genau
  einmal, keine Wiederholung.
- Nach dem Testwert wird **immer** OFF gesendet: auch bei Abbruch, Ende
  der Eingabe, Zeitablauf (10 min), Strg-C/SIGTERM/SIGHUP oder
  fehlgeschlagenem TEST. Scheitert OFF, meldet das Programm
  „RESTORE FAILED“ (Exit 6) und nennt `ff802-mixer-e1 --restore`.
- Der Vorwert von `0x4088` wird nicht gelesen (kein zuverlässiger Weg
  ohne den ungeprüften Refresh, siehe Analyse). Definierter Zustand ist
  „aus“; ein unhörbar kleiner Vorwert ginge dabei verloren.
- Die automatische Initialisierung schreibt `0x4088` nicht; ein Umstecken
  setzt einen stehengebliebenen Testwert also **nicht** zurück.
- CMake-Option `FF802_BUILD_MIXER_E1` (Standard OFF), nie paketiert oder
  installiert, kein Lauf beim Build oder bei der Installation.
- Während des Tests das Gerät nicht umstecken; eine gleichzeitige
  automatische Initialisierung (anderes Register) wird nicht gesperrt.

## Ablauf auf dem Kubuntu-Host

Programm (im Container gebaut, Release):
`build-e1/ff802-mixer-e1`, SHA-256
`2c190d6bbfb7b04245a630a5348bc747cf67b5e2dbafa761a20573b6097ecedf`.

Vorbereitung: Abhörlautstärke an Analog Out 3/4 herunter, Kopfhörer ab.
Eine gleichmäßige Quelle an Input 9 (Mic/Inst 9), z. B. Line-Signal oder
Musik, moderater Pegel; Gain-Regler nicht verändern; kein Mikrofon vor
Lautsprechern. Analog Out 3 ist der **linke** Kanal des Paars 3/4.

```sh
cd <Projektverzeichnis>    # der lokale Klon von fireface802-cc
build-e1/ff802-mixer-e1                 # Probelauf: findet Gerät, Karte, Port; sendet nichts
build-e1/ff802-mixer-e1 --apply         # der Test, ein Aufruf
```

`--apply` führt durch:

1. Plan und Hinweise, dann `SEND` (Freigabe aller drei Schreibzugriffe).
2. Frage: Ist Input 9 **vor** dem Test auf Analog 3 hörbar?
3. OFF, TEST.
4. Abhören (bis 10 min, Strg-C stellt zurück): Was ist auf Analog 3 zu
   hören? Ist Analog 4 still?
5. OFF (Wiederherstellung).
6. Frage: Ist Analog 3 wieder still?
7. Zusammenfassung; Protokoll in `ff802-mixer-e1-<Datum>-<Zeit>.log` im
   aktuellen Verzeichnis (oder `--log <Datei>`).

## Bewertung

| Antwort beim Abhören | Deutung | Ergebnis |
|---|---|---|
| deutlich hörbar, Out 4 still, danach still | `0x2666` wirkt als Pegel deutlich unter Vollaussteuerung, nur auf Out 3 | **PASSED** (Exit 0) – Voraussetzung für v3 |
| nur sehr leise | Hinweis auf dB-lineare Kodierung (`0x2666` ≠ 30 %) | nicht bestätigt (5) |
| nichts | Wert wirkt nicht oder Quelle fehlt | nicht bestätigt (5) |
| unerwartet laut/verzerrt | Kodierung anders als angenommen | nicht bestätigt (5) |
| Out 4 hörbar | falsches Register oder Stereo-Kopplung | nicht bestätigt (5) |

Die MIDI-Übertragung allein ist kein Ergebnis: Das Gerät bestätigt nichts,
und es wird nichts zurückgelesen. „PASSED“ beruht auf der Rückmeldung
beim Abhören, nicht auf einer Messung.

**Objektive Messung:** ohne Zusatzhardware nicht möglich (Ausgang 3 kann
nur über ein Kabel auf einen Eingang zurückgeführt werden; die interne
Loopback-Funktion wäre ein weiterer Hardwareparameter). Optional mit
Kabel Analog Out 3 → externes Aufnahmegerät: der Pegel gegenüber einer
0-dB-Verbindung sollte etwa −10,5 dB betragen (bei dB-linearer Kodierung
wären es etwa −45,5 dB). Nicht über einen Eingang des Fireface selbst,
solange dessen Routing auf Analog 3 unbekannt ist (Rückkopplung).

## Exit-Status

0 bestanden · 2 Aufruf · 3 abgelehnt/abgebrochen vor dem ersten
Schreibzugriff · 4 ein Schreibzugriff schlug fehl (OFF gesendet) ·
5 durchgeführt und zurückgestellt, Wirkung nicht bestätigt ·
6 **Wiederherstellung fehlgeschlagen** (`0x4088` evtl. noch `0x2666`).

## Ergebnis

Ausgeführt am 2026-11-23 auf dem Kubuntu-Host mit `build-e1/ff802-mixer-e1
--apply` (Verbindung 3-2.4.1.1.3, devnum 30, Karte 1, Port 20:1). Es gibt
**zwei Läufe**; beide Protokolle stehen unverändert in [hw_results/](hw_results/):

| Lauf | Protokoll | Antworten (vorher / Out 3 / Out 4 / nachher) | Ergebnis |
|---|---|---|---|
| 1, 18:33–18:42 | [ff802-mixer-e1-20261123-183323.log](hw_results/ff802-mixer-e1-20261123-183323.log) | still / **nichts** / unklar / unklar | NOT CONFIRMED, Exit 5; zurückgestellt |
| 2, 18:43–18:46 | [ff802-mixer-e1-20261123-184341.log](hw_results/ff802-mixer-e1-20261123-184341.log) | still / deutlich hörbar / still / still | **PASSED**, Exit 0 |

Beide Läufe sendeten dieselben drei Nachrichten fehlerfrei. Lauf 1 ist als
**NOT CONFIRMED** dokumentiert: Die erwartete Wirkung wurde nicht bestätigt;
eine Ursache ist nicht festgestellt. Lauf 2 ist **PASSED** und vom Anwender
als Ergebnis gemeldet:

| Schritt | Beobachtung |
|---|---|
| `0x4088 = 0x0000` (definierter Ausgangszustand) | Analog 3 stumm |
| `0x4088 = 0x2666` | Input 9 auf Analog 3 **deutlich hörbar** |
| Gegenprobe | Analog 4 blieb stumm |
| `0x4088 = 0x0000` (Wiederherstellung) | Analog 3 wieder stumm |
| MIDI | keine Fehler bei den drei Übertragungen |
| Ergebnis | **PASSED**, Exit 0 |

Bewertung: `0x2666` wirkt als Routingpegel deutlich unter Vollaussteuerung
und nur auf dem adressierten Ausgang; die Adressformel für Hardware-
Eingänge (Quellindex 8 = Mic/Inst 9) ist für Analog 3 bestätigt. Das
spricht für die lineare Kodierung von OSCMix und gegen die dB-lineare
Lesart (≈ −45,5 dB wäre „sehr leise“). Es ist eine Hörprüfung, keine
Pegelmessung: dass es genau −10,5 dB sind, ist nicht gemessen. Die
übrigen elf Ausgänge sind aus derselben Formel abgeleitet und werden im
Integrationstest der v3-Konfiguration geprüft.
