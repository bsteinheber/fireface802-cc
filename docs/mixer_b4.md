# Hardwaretest B.4: Routing Playback 2 → Phones 10 abschalten

> **Stand 2026-11-22: vorbereitet, nicht ausgeführt.** Es wurde keine
> MIDI-Nachricht an das Gerät gesendet. Die Schreibschritte brauchen eine
> gesonderte Freigabe. Betrifft nur das ursprüngliche RME Fireface 802 im
> Class-Compliant-Modus.

## Ausgangssituation

- Die Wiedergabe über die getrennten PipeWire-Stereosenken funktioniert
  grundsätzlich; Analog 3/4 wurde über einen Phonitor mini in Stereo
  erfolgreich gehört.
- Wird über die Senke „Analog Output 1/2“ wiedergegeben, ist das Signal
  unerwartet auf dem Kopfhörer an Phones 9/10 zu hören.
- Einkanalig: Playback 1 (links) ist auf Phones 9/10 **nicht** hörbar,
  Playback 2 (rechts) ist auf Phones 9/10 **nur rechts** hörbar.
- Die PipeWire-Verbindungen zu den USB-Kanälen 9/10 wurden geprüft; es gibt
  keine erkennbare zusätzliche Verbindung von „Analog Output 1/2“ dorthin.

## Hypothese

In der internen DSP-Matrix besteht eine Verbindung **Playback 2 → Hardware
Output 10 (Phones 10, rechts)**. Sie liegt außerhalb der 24 Register der
Mixer-Initialisierung und wird von dieser bewusst nicht verändert
([mixer_init.md](mixer_init.md): bestehende Querverbindungen werden nicht
gelöscht).

## Technische Grundlage

- Matrixregister ([mixer_routing.md](mixer_routing.md)), Indizes
  nullbasiert:
  `reg = 0x4000 | (output_index << 6) | (0x20 + playback_index)`.
  Bestätigt am Gerät mit `0x4228` = Playback 9 → Output 9 und `0x4269` =
  Playback 10 → Output 10.
- **Playback 2 → Output 10:** Ausgangsindex 9, Playback-Index 1:
  `0x4000 | (9 << 6) | 0x21` = **`0x4261`**.
- Die zuerst genannte Adresse `0x4069` wäre nach derselben Formel
  Ausgangsindex 1, Quelle `0x29`, also **Playback 10 → Output 2** (Analog 2),
  und hätte die Hypothese nicht geprüft. Nach Rückfrage gilt `0x4261`.
- **Wert `0x0000` = aus:** so in OSCMix (`setmixlevel`: Pegel 0 → 0). Das
  FireWire-Protokoll des 802 nennt dagegen `0x8000` = −65 dB. Über den
  eigenen Sendeweg ist `0x0000` **noch nicht bestätigt**; B.4 ist der erste
  Test dieses Werts. `0x9000` = 0 dB ist am Gerät bestätigt (B.3).
- **Nachricht:** Wort `0x42610000` (Adresse und Wert haben zusammen fünf
  gesetzte Bits, die Gesamtparität ist damit ungerade, Paritätsbit 0),
  SysEx

  ```text
  F0 00 20 0D 10 00 00 00 04 13 04 F7
  ```

  geprüft mit dem Projekt-Codec und einer unabhängigen Shell-Rechnung.
- **Programm:** [ff802_mixer_b4.c](../tools/hw-test/ff802_mixer_b4.c),
  Erkennung und Sendeweg wie B.3 (ALSA-Sequencer, Port „Port 2“ per
  Kartennummer, keine feste Client-Nummer).

## Sicherheitsbeschränkungen

- Genau **eine** fest eingetragene Nachricht `0x4261 = 0x0000`; keine
  Eingabe für Adresse oder Wert, keine Wiederholung.
- **Dry-Run ist der Standard.** Senden nur mit `--send` und nach Eingabe
  von `SEND` an einem Terminal. Vorher werden USB-Identität, CC-Modus,
  MIDI-Port, Register, Wert und die vollständige SysEx-Nachricht
  angezeigt; unmittelbar vor dem Senden werden Gerät und Port erneut
  geprüft.
- Nur das ursprüngliche Fireface 802 im CC-Modus (USB-ID, „RME“, Produkt,
  High Speed); bei mehreren Geräten keine stille Auswahl.
