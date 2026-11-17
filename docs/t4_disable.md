# T4: Untersuchung `ff802-cc disable` (CC-Modus → proprietärer Modus)

> **Ergebnis:** Für das originale Fireface 802 gibt es **kein belegtes
> Verfahren**, das Gerät per USB aus dem Class-Compliant-Modus zurück in
> den proprietären Modus zu schalten. `ff802-cc disable` sendet deshalb
> nichts. Es erklärt den Stand und die sicheren Alternativen und endet mit
> Exit 5 („nicht gesendet“). **Offener Punkt**, siehe unten
> „Was für eine Umsetzung nötig wäre“.

Stand: 2026-11-17. Untersucht im Dev-Container. Es gab keine USB-Zugriffe:
Die Deskriptoren stammen aus dem schreibgeschützt eingebundenen sysfs des
Hosts, die der Kernel bei der Enumeration zwischengespeichert hat.

## Befunde

### 1. RME-Handbuch (Fireface 802 User's Guide, Kapitel 31)

| Aussage (wörtlich) | Fundstelle |
|---|---|
| „Check the entry Activate CC mode on disconnect in TotalMix FX Options.“ | S. 91, 31. Operation |
| „As soon as the connection to the computer is interrupted, the Fireface 802 boots into Class Compliant mode.“ | S. 91 |
| „Upon reconnection to the computer the CC mode is automatically disabled.“ | S. 91 |
| „On reconnection to the computer the interface is detected despite being in CC mode, and TotalMix FX will immediately turn off the CC mode automatically.“ | S. 78, Options |
| „TotalMix FX resets the unit to normal mode whenever it detects CC mode.“ | S. 93, 31.2 |
| „Connect the ARC, push and hold keys Prog and A (Talkback), then turn on the 802. The unit boots into CC mode.“ | S. 91 |
| „Note: with the keys Prog and B (Mono) the 802 boots into normal mode.“ | S. 91 |
| „… can be activated and deactivated directly at the unit only via the optional Advanced Remote Control, ARC.“ | S. 90 |

