# Interner Mixer und Routing im Class-Compliant-Modus

> **Stand 2026-11-21.** Dieses Dokument sammelt, was über den internen
> Mixer des ursprünglichen Fireface 802 im CC-Modus bekannt ist. Es trennt
> streng zwischen **bestätigten Beobachtungen** an unserem Gerät, den
> **Angaben im RME-Handbuch**, **Schlussfolgerungen** und **offenen
> Fragen**. Die veröffentlichten Versionen (bis 0.4.1) lesen und schreiben
> den Mixer **nicht**. Eine optionale Mixer-Initialisierung ist in
> Entwicklung; sie ist im Code enthalten, sendet aber noch nichts (siehe
> [Mixer-Initialisierung im CC-Modus](#mixer-initialisierung-im-cc-modus)).

## Worum es geht

Unter Linux kommt der Ton über USB als einer von 22 Wiedergabekanälen im
Fireface an. Welche Buchse ihn ausgibt, entscheidet der interne digitale
Mixer (DSP) des Geräts, nicht Linux:

```text
PipeWire/ALSA ──► USB-Kanal ──► Software Playback n ──► Fader ──► Hardware Output m ──► Buchse
                  (UCM-Profil)   (Eingang des Mixers)     (Routing im Gerät)
```

In TotalMix FX heißt ein Wiedergabekanal vom Computer **Software
Playback**. Für jede Kombination aus Software Playback und Hardware
Output gibt es einen eigenen Fader (Pegelregler). Steht dieser Fader auf
null, kommt vom betreffenden Software Playback an diesem Ausgang nichts
an. Steht der Fader eines anderen Ausgangs offen, kommt der Ton dort an.
Zusätzlich hat jeder Hardware Output einen eigenen Ausgangsfader.

Die Kanalzuordnung des UCM-Profils (z. B. „Headphones Output 9/10“ =
USB-Kanäle 9 und 10, intern 8/9) legt also nur fest, auf welchem
**Software Playback** der Ton ankommt. Ob er an Phones 9/10 hörbar ist,
hängt vom Mixer ab.

## Bestätigte Beobachtungen (unser Gerät, Kubuntu 26.04.1)

Gemeldet vom Anwender, aus den Hardwaretests vom 2026-11-17 bis
2026-11-19:

1. Unter Windows wurde das Routing in TotalMix FX geprüft: Software
   Playback 9/10 war auf Hardware Output Phones 9/10 geroutet, nicht auf
   Analog 1/2.
2. Unter Kubuntu wurde die Wiedergabe nachweislich auf Software Playback
   9/10 übertragen (Senke „Headphones Output 9/10“).
3. Trotzdem war die Musik zunächst über Analog 1/2 hörbar, nicht über
   Phones 9/10.
4. Mit dem Fremdwerkzeug **oscmix** ließen sich unter Kubuntu die
   Routing- und Ausgangsfader so einstellen, dass Phones 9/10
   funktionierte.
5. Nach einem Neustart von oscmix blieb die Kopfhörerwiedergabe erhalten;
   das Gerät hatte die Einstellungen also weiter aktiv. oscmix zeigte
   danach teilweise nicht die tatsächlich wirksamen Faderstellungen an.
6. Am Vortag hatte die Kopfhörerwiedergabe unter Kubuntu bereits
   funktioniert, nachdem die **Software-Playback-Fader in TotalMix FX**
   korrigiert worden waren. Dazwischen lief das Gerät wieder unter
   Windows.
7. In **T3** wurden Analog 3/4, Phones 9/10 und Phones 11/12 unter
   Kubuntu mit realer Wiedergabe über PipeWire erfolgreich gehört; die
   zuvor stummen Kopfhörerausgänge nach der Korrektur der
   Software-Playback-Fader in TotalMix FX. Das ist ein Hörtest, keine
   Messung des Kanalindex.
8. In der Messung **T2** (2026-11-17, Kabelschleife auf Line In 1) kam
   Wiedergabe-Index 0 an
   Analog Out 1 und Index 1 an Analog Out 2 an. An Out 3, 5 und 7 wurde
   kein Signal erkannt, Out 4 wurde abgebrochen
   ([audio_t2_results/](audio_t2_results/README.md)). Der Zustand des
   internen Mixers wurde dabei nicht erfasst.

## Angaben im RME-Handbuch (Fireface 802 User's Guide)

| Aussage (wörtlich) | Fundstelle |
|---|---|
| CC-Modus: „no hardware settings, no (Total) Mix and no settings for the effects“ | S. 90, Kap. 29 |
| „Audio playback in iTunes will automatically be performed by the 802, using analog outputs 1/2.“ | S. 91, Kap. 31 |
| „Playback will use the rear-side analog outputs 1 and 2, or even more channels if the app supports such operation.“ | S. 93, Kap. 32 |
| „Configuring the 802 is ideally done via the app TotalMix FX for iPad, otherwise by loading Setups.“ | S. 93 |
| „Store current state into device. The unit can store up to 6 setups (the current state) in its own memory.“ | S. 78 |
| „This function has been developed especially for the stand-alone mode operation, where there are no playback channels.“ „…using it under Windows, Mac or iOS will result in incomplete setups.“ | S. 78 |
| „Activate CC mode on disconnect … will boot into Class Compliant mode as soon as the connection to the computer is interrupted“ | S. 78 |
| „TotalMix FX resets the unit to normal mode whenever it detects CC mode.“ | S. 93 |
| Total Reset: „Playback routing 1:1 with mixdown to Main Out.“ | S. 77 |

Quelle: RME Fireface 802 User's Guide, z. B.
<https://www.manualslib.com/manual/745601/Rme-Audio-Fireface-802.html?page=78>.

**Nicht im Handbuch beschrieben:**
- ob das Gerät seinen Mixerzustand selbständig speichert,
- welcher Mixerzustand beim Start in den CC-Modus aktiv wird,
- ob TotalMix FX beim Verbinden seinen eigenen Zustand ins Gerät schreibt,
- wie die 22 CC-Wiedergabekanäle auf die Software-Playback-Kanäle
  abgebildet werden.

## Technische Schlussfolgerungen

**Belegt durch Beobachtung und Quellcode:**
- Die Ursache der stummen Kopfhörer lag im Mixer des Geräts, nicht in
  ALSA oder PipeWire: Der Ton kam auf Software Playback 9/10 an
  (Beobachtung 2), und die Ausgabe änderte sich allein durch Fader im
  Gerät (Beobachtungen 4 und 6).
- Keine veröffentlichte Komponente dieses Projekts (bis 0.4.1) schreibt
  Mixer-Werte: Das UCM-Profil enthält keine Mixer-Befehle (kein `cset`,
  keine `BootSequence`/`EnableSequence`), WirePlumber regelt nur Software-Lautstärken,
  `ff802-cc-auto` und `ff802-cc enable` senden nur den Befehl für
  Register `0x3D44`, `ff802-chanmap` liest den Mixer nur. Die neue
  Mixer-Initialisierung ist ohne `FF802_ENABLE_MIXER_WRITE` gebaut und
  sendet ebenfalls nichts.
- Der Host-Test `ff802-t3-host.sh verify` prüft nur die PipeWire/ALSA-
  Ebene. Ein bestandener `verify` sagt nichts über das Routing im Gerät.

**Plausibel, nicht belegt:**
- Im CC-Modus ist nicht immer der Mixerzustand aktiv, den TotalMix FX
  zuletzt angezeigt hat. Beobachtung 3 passt zu einem Zustand wie „Total
  Reset“ (Wiedergabe 1:1 und zusätzlich Mixdown auf Main Out = Analog
  1/2).
- Welcher Zustand im CC-Modus aktiv ist, könnte davon abhängen, **wie**
  das Gerät in den CC-Modus kam: aus einer laufenden TotalMix-Sitzung
  („Activate CC mode on disconnect“) oder nach dem Einschalten ohne
  TotalMix, umgeschaltet mit `ff802-cc enable` bzw. `ff802-cc-auto`.
  Das würde erklären, warum es an einem Tag funktionierte und nach dem
  Windows-Betrieb zunächst nicht (Beobachtung 6).
- „Store current state into device“ ist für den Stand-alone-Betrieb ohne
  Wiedergabekanäle gedacht; das Routing der Software-Playback-Kanäle für
  den CC-Modus ist darin laut Handbuch nicht verlässlich enthalten.
- oscmix zeigt nach einem Neustart vermutlich eigene Startwerte statt
  des Gerätezustands (Beobachtung 5).

**Ausgeschlossen:** eine falsche Kanalzuordnung in ALSA/PipeWire als
Ursache (widerspricht Beobachtung 2).

## Offene Fragen

1. Welcher Mixerzustand ist nach dem Wechsel in den CC-Modus aktiv, und
   hängt er vom Weg in den CC-Modus oder vom Aus- und Einschalten ab?
   Vorgeschlagener Test (noch nicht ausgeführt bzw. nicht dokumentiert):
   - **Lauf A:** `ff802-cc-auto off`; unter Windows in TotalMix FX
     „Activate CC mode on disconnect“ setzen, USB trennen, Gerät **nicht**
     ausschalten; unter Kubuntu anschließen und ein leises Testsignal
     über „Headphones Output 9/10“ abspielen.
   - **Lauf B:** gleicher TotalMix-Zustand, Gerät aus- und einschalten,
     unter Kubuntu im proprietären Modus anschließen und mit
     `ff802-cc enable` oder der Automatik umschalten; gleiches
     Testsignal.
   - Auswertung: A richtig und B falsch → der Weg entscheidet; beide
     falsch → der CC-Modus startet mit eigenem Zustand.
2. Übersteht ein im CC-Modus gesetzter Mixerzustand (z. B. mit oscmix)
   das Aus- und Einschalten des Geräts?
3. Wie werden die 22 CC-Kanäle auf die Software-Playback-Kanäle
   abgebildet? Gemessen (T2) ist nur Analog 1/2. Für 3/4, 9/10 und 11/12
   passt die Zuordnung des Profils zum Hörtest in T3; für die übrigen
   Paare gibt es weder Messung noch Hörtest
   ([audio_t2_results/](audio_t2_results/README.md)). Ob die fehlenden
   Signale in T2 am Mixer-Routing lagen, ist offen.
4. Lässt sich das Routing zuverlässig und dauerhaft von Linux aus
   einrichten? Mit OSCMix ja (Beobachtungen 4 und 5). Eine eigene,
   optionale Mixer-Initialisierung ist in Entwicklung und sendet noch
   nichts (siehe unten).

## Werkzeuge zum Einstellen des Mixers

- **TotalMix FX** (RME) unter Windows oder macOS, im proprietären Modus.
  Laut Handbuch ist für den CC-Modus TotalMix FX für iPad vorgesehen.
- **oscmix**: Fremdprojekt, nicht Teil dieses Projekts. Die Unterstützung
  des Fireface 802 gibt es nur in einem inoffiziellen Fork
  (<https://github.com/huddx01/oscmix>), der selbst auf weiteren
  Testbedarf hinweist. Bei uns hat es für Phones 9/10 funktioniert
  (Beobachtung 4); eine Zusage für andere Einstellungen gibt es nicht.

## Register, mit OSCMix am Gerät bestätigt (2026-11-21)

Gemessen vom Anwender mit OSCMix (Debug-Ausgabe der geschriebenen und
empfangenen Register) im CC-Modus. Formeln für Kanal k = 1…12 (1–8 Analog,
9–12 Phones):

- Playback k → Hardware Output k: `0x4000 | ((k−1) << 6) | (0x20 + k − 1)`,
  0 dB = `0x9000`
- Ausgangsfader Hardware Output k: `0x1E00 + ((k−1) << 8)`, 0 dB = `0x0000`

| Funktion | Register | 0 dB | Nachweis |
|---|---|---|---|
| Playback 1 → Output 1 | `0x4020` | `0x9000` | geschrieben und beobachtet |
| Playback 2 → Output 2 | `0x4061` | `0x9000` | geschrieben und beobachtet |
| Playback 9 → Output 9 | `0x4228` | `0x9000` | geschrieben und beobachtet |
| Playback 10 → Output 10 | `0x4269` | `0x9000` | geschrieben und beobachtet |
| Playback 11 → Output 11 | `0x42AA` | `0x9000` | geschrieben und beobachtet |
| Playback 12 → Output 12 | `0x42EB` | `0x9000` | geschrieben und beobachtet |
| Hardware Output 1 | `0x1E00` | `0x0000` | geschrieben und beobachtet |
| Hardware Output 9 | `0x2600` | `0x0000` | geschrieben und beobachtet |
| Hardware Output 10 | `0x2700` | `0x0000` | geschrieben und beobachtet |
| Hardware Output 11 | `0x2800` | `0x0000` | geschrieben und beobachtet |
| Hardware Output 12 | `0x2900` | `0x0000` | nur in Registermeldungen beobachtet |

Playback 3–8 → Output 3–8 und die Ausgangsfader 2–8 sind aus den Formeln
abgeleitet. OSCMix schreibt beim Verändern eines Stereo-Playback-Faders
teilweise zusätzlich die gekreuzten Routings (z. B. Playback 9 → Output 10)
mit dem Wert null; die Mixer-Initialisierung übernimmt das **nicht**.

Die Kommunikation über den ALSA-Sequencer-Port „Port 2“ wurde mit OSCMix
(`alsaseqio`) erfolgreich getestet. Phase 2a (passives Mitlesen) zeigte
den Port als Kernel-Client 20 mit `card=1`, Port 1 „Fireface 802 (24047128)
Port 2“.

## Mixer-Initialisierung im CC-Modus

**Stand:** implementiert und automatisiert getestet (Simulation), **ohne
Schreibzugriffe**: Die Build-Option `FF802_ENABLE_MIXER_WRITE` ist `OFF`,
auch im Paket `ff802-cc-auto 0.2.0~dev1` (unveröffentlicht). Ein solcher
Build findet und meldet den MIDI-Port, sendet aber nichts
(Ergebnis `disabled-build`). Ein Hardwaretest ist noch nicht freigegeben.

- **Einschalten:** `ff802-cc-auto mixer-init on|off|status`, Standard aus,
  unabhängig von der automatischen Umschaltung
  (`/var/lib/ff802-cc-auto/mixer-init`).
- **Wann:** im bestehenden Dienst `ff802-cc-auto@.service`, im Zweig für
  ein Gerät im CC-Modus, bei **jedem** udev-„add“ des Geräts (Umstecken,
  Aus/Ein, Boot, nach `ff802-cc enable`). Es gibt dafür bewusst keinen
  Eintrag „schon erledigt“ je Verbindung: Die Werte sind absolut, eine
  Wiederholung schreibt dieselben Werte, und eine wiederverwendete
  USB-Gerätenummer kann eine nötige Initialisierung nie unterdrücken.
- **Was:** genau 24 Register in fester Reihenfolge, erst Playback 1…12 →
  Output 1…12, dann die Ausgangsfader 1…12, jeweils 0 dB
  ([mixer_init.h](../include/ff802/mixer_init.h)). Nichts anderes:
  keine gekreuzten Routings, Eingänge, Mute, Pan, weitere Ausgänge oder
  gespeicherten Setups. Die Liste ist eine Konstante mit Prüfsumme.
- **Wie:** RME-SysEx `F0 00 20 0D 10 00 <5 Septette> F7` über den
  ALSA-Sequencer. Der Port wird ohne feste Nummern gefunden: Soundkarte
  der USB-Verbindung aus sysfs, Kernel-Client mit dieser Kartennummer,
  dessen Port 1 mit einem Namen auf „Port 2“; Index und Name müssen
  übereinstimmen. Warten auf Karte und Port höchstens 10 s.
- **Fehler:** keine Wiederholung, kein Zurücksetzen (es wird nichts
  gelesen). Ergebnis in `/run/ff802-cc-auto/mixer-result`, eine
  Journalzeile je Register, bei Fehlern eine eigene Desktop-Benachrichtigung
  „Mixer-Initialisierung fehlgeschlagen“, getrennt von „Class-Compliant-Modus
  aktiv“.
- **Gefahr:** 0 dB an Phones 9–12 kann sehr laut sein; manuelle
  Änderungen an diesen 24 Werten werden bei jeder Verbindung
  überschrieben.

### Hardware-Trockenlauf B.1 (2026-11-21): erfolgreich

Nur lesend, ohne MIDI- oder USB-Schreibzugriff, ohne Änderung am Host.
Ausgeführt wurde der implementierte Erkennungscode
(`ff802_sysfs_identify`, `ff802_mixer_find_card`, ALSA-Portliste,
`ff802_mixer_select_port`) in einem Prüfprogramm ohne
`FF802_ENABLE_MIXER_WRITE` (laut `nm` ohne `snd_seq_connect_to`,
`snd_seq_create_simple_port`, `snd_seq_event_output_direct`), als
normaler Benutzer am Host.

| Merkmal | Ergebnis |
|---|---|
| USB-Verbindung | `3-2.4.1.1.3` (Bus 3, Gerätenummer 53), `0424:3fdd`, RME, „Fireface 802 (24047128)“, 480 Mbit/s |
| Identität | Fireface 802, class-compliant mode |
| Soundkarte (sysfs) | `card1` unter Schnittstelle `:1.0` (Audio Control), mit `midiC1D0` und `seq-midi-1-0`; **nicht** unter der MIDI-Schnittstelle `:1.3` |
| Sequencer | Kernel-Client 20, Karte 1 |
| Gewählter Port | `20:1` „Fireface 802 (24047128) Port 2“ |
| Mixer-Schreibzugriffe | im Prüfprogramm nicht einkompiliert |

Die implementierte Erkennung stimmt damit mit dem Gerät überein. Nicht
abgedeckt: der Lauf in der gehärteten systemd-Unit (`DeviceAllow`,
`ProtectKernelModules`) und das Zeitverhalten beim Anstecken.

**Registermeldungen des Geräts (Auswertung der Phase-2a-Mitschnitte):** Auf
„Port 2“ sendet das Gerät ohne Anforderung nur RME-SysEx mit dem Register
`0x3F00 = 0x0908` (3300 Wörter im Ruhezustand, 5207 beim Drehen eines
Knopfs, alle mit gültiger Parität). Das Drehen des Knopfs erzeugte
**keine** weitere Registermeldung. Eine Prüfung von Schreibzugriffen über
den MIDI-Eingang ist ohne Refresh-Anforderung daher nicht möglich.

**Offen bis zum Hardwaretest:** ob der Kernel-Client `snd-seq-midi`
direkt adressierte SysEx-Ereignisse nach dem Verbinden vollständig an das
Gerät weitergibt (bei OSCMix mit `alsaseqio` belegt, für dieses Programm
nicht); ob der Lautstärkeknopf am Gerät auf die Ausgangsfader 9–12 wirkt;
Wirkung auf Ausgang 3–8 (abgeleitet).

## Weitere Quellen

- RME-Forum: Remote controlling Fireface 802 in class compliant mode?
  <https://forum.rme-audio.de/viewtopic.php?id=31390>
- [t4_disable.md](t4_disable.md): CC-Option in TotalMix FX und Register
  `0x3D44`
- [audio_t3.md](audio_t3.md): Lautstärken in PipeWire/WirePlumber
