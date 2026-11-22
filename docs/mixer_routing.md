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
| Hardware Output 9 | `0x2600` | `0x0000` | geschrieben und beobachtet; außerdem `0xFF38` = −20,0 dB, und über den ALSA-Sendeweg des Projekts geschrieben (B.2) |
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

**Stand 2026-11-22:** vollständig implementiert und in der Simulation
getestet; Beschreibung, Registerliste, Bedienung, Installation und
Einschränkungen in **[mixer_init.md](mixer_init.md)**. Das Paket
`ff802-cc-auto 0.2.0~dev2` (unveröffentlicht) enthält die Schreibfunktion
(`FF802_ENABLE_MIXER_WRITE=ON` nur im Paketbau); die automatische
Initialisierung ist nach der Installation aus. Am Gerät getestet ist der
Sendeweg mit einer und zwei Nachrichten (B.2, B.3), nicht die Folge von 24.
Der folgende Überblick ist der Stand der ersten Implementierung; wo er
abweicht (Abstände, Wartezeit), gilt mixer_init.md.

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
  übereinstimmen. Warten auf Karte und Port höchstens 15 s.
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

### Hardwaretest B.2 (2026-11-21): erfolgreich

Erster MIDI-Schreibzugriff über den ALSA-Sequencer: genau ein Register,
`0x2600` (Ausgangspegel Hardware Output 9 = Phones 9), mit dem Testprogramm
[ff802_mixer_b2.c](../tools/hw-test/ff802_mixer_b2.c) (CMake-Option
`FF802_BUILD_MIXER_B2`, nie installiert). Es enthält nur zwei Nachrichten:

| Schritt | Register = Wert | SysEx |
|---|---|---|
| Test | `0x2600 = 0xFF38` (−20,0 dB) | `F0 00 20 0D 10 00 38 7E 03 30 0A F7` |
| Wiederherstellung | `0x2600 = 0x0000` (0 dB) | `F0 00 20 0D 10 00 00 00 00 30 02 F7` |

Ergebnisse (vom Anwender gemeldet):

| Schritt | Ergebnis |
|---|---|
| B.2.0 Kodierung (OSCMix) | `0x2600` wirkt auf Hardware Output 9 (Phones 9); Pegel als vorzeichenbehaftete 16-Bit-Werte in 0,1 dB; `0xFF38` = −20,0 dB, `0x0000` = 0,0 dB |
| B.2.1 Dry-Run | `0424:3fdd`, CC-Modus, Karte 1, Port `20:1` automatisch gewählt, nichts gesendet |
| B.2.3 Test | Nachricht `0x2600 = 0xFF38` über `20:1` gesendet; Pegeländerung hörbar. **Nach subjektiver Wahrnehmung wurden beide Kanäle von Phones 9/10 leiser**, nicht nur der linke |
| B.2.4 Wiederherstellung | `0x2600 = 0x0000` gesendet; beide Kanäle wieder in ursprünglicher Lautstärke |

**Damit belegt:** Der Sendeweg des Projekts (ALSA-Sequencer, Verbindung
mit dem Kernel-Port „Port 2“, direkt adressiertes SysEx-Ereignis,
`snd-seq-midi`) erreicht das Gerät, und das Gerät übernimmt einen
Registerwert daraus. Die automatische Port- und Kartenerkennung arbeitet
am Gerät. Belegt ist das für **eine** Nachricht, nicht für eine Folge von
24.

**Nicht geklärt:** warum beide Kanäle leiser wurden. Phones 9/10 sind in
TotalMix sehr wahrscheinlich als Stereopaar gekoppelt. Möglich sind: (a)
das Gerät überträgt den Pegel von Output 9 bei gekoppeltem Paar auch auf
Output 10, (b) der Pegel von `0x2600` wirkt auf das ganze
Kopfhörerpaar, (c) eine Täuschung der Wahrnehmung (−20 dB auf einem Ohr
wirkt insgesamt leiser). Eine objektive Messung oder ein Auslesen der
Register fehlt. Ob die Kopplung auch die Routing-Register betrifft, ist
offen.

### Hardwaretest B.3 (2026-11-22): erfolgreich; Wiederherstellung vorbereitet

Ein einzelnes Routing: Playback 9 → Hardware Output 9, `0x4228 = 0x9000`
(0 dB), `F0 00 20 0D 10 00 00 20 22 11 0C F7`, mit
[ff802_mixer_b3.c](../tools/hw-test/ff802_mixer_b3.c) (CMake-Option
`FF802_BUILD_MIXER_B3`, nie installiert). Das Programm schreibt nur den
belegten Wert `0x9000`; „aus“ (`0x0000` laut OSCMix, `0x8000` = −65 dB laut
FireWire-Protokoll) ist nicht belegt.