- Kein systemd, kein udev, keine Verbindung zur automatischen
  Mixer-Initialisierung; nie installiert, nicht im Paket. Gebaut nur mit der
  CMake-Option `FF802_BUILD_MIXER_B4` (Standard OFF).
- Andere Routings, Ausgangspegel, Mute, Pan und gespeicherte Setups werden
  nicht geschrieben.

## Wiederherstellungsproblematik

- Der aktuelle Wert von `0x4261` kann **nicht ausgelesen** werden.
- Der Schreibzugriff verändert möglicherweise den aktuellen Mixerzustand;
  die **exakte Wiederherstellung des vorherigen Werts ist nicht
  gewährleistet**.
- Ein späteres `0x4261 = 0x9000` wäre **keine Wiederherstellung**, sondern
  eine neue, definierte Verbindung Playback 2 → Phones 10 mit 0 dB.
- Das Programm enthält deshalb keine Restore-Funktion.
- Die Wirkung auf gekoppelte Stereokanäle ist nicht vollständig
  untersucht. B.3 zeigte für 9/10 bei einem Routing-Schreibzugriff keine
  Kopplung (`0x4228 = 0x9000` wirkte nur auf Phones 9); bei B.2 wurden mit
  einem Ausgangspegel (`0x2600`) subjektiv beide Kopfhörerkanäle leiser.

## Testablauf

Vorbereitung (nur lesend):

```sh
cd <Projektverzeichnis>    # der lokale Klon von fireface802-cc
sha256sum build-b4/ff802-mixer-b4
./build-b4/ff802-mixer-b4            # Dry-Run (Standard), sendet nichts
```

Hörtest (Schritte mit Schreibzugriff erst nach Freigabe):

1. Lautstärke niedrig, Kopfhörer an Phones 9/10.
2. **Vorher:** einkanalig nur rechts über die Senke „Analog Output 1/2“,
   ohne die Standard-Senke zu ändern:

   ```sh
   wpctl status                                  # ID der Senke "Analog Output 1/2"
   wpctl inspect <ID> | grep node.name
   PIPEWIRE_NODE=<node.name> speaker-test -D pipewire -c 2 -s 2 -t sine -f 440 -l 2
   ```

   Erwartet (Ausgangslage): Ton rechts auf Phones 9/10.
3. `./build-b4/ff802-mixer-b4 --send`, Anzeige prüfen, `SEND` eintippen.
4. **Nachher:** derselbe Befehl wie in Schritt 2.
5. Zur Kontrolle optional `-s 1` (links): weiterhin kein Ton auf Phones
   9/10.

## Erwartete Beobachtungen und Interpretation

| Beobachtung nach Schritt 3 | Deutung |
|---|---|
| Rechter Testton auf Phones 9/10 verschwunden | Hypothese bestätigt: Verbindung Playback 2 → Phones 10 bestand und ist aus; `0x0000` wirkt über den eigenen Sendeweg als „aus“ |
| Ton unverändert | entweder liegt die Ursache nicht in `0x4261` (anderes Register, z. B. ein Mixdown oder ein gekoppeltes Paar), oder `0x0000` wirkt nicht als „aus“; B.4 allein unterscheidet das nicht |
| Ton leiser, aber hörbar | `0x0000` ist möglicherweise nicht „aus“ (z. B. −65 dB nach FireWire-Lesart wäre `0x8000`), oder es gibt einen zweiten Weg |
| Ton verschwindet auch auf anderen Kanälen oder andere Änderungen | unerwartete Kopplung; Abbruch, keine weiteren Schreibzugriffe, Befund dokumentieren |

## Grenzen der Aussagekraft

- Der Hörtest prüft **ausschließlich**, ob bei Wiedergabe über „Analog
  Output 1/2“ der rechte Testton auf Phones 9/10 verschwindet.
- An Analog 1/2 ist derzeit kein Wiedergabegerät angeschlossen. Ob das
  Signal dort korrekt ausgegeben wird, lässt sich aus diesem Test **nicht**
  schließen.
- Der Test sagt nichts über andere Querverbindungen (z. B. Playback 1 →
  Output 9 oder Analog-Mixdowns) aus.
- Ohne Auslesen der Register ist der Zustand vor dem Test unbekannt; die
  Interpretation stützt sich allein auf das Gehör.