Quelle: [manualslib, Fireface 802 User's Guide](https://www.manualslib.com/manual/745601/Rme-Audio-Fireface-802.html?page=91)
(S. 78, 90, 91, 93). Ob der CC-Zustand dauerhaft im Gerät gespeichert
wird, steht dort nicht.

Daraus folgt:

- **Register `0x3D44` entspricht sehr wahrscheinlich der TotalMix-Option
  „Activate CC mode on disconnect“.** Der Befehl `01 00 44 BD` setzt sie.
  Erst die Trennung der USB-Verbindung schaltet das Gerät um. Genau das
  wurde am Gerät beobachtet. Das erklärt auch, warum eine physische
  Trennung nötig ist und ein bloßer Software-Reset nicht reicht.
- **Der aufgezeichnete Befehl `00 00 44 3D` (Wert 0) gehört zum
  proprietären Protokoll** (Interface 1, Endpunkt `0x0C`). Wahrscheinlich
  entfernt er nur diese Option, solange das Gerät im proprietären Modus
  läuft. Getestet ist das nicht. Er ist kein Rückweg aus dem CC-Modus.
- **Den Rückweg übernimmt TotalMix FX auf Windows/macOS automatisch**,
  sobald es das Gerät im CC-Modus erkennt. Das entspricht dem bisherigen
  Wechsel zwischen Windows und Kubuntu. Ohne Computer geht es nur mit der
  optionalen ARC.

### 2. USB-Schnittstellen im CC-Modus (sysfs des Hosts, `0424:3fdd`)

| Interface | Klasse | Endpunkte | Treiber |
|---|---|---|---|
| 0 | Audio Control (UAC2) | – | snd-usb-audio |
| 1 | Audio Streaming | (Alternate Settings) | snd-usb-audio |
| 2 | Audio Streaming | (Alternate Settings) | snd-usb-audio |
| 3 | MIDI Streaming | Bulk OUT `0x07`, Bulk IN `0x86` | snd-usb-audio |
| 4 | **herstellerspezifisch (0xFF)** | Bulk OUT `0x09`, Bulk IN `0x88` | keiner |

Endpunkt `0x0C` gibt es im CC-Modus nicht. Über Interface 4 spricht
TotalMix FX das Gerät vermutlich an, wenn es den CC-Modus erkennt. Das
Protokoll auf Interface 4 ist aber **unbekannt**. Es gibt weder eine
Aufzeichnung noch eine Dokumentation.

### 3. Aufzeichnungen im Projekt

`tests/test_cc_mode.c` enthält beide Wörter aus Windows-Aufzeichnungen
(`01 00 44 BD`, `00 00 44 3D`). Die Aufzeichnungsdateien selbst und ihr
Kontext (Modus, Interface, Endpunkt, Anlass) liegen nicht im Repository.
Eine Aufzeichnung des Datenverkehrs **im CC-Modus** gibt es nicht.

### 4. Linux-Treiber und Open-Source-Projekte

- `snd-usb-audio` bedient im CC-Modus nur die UAC2- und MIDI-Interfaces
  und kennt keine Moduswechsel.
- Die ALSA-FireWire-Treiber (`snd-fireface`) betreffen die
  FireWire-Verbindung und nicht den USB-Modus.
- Laut RME-Forum (Mitarbeiter MC, 2020) stehen im CC-Modus die
  MIDI-Fernsteuerfunktionen nicht zur Verfügung: „The MIDI features
  mentioned in the manual for Stand-alone mode are not available in CC
  mode.“ ([Forum](https://forum.rme-audio.de/viewtopic.php?id=31390))
  Andere Projekte, etwa oscmix für das UCX II, betreffen andere Modelle.
  Ihre Übertragbarkeit auf das originale 802 ist nicht belegt.

## Antworten auf die Fragen aus der Aufgabenstellung

| # | Frage | Antwort |
|---|---|---|
| 1 | USB-gesteuerter Wechsel zurück möglich? | Ja, laut Handbuch durch TotalMix FX. Der Befehl ist aber unbekannt. |
| 2 | Dokumentierter Befehl? | Nein. |
| 3 | Informationen in Doku, Treibern, Projekten? | Nur das Verhalten (Handbuch), kein Protokoll. |
| 4 | Interfaces und Endpunkte im CC-Modus? | Siehe Tabelle. Herstellerspezifisch nur Interface 4 (`0x09`/`0x88`). |
| 5 | Wechsel über USB möglich? | Grundsätzlich ja (TotalMix FX tut es), mit dem vorhandenen Wissen nicht umsetzbar. |
| 6 | Physische Trennung nötig? | Unbekannt. Laut Handbuch schaltet TotalMix „immediately“ um. |
| 7 | Ohne Beeinträchtigung der Audiokonfiguration? | Nein. Der Wechsel beendet das UAC2-Gerät, also auch die T3-Sinks und laufende Wiedergabe. Eine Umsetzung müsste laufende Wiedergabe erkennen und verweigern. |

## Sichere Alternativen (heute)

1. **Windows oder macOS mit RME-Treiber und TotalMix FX:** Gerät
   anschließen. TotalMix setzt es automatisch in den Normalmodus zurück.
   Das ist der bisherige Weg.
2. **ARC:** Fireface ausschalten, Prog und B (Mono) halten, einschalten.
3. **Damit Linux nicht erneut umschaltet:** vorher `ff802-cc-auto off`.
   Das gilt dauerhaft, auch über Neustarts hinweg.

## Was für eine Umsetzung nötig wäre

1. Eine USB-Aufzeichnung unter Windows (USBPcap/Wireshark), während das
   Gerät im CC-Modus angeschlossen wird und TotalMix FX es zurücksetzt.
   Sie zeigt Interface, Endpunkt, Bytes und Ablauf.
2. Prüfen, ob der Ablauf ohne vorherige Initialisierung durch den Treiber
   funktioniert und ob danach eine Trennung nötig ist.
3. Erst dann eine Umsetzung mit denselben Schutzmaßnahmen wie bei
   `enable`:
   - Identität `0424:3fdd` mit „RME“ und „Fireface 802 (…)“
   - Layout-Prüfung von Interface 4
   - interaktive Bestätigung, keine Wiederholung
   - nur bei abgeschalteter Automatik
   - Verweigerung, solange ein PCM des Geräts läuft (`/proc/asound/cardN/pcm*/sub*/status`)