Geplanter Ablauf: B.3.0 OSCMix setzt das Paar Playback 9/10 → Output 9/10
auf −∞; B.3.1 einkanalig links/rechts: beide stumm; B.3.2 Dry-Run; B.3.3
`--send`; B.3.4 links-Signal: nur links hörbar; B.3.5 rechts-Signal:
stumm; B.3.6 Betriebszustand wiederherstellen.

**Ergebnis (vom Anwender gemeldet):** `0x4228 = 0x9000` wurde über das
eigene ALSA-MIDI-Backend gesendet. Danach war Playback 9 ausschließlich auf
Hardware Output 9 hörbar; Playback 10 blieb vollständig stumm. Ein
einzelner Routing-Schreibzugriff wirkt also wie vorgesehen und erzeugt bei
diesem Paar keine zusätzlichen Signalwege: weder gespiegelt (Playback 10 →
Output 10) noch gekreuzt (Playback 9 → Output 10).

**B.3.6 mit OSCMix ist misslungen:** OSCMix konnte das normale
Stereo-Routing nicht wiederherstellen. OSCMix wird für die
Wiederherstellung nicht weiter verwendet. Nach Angabe des Anwenders hatte
OSCMix zuvor die gekreuzten Routings `0x4229` und `0x4268` auf `0x0000`
gesetzt; sie bleiben unverändert.

**Wiederherstellung mit dem eigenen Sendeweg (vorbereitet, nicht
freigegeben):** `ff802-mixer-b3 --restore-stereo` sendet genau zwei feste
Nachrichten in dieser Reihenfolge über eine Verbindung, 10 ms Abstand,
200 ms vor dem Trennen:

| Nr. | Register = Wert | Funktion | SysEx |
|---|---|---|---|
| 1 | `0x4228 = 0x9000` | Playback 9 → Hardware Output 9, 0 dB | `F0 00 20 0D 10 00 00 20 22 11 0C F7` |
| 2 | `0x4269 = 0x9000` | Playback 10 → Hardware Output 10, 0 dB | `F0 00 20 0D 10 00 00 20 26 13 0C F7` |

Gekreuzte Routings und Ausgangspegel werden nicht geschrieben.
`0x4269 = 0x9000` ist mit OSCMix am Gerät bestätigt (Tabelle oben); über
den eigenen Sendeweg wurde bisher nur `0x4228` geschrieben.

**Einkanalige Wiedergabe auf „Headphones Output 9/10“**, ohne die
Standard-Senke zu ändern (die Zielangabe gilt nur für den einen Prozess):

```sh
wpctl status                                   # ID der Senke "Headphones Output 9/10"
wpctl inspect <ID> | grep node.name            # Knotenname der Senke
PIPEWIRE_NODE=<node.name> speaker-test -D pipewire -c 2 -s 1 -t sine -f 440 -l 3   # nur links
PIPEWIRE_NODE=<node.name> speaker-test -D pipewire -c 2 -s 2 -t sine -f 440 -l 3   # nur rechts
```

`speaker-test` (alsa-utils) erzeugt einen Stereostrom, in dem nur der
gewählte Kanal ein Signal trägt. `PIPEWIRE_NODE` setzt das Ziel des
ALSA-Plugins `pipewire` für diesen Aufruf. Ersatzweise `-D pulse` mit
`PULSE_SINK=<node.name>`. Während der Wiedergabe zeigen `wpctl status`
oder `pw-top`, an welcher Senke der Strom hängt. Die Lautstärke wird mit
dem Kopfhörerregler am Gerät niedrig gehalten, nicht über die
gespeicherte Senkenlautstärke.

**Offen bis zum nächsten Hardwaretest:** Verhalten gekoppelter Paare bei
direkten Registerzugriffen (Ausgangspegel und Routings); eine Folge
mehrerer Nachrichten über denselben Weg; ob der Lautstärkeknopf am Gerät
auf die Ausgangsfader 9–12 wirkt; Wirkung auf Ausgang 3–8 (abgeleitet);
der Lauf in der gehärteten systemd-Unit.

### Hardwaretest B.4 (vorbereitet, nicht ausgeführt)

Beobachtung: Wiedergabe über „Analog Output 1/2“ ist auf Phones 9/10
hörbar, und zwar nur Playback 2 (rechts) auf Phones 10. Vermutet wird eine
Matrixverbindung Playback 2 → Hardware Output 10, Register `0x4261`. B.4
schaltet sie mit genau einer Nachricht `0x4261 = 0x0000` ab
(`F0 00 20 0D 10 00 00 00 04 13 04 F7`). Der vorherige Wert ist nicht
wiederherstellbar. Einzelheiten: [mixer_b4.md](mixer_b4.md).

## Weitere Quellen

- RME-Forum: Remote controlling Fireface 802 in class compliant mode?
  <https://forum.rme-audio.de/viewtopic.php?id=31390>
- [t4_disable.md](t4_disable.md): CC-Option in TotalMix FX und Register
  `0x3D44`
- [audio_t3.md](audio_t3.md): Lautstärken in PipeWire/WirePlumber
