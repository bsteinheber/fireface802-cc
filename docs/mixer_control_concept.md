# Konzept: Steuerung des internen Mixers des Fireface 802 unter Linux

> **Stand 2026-11-20, Konzept – keine Implementierung.** Es gab keine
> USB- oder MIDI-Zugriffe auf das Gerät und keine Änderung an
> Programmdateien. Externe Projekte wurden nur lesend über ihre
> öffentlichen Quelltexte geprüft (kein Klon, keine Installation).
> Betrifft ausschließlich das **ursprüngliche Fireface 802** (USB
> `2a39:3fcd` / CC `0424:3fdd`), nicht das 802 FS.

Kennzeichnung jeder Aussage:

| Status | Bedeutung |
|---|---|
| **VERIFIED** | an unserem Gerät gemessen oder durch nachvollziehbaren Test belegt |
| **OBSERVED** | an unserem Gerät beobachtet, aber nicht vollständig erklärt |
| **HYPOTHESIS** | technisch begründete Annahme, Quelle angegeben, an unserem Gerät nicht geprüft |
| **UNKNOWN** | ungeklärt |

---

## 1. Zielsetzung und Abgrenzung

**Ziel:** den **internen DSP-Mixer** des Fireface 802 im CC-Modus von
Linux aus lesen und gezielt verändern: Routing und Pegel der
Software-Playback-Kanäle auf Hardware-Ausgänge, Eingangs-Routing,
Ausgangspegel, Mute, später Speichern und Wiederherstellen.

**Erstes praktisches Ziel:** ein einzelnes Kanalpaar,
`Software Playback 9/10 → Hardware Output Phones 9/10`, im Gerät setzen
und seinen Pegel einstellen.

**Nicht gemeint:** Lautstärke, Routing oder Kanalzuordnung in PipeWire,
ALSA oder einer Software wie OSCMix. Diese Ebenen liegen vor dem Gerät
und ändern die Matrix im Fireface nicht (siehe
[mixer_routing.md](mixer_routing.md)).

## 2. Aktueller Entwicklungsstand

- Pakete: `ff802-cc` 0.4.1, `ff802-cc-auto` 0.1.2, `ff802-ucm-profile`
  0.3.1 (Release `v0.4.1`).
- UCM-Profil (Kartenprofil `HiFi`): sechs Stereo-Ausgänge, ein Eingang;
  im Kartenprofil `Pro Audio` ein generisches 22-Kanal-Gerät.
- Kein Teil des Projekts liest oder schreibt den internen Mixer
  ([mixer_routing.md](mixer_routing.md), Abschnitt „Schlussfolgerungen“).
- Vorhandene Bausteine, die wiederverwendbar sind:
  - Registercodec `ff802_reg_encode()`/`ff802_reg_decode()`
    ([src/reg_codec.c](../src/reg_codec.c), [protocol.md](protocol.md)).
  - SysEx-Erzeugung `ff802_cc_disable_midi()` (Register als RME-SysEx,
    [cc_disable_port_reset.md](cc_disable_port_reset.md)).
  - Messwerkzeug `ff802-chanmap` (Kabelschleife, Pegelgrenze −40 dBFS,
    [audio_t2_plan.md](audio_t2_plan.md)) für die Wirkungsmessung.

## 3. Gesicherte Erkenntnisse

| # | Erkenntnis | Status | Beleg |
|---|---|---|---|
| E1 | Registerwort: Bit 31 ungerade Parität, Bits 30–16 Adresse (15 Bit), Bits 15–0 Wert | VERIFIED | Windows-Aufzeichnungen (`01 00 44 BD`), [protocol.md](protocol.md), `test_cc_mode` |
| E2 | Im CC-Modus nimmt das Gerät Registerwörter als RME-SysEx `F0 00 20 0D 10 00 <5 Septette> F7` auf USB-MIDI-Interface 3, EP `0x07`, Kabel 1 an | VERIFIED | Disable-Test 2026-11-19: Register `0x3D44` = `0xFFFE`, Gerät wechselte sofort den Modus ([cc_disable_port_reset.md](cc_disable_port_reset.md)) |
| E3 | Die SysEx-Kodierung von OSCMix (`setreg()`, `base128enc()`) ergibt für `0x3D44`/`0xFFFE` bytegleich unseren getesteten Befehl (`7E 7F 13 6A 0B`) | VERIFIED (rechnerisch, mit unserem Codec nachvollzogen) | Abschnitt 6.3 |
| E4 | CC-Modus: 5 Interfaces; IF 3 MIDI Streaming mit zwei Ports („Port 1“, „Port 2“, Embedded-Jacks 1/5 an EP `0x07`, 3/7 an EP `0x86`); IF 4 herstellerspezifisch, Bulk `0x09` OUT / `0x88` IN, ohne Treiber | VERIFIED (Host-sysfs für IF 3/4) / OBSERVED (Jack-Namen aus OSCMix-Deskriptor `doc/ff802-cc-desc.txt`, fremdes Gerät gleichen Typs) | [t4_disable.md](t4_disable.md), Abschnitt 5.3 |
| E5 | Kabel 1 des MIDI-Interface entspricht „Port 2“ (zweiter Embedded-Jack) | HYPOTHESIS (Deskriptor-Reihenfolge) | E4; OSCMix-README: „use the last port“ |
| E6 | ALSA zeigt für die Karte nur `'Playback Channel Map'` und `'PCM Playback Volume'` (Wert 60), keine Routing-Controls | OBSERVED | T2-Protokolle, Abschnitt „mixer state before“ ([audio_t2_results/](audio_t2_results/README.md)) |
| E7 | USB-Wiedergabekanal 1/2 (Index 0/1) kommt an Analog Out 1/2 an | VERIFIED | T2 ([audio_t2_results/](audio_t2_results/README.md)) |
| E8 | USB-Kanäle 9/10 (Index 8/9) erreichen Phones 9/10 erst, nachdem die Fader „Software Playback 9/10 → Phones 9/10“ im Gerät gesetzt waren | OBSERVED | T3 und [mixer_routing.md](mixer_routing.md), Beobachtungen 2–7 |
| E9 | Der Mixer-Zustand im CC-Modus entsprach nach Windows-Betrieb nicht immer dem zuletzt in TotalMix FX angezeigten | OBSERVED | [mixer_routing.md](mixer_routing.md), Beobachtungen 1, 3, 6 |
| E10 | Der interne Mixer-Zustand wurde bei T2 **nicht** erfasst; fehlende Signale an Out 3–7 sind nicht erklärt | VERIFIED (Lücke) | [audio_t2_results/](audio_t2_results/README.md) |
| E11 | Mit oscmix (Fork) ließen sich unter Linux Fader so setzen, dass Phones 9/10 hörbar wurde; nach Neustart zeigte oscmix teils nicht die wirksamen Werte | OBSERVED | [mixer_routing.md](mixer_routing.md), Beobachtungen 4, 5 |

## 4. Architektur des internen Fireface-Mixers

Quellen: RME-Handbuch ([mixer_routing.md](mixer_routing.md)), der
Gerätetreiber `device_ff802.c` im OSCMix-Fork (Abschnitt 6) und die
FireWire-Implementierung des 802 in snd-firewire-ctl-services
(Abschnitt 7.2). **Keine dieser Quellen ist für unser Gerät im CC-Modus
geprüft.**

### 4.1 Kanäle

| Größe | Wert | Status | Quelle |
|---|---|---|---|
| Hardware-Eingänge | 30: Analog 1–8, Mic/Inst 9–12, AES L/R, ADAT 1–16 | HYPOTHESIS | `device_ff802.c` (30 Einträge, `_Static_assert`), `ff802.rs` (8+4+2+16) |
| Hardware-Ausgänge | 30: Analog 1–8, Phones 9–12, AES L/R, ADAT 1–16 | HYPOTHESIS | wie oben |
| Software-Playback-Kanäle im Mixer | 30 | HYPOTHESIS | `ff802.rs` `STREAM_INPUT_COUNT = 30` |
| USB-Wiedergabekanäle im CC-Modus | 22 (48 kHz) | VERIFIED | T2: `opened … 22 ch`; UCM-Profil |
| Zuordnung USB-Kanal → Software Playback | Index 0/1 → SW PB 1/2 (E7), Index 8/9 → SW PB 9/10 (E8); übrige unbekannt | teils VERIFIED/OBSERVED, sonst UNKNOWN | T2, T3 |

### 4.2 Mixer-Matrix

- **Aufbau:** ein Submix je Hardware-Ausgang. Quellen sind die
  Hardware-Eingänge und die Software-Playback-Kanäle; jede Verbindung hat
  einen eigenen Pegel. **HYPOTHESIS** (`ff802.rs` serialisiert 30 Mixer
  × 62 Quellen; `device_ff802.c` adressiert `out × idx`).
- **Adresse** (beide Quellen übereinstimmend, Abschnitt 6.4):

  ```
  reg = 0x4000 | out << 6 | src
  src = 0x00 … 0x1D   Hardware-Eingang 1 … 30
  src = 0x20 + k      Software Playback k+1 (k = 0 … 29)
  ```

  Beispiel: `SW Playback 9 → Phones 9` = `0x4000 | 8<<6 | 0x28` =
  **`0x4228`**, `SW Playback 10 → Phones 10` = **`0x4269`**. **HYPOTHESIS**.
- **Pegelwert** (OSCMix `setmixlevel()`, Format „UCX II / FF802“):
  lineare Amplitude `a`; `v = round(a·0x8000)`; ist `v > 0x4000`, dann
  `v = (v>>3) − 0x8000` (16 Bit). Ergebnis: aus `0x0000`, −6 dB
  `0x4000`, **0 dB `0x9000`**, +6 dB `0xA000`. Das FireWire-Protokoll
  nennt ebenfalls 0 dB = `0x9000` und +6 dB = `0xA000`, aber −65 dB =
  `0x8000`. **HYPOTHESIS**; unterhalb 0 dB widersprechen sich die
  Quellen → **UNKNOWN**.
- **Pan:** kein eigenes Register für das 802; Pan entsteht durch
  getrennte Pegel auf linkem und rechtem Ausgang
  (`DEVICE_MIX_VOLONLY`). **HYPOTHESIS**.
- **Mute je Verbindung:** kein eigener Schalter bekannt; „aus“ = Pegel
  `0x0000` (OSCMix). **HYPOTHESIS**.
- **Rolle der TotalMix-Fader:** Die Fader der Zeile „Software Playback“
  in TotalMix FX sind diese Matrix-Pegel für den jeweils gewählten
  Ausgang. **HYPOTHESIS**, gestützt durch E8.

### 4.3 Hardware-Eingänge

- Eingangs-Routing auf Ausgänge: dieselbe Matrix, `src` 0x00–0x1D,
  unabhängig von den Playback-Quellen. **HYPOTHESIS**.
- Kanalparameter je Eingang `i`: `reg = i<<8 | p` mit p = 0x00 Mute,
  0x01 FX-Send, 0x02 Stereo, 0x03 Record, 0x04 Playchan, 0x05 M/S,
  0x06 Phase, 0x07 Gain (Analog 1–8), 0x08 Ref-Level bzw. 48 V
  (Mic 9–12), 0x09 Hi-Z. **HYPOTHESIS** (`device_ff802.c`; Gain 0x07 und
  Ref-Level 0x08 auch im FireWire-Code).
- **Vorsicht:** 48 V (Phantomspeisung) liegt auf derselben Unteradresse
  wie der Ref-Level → ein falsch adressierter Schreibbefehl kann
  Phantomspeisung einschalten.

### 4.4 Hardware-Ausgänge

- Kanalparameter je Ausgang `o`: `reg = 0x1E00 + (o<<8) + p` mit
  p = 0x00 **Volume** (dB×10, −650 … +60), 0x01 Pan/Balance, 0x02
  **Mute**, 0x03 FX-Return, 0x04 Stereo, 0x05 Record, 0x06 Playchan,
  0x07 Phase, 0x08 Ref-Level. **HYPOTHESIS** (`device_ff802.c`; Volume
  0x00 mit −650 … 60 auch im FireWire-Code: `(30+o)<<8 | 0` = dieselbe
  Adresse, z. B. Phones 9 = **`0x2600`**).
- Kopfhörer: Phones 9–12 sind gewöhnliche Ausgänge des Mixers; der
  analoge Regler am Gerät wirkt zusätzlich. **HYPOTHESIS**.
- Echo der Matrix: Register `0x1E00 + (o<<8) + 0xE0 + i` meldet den
  Beitrag von **Hardware-Eingang** `i` zum Ausgang `o` (12-Bit, dB×10).
  Für Software-Playback-Quellen gibt es im OSCMix-Code **kein** Echo.
  **HYPOTHESIS** / für Playback **UNKNOWN**.
- Was davon im CC-Modus wirkt: Handbuch S. 90 „no hardware settings, no
  (Total) Mix“ bezieht sich auf die Bedienung vom Computer im
  Treibermodus; S. 93 empfiehlt für den CC-Modus „TotalMix FX for iPad“.
  OSCMix steuert das 802 angeblich im CC-Modus. → Steuerbarkeit im
  CC-Modus **HYPOTHESIS**, für Register `0x3D44` **VERIFIED** (E2).

### 4.5 Speicherung des Mixer-Zustands

| Frage | Status | Stand |
|---|---|---|
| Wo liegt der aktuelle Zustand? | UNKNOWN | Im DSP wirksam (E8, E11); ob flüchtig oder dauerhaft, ist offen |
| Bleibt er Windows ↔ Linux erhalten? | OBSERVED: nicht immer (E9) | Ursache offen ([mixer_routing.md](mixer_routing.md), Hypothese „Einstiegsweg“) |
| Bleibt er proprietär ↔ CC erhalten? | UNKNOWN | Handbuch: Gerät „bootet“ in den CC-Modus |
| Flüchtig vs. gespeichert | HYPOTHESIS | 6 Setups über „Store current state into device“, für Stand-alone ohne Playback gedacht (Handbuch S. 78); OSCMix kennt `SETUP_STORE` = `0x3F97` |
| Zustand auslesbar? | HYPOTHESIS | OSCMix: Schreiben von `0x3F99` = `0x0812` löst angeblich einen Register-Dump aus (Abschnitt 6.5) |

## 5. Verfügbare Steuerungsschnittstellen

### 5.1 ALSA-Mixer-Controls

- Vorhanden: `'Playback Channel Map'`, `'PCM Playback Volume'` (E6). Das
  sind Controls des UAC2-Standards (Feature Unit) bzw. von
  `snd-usb-audio`; ob `PCM Playback Volume` im Gerät wirkt, ist
  **UNKNOWN**. Routing- oder Matrix-Controls gibt es **nicht**.
- Der Kernel hat für das 802 keine Sonderbehandlung; RME-Quirks gibt es
  für ADI-2, Babyface Pro FS und Digiface USB (Abschnitt 7.1).
  **HYPOTHESIS** (Kernel-Quelle nur teilweise wörtlich gelesen).
- Fehlt: alles, was in 4.2–4.4 steht.

### 5.2 USB-Control-Transfers (EP 0)

- Für das 802 ist kein herstellerspezifischer Control-Request bekannt.
  **UNKNOWN**.
- Der Kernel steuert den Babyface Pro FS über Vendor-Requests an EP 0
  (bRequest `0x12` Mixer, `0x1a` Gain, `0x10`/`0x17` Register) und liest
  den Mixer dabei **nicht** zurück (Werte nur zwischengespeichert).
  **HYPOTHESIS:** aus einer Zusammenfassung der Kernel-Quelle, nicht
  wörtlich geprüft (Abschnitt 7.1).
  Übertragbarkeit auf das 802: **keine Anhaltspunkte**.
- Unser Projekt verbietet Control-Transfers bisher ausdrücklich
  (`check_forbidden_usb_calls`).

### 5.3 USB-MIDI und RME-SysEx

- Zwei MIDI-Ports (E4). Über **Kabel 1** nimmt das Gerät
  Register-SysEx an (E2). Normale MIDI-Daten (Noten, CC) sind davon zu
  trennen; RME (Forum, MC 2020): „The MIDI features mentioned in the
  manual for Stand-alone mode are not available in CC mode.“ Das betrifft
  die MIDI-Fernsteuerung des Stand-alone-Betriebs, nicht das
  Register-SysEx, das nachweislich wirkt (E2).
- Format (OSCMix, mit E3 an unserem Befehl bestätigt):
  `F0 00 20 0D 10 <subid> <Daten> F7`, je 32-Bit-Wort 5 Septette
  (LSB zuerst). Sub-IDs laut OSCMix: 0 = Register schreiben / Register
  melden (Gerät → Host), 2 = Pegelmeter anfordern (leere Nutzlast),
  3/4 = Loopback bzw. EQ/DRec; vom Gerät 1–5 = Pegelblöcke.
  **HYPOTHESIS** außer Sub-ID 0 schreibend (VERIFIED, E2).
- Zugang unter Linux ohne Treiberablösung: `snd-usb-audio` stellt das
  MIDI-Interface als ALSA-Rawmidi bereit (`/dev/snd/midiC<N>D0`,
  Unterport 1 = Kabel 1). Kein libusb, kein Detach, die Soundkarte
  bleibt erhalten. **HYPOTHESIS** (Standardverhalten von
  `snd-usb-audio`; für dieses Gerät nicht geprüft).
- Offener Unterschied: unser getesteter Transfer nutzte CIN 3 in jedem
  USB-MIDI-Paket; ALSA kodiert SysEx standardgemäß mit CIN 4/5/6/7. Ob
  das Gerät beides annimmt, ist **UNKNOWN** (OSCMix nutzt ALSA und meldet
  Erfolg → HYPOTHESIS: ja).

### 5.4 Weitere USB-Schnittstellen

- **Interface 4** (herstellerspezifisch, Bulk `0x09`/`0x88`) im
  CC-Modus. Im proprietären Modus trägt Interface 3 dieselben Endpunkte
  (`ff802-usb-desc.txt` bei OSCMix). Nutzung und Protokoll: **UNKNOWN**.
  Vermutlich der Weg, über den TotalMix FX den CC-Modus erkennt
  (HYPOTHESIS, [t4_disable.md](t4_disable.md)).
- Im proprietären Modus gehen Registerwörter über Bulk `0x0C`
  (VERIFIED für `0x3D44`). Eine USB-Aufzeichnung von TotalMix FX unter
  Windows zeigt dort dieselben 32-Bit-Wörter für jede Faderbewegung →
  Referenzquelle für Adressen und Werte (Abschnitt 10, Phase 1b).

### 5.5 Linux-Treiber und Open-Source-Projekte (Kurzfassung)

| Projekt | Relevanz für das 802 | Übertragbar? |
|---|---|---|
| `snd-usb-audio` (Kernel) | Audio + MIDI im CC-Modus, keine Mixer-Controls | Transport (Rawmidi) ja |
| Kernel-Quirks Babyface Pro FS/Digiface/ADI-2 | EP-0-Vendor-Requests | nein, keine Anhaltspunkte |
| `snd-fireface` (Kernel, FireWire) | 802 über FireWire, Mixer im Userspace | nur Wissen |
| snd-firewire-ctl-services (FireWire) | vollständiges 802-Mixerprotokoll | **Adressen stimmen mit OSCMix überein** (6.4); Transport anders |
| oscmix (upstream) | UCX II im CC-Modus via SysEx; 802 nur Kanaltabelle | Transport, Kodierung, Werkzeuge |
| oscmix (Fork huddx01) | 802 im CC-Modus, Registerkarte | wichtigste Quelle, ungeprüft |
| ALSA-UCM, PipeWire, WirePlumber | nur Software-Ebene | nein (Abgrenzung) |

## 6. Ergebnisse der OSCMix-Analyse

Gelesen (nur lesend, Rohdateien von GitHub, 2026-11-20): upstream
`michaelforney/oscmix` (Zweig `main`) und Fork `huddx01/oscmix` (Zweig
`dev`), Dateien siehe Abschnitt 13.

### 6.1 Architektur

- **Geräte:** upstream nur Fireface UCX II (README). `device_ff802.c`
  existiert upstream nur als Kanaltabelle ohne Registerfunktionen und
  wird in `init()` nicht registriert. Der Fork nennt „802, UCX, UCX II,
  UFX, UFX II, UFX III, UFX+“ („still needs a lot more testing“). „802
  FS“ kommt nirgends vor; das Gerät wird nur am MIDI-Portnamen erkannt
  („Fireface 802“). Für **unser** Modell gibt es keine Bestätigung im
  Code. **OBSERVED** (unsere praktische Nutzung, E11).
- **Betriebsmodus:** CC-Modus.
- **Komponenten:** `oscmix` (Kern, OSC-Server, UDP 7222/8222) liest
  SysEx von Dateideskriptor 6 und schreibt auf 7; Transport über
  `alsarawio` (ALSA-Rawmidi) bzw. `alsaseqio` (ALSA-Sequencer). GTK-,
  Web- und Qt-Oberflächen sind OSC-Clients. Kein direkter USB-Zugriff,
  keine Audioverarbeitung, keine Verbindung zu PipeWire.
- **Folgerung:** OSCMix verändert **ausschließlich Register im Gerät**
  per SysEx, es routet kein Audio in Software. Ein in OSCMix gesetzter
  Fader ist also eine Änderung der internen Matrix (passt zu E11).

### 6.2 Routing und Pegel

| Funktion | Code (Fork, `dev`) | Register beim 802 |
|---|---|---|
| Matrix-Pegel schreiben | `setmixlevel()` → `ctltoreg(MIX_LEVEL)` | `0x4000 \| out<<6 \| idx`, Playback `idx += 0x20 − 30` |
| Matrix lesen (Echo) | `regtoctl()` → `MIX` für Ausgangsblock `reg ≥ 0xE0`; `newmix()` „Volume-only MIX echo path (FF802-class first-generation devices)“ | `0x1E00 + out<<8 + 0xE0 + in`, nur `in < 30` (Hardware-Eingänge) |
| Matrix-Pan | `case MIX: /* TODO: Fill content */` | nicht vorhanden |
| Ausgangslautstärke | `OUTPUT_VOLUME`, `.scale=0.1`, −65 … +6 dB | `0x1E00 + out<<8 + 0x00` |
| Ausgangs-Mute | `OUTPUT_MUTE` | `0x1E00 + out<<8 + 0x02` |
| Eingangsparameter | `INPUT_*` | `in<<8 + p` |
| Playback-Kanäle | `ctltoreg` liefert −1 („never echoes a /playback/N/stereo update“) | keine eigenen Register |

### 6.3 Protokoll

- `setreg()`: `regval = (reg & 0x7fff) << 16 | val`; Paritätsbit so,
  dass das Wort ungerade Parität hat; `putle32()`; `writesysex(0, …)`
  mit `mfrid = 0x200d`, `devid = 0x10`. `base128enc()`: Bytes LSB zuerst
  in 7-Bit-Gruppen, 4 Byte → 5 Septette. Das ist **genau** unser
  `ff802_reg_encode()` + SysEx-Rahmen. Mit unserem Codec nachgerechnet:

  | Befehl | Register | Wert | SysEx |
  |---|---|---|---|
  | getesteter Disable-Befehl (Referenz) | `0x3D44` | `0xFFFE` | `F0 00 20 0D 10 00 7E 7F 13 6A 0B F7` ✓ |
  | SW PB 9 → Phones 9, 0 dB | `0x4228` | `0x9000` | `F0 00 20 0D 10 00 00 20 22 11 0C F7` |
  | SW PB 10 → Phones 10, 0 dB | `0x4269` | `0x9000` | `F0 00 20 0D 10 00 00 20 26 13 0C F7` |
  | SW PB 9 → Phones 9, aus | `0x4228` | `0x0000` | `F0 00 20 0D 10 00 00 00 20 11 0C F7` |
  | Phones 9 Volume −65,0 dB | `0x2600` | `0xFD76` | `F0 00 20 0D 10 00 76 7A 03 30 02 F7` |
  | Refresh-Anforderung | `0x3F99` | `0x0812` | `F0 00 20 0D 10 00 12 10 64 7C 03 F7` |

  Nur die erste Zeile ist am Gerät belegt; alle anderen sind
  **HYPOTHESIS** und dürfen ohne Freigabe nicht gesendet werden.
- Empfang: `handleregs()` zerlegt jedes Wort in `reg = w>>16 & 0x7fff`
  und einen vorzeichenbehafteten 16-Bit-Wert. Pegelblöcke: 3 Wörter je
  Kanal (RMS 64 Bit, Peak).
- Periodisch (Timer 100 ms): `setreg(0x3F00, serial)` mit 4-Bit-Zähler
  und, falls Meter aktiv, Sub-ID-2-Anforderung. Zweck von `0x3F00`
  laut Code unklar (Kommentar und Taktrate widersprechen sich).
  **UNKNOWN**, ob das 802 ohne diese Nachricht Register meldet.

### 6.4 Übereinstimmung mit dem FireWire-Protokoll

snd-firewire-ctl-services (`protocols/fireface/src/latter.rs`,
`latter/ff802.rs`) beschreibt das 802 über FireWire: Quadlet, „16 bit
target and 16 bit coefficient“, ungerade Parität in Bit 31.

| Größe | FireWire (`latter.rs`) | OSCMix-Fork | gleich? |
|---|---|---|---|
| Matrix | `0x40000000 \| ((0x40·mixer + ch) << 16)`, Stream ab `0x20` | `0x4000 \| out<<6 \| idx`, Playback ab `0x20` | **ja** (`0x4228`) |
| Ausgangslautstärke | `ch<<24 \| 0x00<<16`, Ausgänge ab ch 30, −650 … 60 | `0x1E00 + out<<8`, dB×10, −650 … 60 | **ja** (`0x2600`) |
| Eingangs-Gain | Befehl `0x07` | `p = 0x07` | ja |
| 0 dB / +6 dB Matrix | `0x9000` / `0xA000` | `0x9000` / `0xA000` | ja |
| −65 dB Matrix | `0x8000` | nicht vorgesehen (aus = `0x0000`) | **nein** |
| Mute Ausgang | nicht gefunden | `0x02` | offen |
| Rücklesen | nur Status und Meter | Echo nur für Eingänge | – |

**Bewertung:** Zwei Quellen mit unterschiedlichem Transport kommen auf
dieselben Adressen. Das stützt die Registerkarte deutlich. Ob der Fork
seine 802-Karte unabhängig ermittelt oder aus dem FireWire-Projekt
abgeleitet hat, ist **UNKNOWN**; die Übereinstimmung ist deshalb kein
Beweis. → Registerkarte insgesamt **HYPOTHESIS (stark gestützt)**.

### 6.5 Lesen des Zustands

- Start: `/refresh` → `setreg(0x3F99, 0x0812)` (Fork, 802); upstream UCX
  II: `0x3E04` = `0x67CD`. Danach verarbeitet OSCMix eingehende
  Sub-ID-0-Meldungen als Registerwerte. Ein ausdrückliches Lesekommando
  für einzelne Register gibt es nicht. **HYPOTHESIS**.
- Playback-Matrix: kein Echo im Code (6.2) → ob der Zustand
  `SW PB 9 → Phones 9` auslesbar ist, ist **UNKNOWN**. Das passt zu E11
  („oscmix zeigte nicht die wirksamen Werte“).
- `tools/regtool.c` (upstream) protokolliert Register über den
  ALSA-Sequencer (Dump-Modus) und kann schreiben (`-w`).

### 6.6 Übertragbarkeit, Lizenz, Risiken

- **Verwendbar:** Transport über ALSA-Rawmidi, SysEx-Rahmen und
  Kodierung (identisch mit E2/E3), Adressschema, Pegel-Endpunkte,
  Dump-Auswertung.
- **Geräteabhängig:** Refresh-Wert, Adressen jenseits Matrix/Volume,
  Echo-Format, Kanalzahlen.
- **Lizenz:** OSCMix: ISC-artige Erlaubnis („Permission to use, copy,
  modify, and/or distribute … with or without fee“), Copyright Michael
  Forney; mit MIT verträglich, Übernahme mit Copyright-Hinweis möglich.
  snd-firewire-ctl-services: Protokoll-Bibliotheken LGPL-3.0-or-later,
  Laufzeit GPL-3.0 → **kein Code übernehmen**; Fakten (Adressen,
  Wertebereiche) sind als Information verwendbar.
- **Risiken:** ungeprüfte Adressen können andere Funktionen treffen
  (48 V, Setup speichern `0x3F97`, CC-Modus `0x3D44`); Formeln unterhalb
  0 dB widersprüchlich; Refresh-Wert gerätespezifisch und ungeprüft;
  OSCMix ist laut Autor „untested/experimental“.

## 7. Weitere Open-Source-Referenzen

### 7.1 Linux-Kernel `sound/usb`

- `mixer_quirks.c`: RME-Sonderbehandlung nur für `2a39:3fd2/3fd3/3fd4`
  (ADI-2), `2a39:3fb0` (Babyface Pro FS), `2a39:3f8c/3fa0` (Digiface
  USB). Kein Fireface, kein `0x0424`. Babyface-Getter lesen nicht vom
  Gerät, sondern liefern zwischengespeicherte Werte.
- **Einschränkung:** Die Datei wurde nur teilweise wörtlich gelesen,
  die RME-Abschnitte stammen aus einer Zusammenfassung → Status
  **HYPOTHESIS**, vor Verwendung wörtlich prüfen.

### 7.2 FireWire (`snd-fireface`, snd-firewire-ctl-services)

- Kernel `ff.c`: 802 als „latter“-Modell, 30/22/14 PCM-Kanäle je
  Ratenbereich, keine Mixer-Funktionen (Mixer im Userspace).
- snd-firewire-ctl-services: Mixer und Ausgänge werden nur
  **geschrieben**; die Laufzeit merkt sich die letzten Werte selbst.
  Gelesen werden nur Status und Pegelmeter. Für die übrigen Fragen
  siehe 6.4. README: Die Steuerung ist „not synchronized to any
  operation by the Remote Control or the Advanced Remote Control“.

### 7.3 Was es nicht gibt

- Keine Dokumentation von RME zum Registerprotokoll, keine öffentliche
  Beschreibung von Interface 4, keine Aufzeichnung von TotalMix FX für
  iPad.

## 8. Vergleich der Lösungsansätze

Bewertung: ++ gut, + eher gut, o offen, − eher schlecht, −− schlecht.

| Kriterium | A ALSA-Controls | B USB-Control/IF 4 | C MIDI/SysEx | D Kombination | E OSCMix-Erweiterung |
|---|---|---|---|---|---|
| 1 Machbarkeit | − (Kernel-Quirk nötig, Transport unklar) | o | **+** (Transport belegt, E2) | + | + |
| 2 Protokollwissen | −− | −− | **+** (Kodierung VERIFIED, Adressen HYPOTHESIS) | + | + |
| 3 Reverse Engineering | hoch | sehr hoch | gering bis mittel (Bestätigung der Adressen) | mittel | gering |
| 4 Lesen | o (Kernel liest bei RME nicht zurück) | o | o (Refresh/Echo, Playback offen) | **+** (Windows-Aufzeichnung als Referenz) | o |
| 5 Schreiben | o | o | **+** | + | + |
| 6 Risiko Gerät/Gehör | mittel | hoch (unbekannte Bulk-Befehle) | mittel (falsche Adressen) | mittel | mittel bis hoch (ganze Zustände, Timer schreibt ständig `0x3F00`) |
| 7 Abhängigkeit Windows | keine | Windows für Analyse | keine im Betrieb | Windows nur einmalig zur Verifikation | keine |
| 8 Integration ins Projekt | Kernel-Patch, außerhalb | libusb wie `disable`, schwierig | **++** (Codec, SysEx, Sicherheitsrahmen vorhanden) | ++ | − (fremder OSC-Server, eigene Architektur) |
| 9 Wartbarkeit | − (Upstream-Kernel) | −− | ++ | + | o (Fork mit unklarer Zukunft) |
| 10 Aufwand | hoch | sehr hoch | **gering bis mittel** | mittel | gering für Test, hoch für Integration |

**Empfehlung: Variante C (RME-SysEx über USB-MIDI, Zugriff über
ALSA-Rawmidi) als Steuerweg, abgesichert durch Variante D.**

Begründung:
1. Der Transport ist an unserem Gerät im CC-Modus belegt (E2), unsere
   Kodierung ist bytegleich mit OSCMix (E3).
2. Die Matrix- und Volume-Adressen werden von zwei Quellen gleich
   angegeben (6.4).
3. Der Weg braucht weder Kernel-Änderung noch Treiberablösung; die
   Soundkarte bleibt bestehen.
4. Das Projekt hat Codec, SysEx-Erzeugung, Sicherheitsrahmen
   (Bestätigung, Allowlist-Prinzip, statische Prüfungen) und ein
   Messwerkzeug.

Variante D heißt hier: die Adressen und Wertebereiche **vor** dem
ersten Schreibtest unabhängig bestätigen, entweder (a) passiv aus den
Registermeldungen des Geräts oder (b) aus einer USB-Aufzeichnung von
TotalMix FX unter Windows (proprietärer Modus, Bulk `0x0C`), in der
eine bekannte Faderbewegung genau ein bekanntes Wort erzeugt. OSCMix
(E) dient als Informationsquelle und, nach Freigabe, als
Vergleichswerkzeug, nicht als Bestandteil unserer Lösung. A und B
werden nicht weiterverfolgt, solange C trägt.

## 9. Empfohlene technische Architektur

```text
ff802-mixer (CLI, neu, später)
  ├─ Registerkarte  (Allowlist: nur Matrix 0x4000–0x47FF, Ausgangs-Volume/-Mute;
  │                  alles andere gesperrt, insbesondere 0x3D44, 0x3F97, 48 V)
  ├─ Codec          (vorhandenes ff802_reg_encode/decode, SysEx-Rahmen)
  ├─ Transport      (ALSA-Rawmidi, Kabel 1 des Fireface; Lesen und Schreiben getrennt
  │                  einkompilierbar, wie FF802_ENABLE_USB_WRITE)
  ├─ Zustandsspiegel (zuletzt gemeldete Werte; Rückbau nur auf gemessene Werte)
  └─ Sicherheitsrahmen (Bestätigungswort, Pegelobergrenze, Protokolldatei je Schreibvorgang)
```

- Lesepfad und Schreibpfad sind getrennte Programme oder Build-Optionen;
  der Standard-Build kann nur lesen.
- Pegelgrenze im Code: kein Matrix-Wert über 0 dB (`0x9000`), kein
  Ausgangs-Volume über einem festen Grenzwert.
- Kein automatisches Schreiben, keine Timer-Schreibvorgänge (anders als
  OSCMix).
- Später möglich: ALSA-Control-Plugin oder ein kleiner Dienst für
  PipeWire, erst nach Phase 4.

## 10. Schrittweiser Entwicklungsplan

### Phase 1 – Bestandsaufnahme (nur Analyse)

| | |
|---|---|
| Ziel | Wissen sichern, offene Fragen priorisieren |
| Vorgehen | 1a: dieses Konzept; Kernel-Quelle und `intpack.h`/`alsarawio.c` wörtlich prüfen. 1b (optional, Freigabe nötig): USB-Aufzeichnung unter **Windows** mit TotalMix FX im proprietären Modus: genau einen Fader `SW PB 9 → Phones 9` bewegen, Wort auf Bulk `0x0C` dekodieren (vorhandener Codec) |
| Voraussetzungen | für 1b: Windows-Rechner, USBPcap/Wireshark **dort** |
| Ergebnis | bestätigte Adresse und Werteskala für eine Verbindung |
| Risiken | 1b: keine für Linux; Fader nur in TotalMix bewegen, Kopfhörer ab |
| Erfolgskriterium | Wort `0x4228`/`0xC228xxxx` erscheint (oder Widerlegung) |
| Freigaben | 1b: Installation auf Windows, Hardwarebedienung |

### Phase 2 – Lesender Zugriff (keine Schreibzugriffe)

| | |
|---|---|
| Ziel | herausfinden, ob und was das Gerät von sich aus meldet |
| Vorgehen | 2a: auf dem Host das Rawmidi-Gerät des Fireface **nur lesend** öffnen (z. B. vorhandenes `amidi -d`, falls installiert; sonst kleines Lese-Werkzeug im Container gebaut) und mitschreiben, während (i) nichts passiert, (ii) am Gerät ein Ausgangspegel per Drehregler verändert wird, (iii) unter Windows nichts. 2b (Freigabe): **eine** Refresh-Anforderung `0x3F99 = 0x0812` bzw. eine leere Sub-ID-2-Meteranforderung senden und die Antwort aufzeichnen |
| Voraussetzungen | Gerät im CC-Modus, keine Wiedergabe, Kopfhörer ab; Auswerter, der SysEx in Register/Wert zerlegt (Codec vorhanden) |
| Ergebnis | Liste gemeldeter Register; Antwort auf: gibt es ein Echo für `0x4228`? |
| Risiken | 2a: keine (nur Lesen). 2b: ein Schreibbefehl auf ein ungeprüftes Register; Wirkung laut OSCMix nur Dump |
| Erfolgskriterium | dekodierbare Sub-ID-0-Wörter mit gültiger Parität; idealerweise Werte der Ausgänge 9/10 |
| Freigaben | 2a: Hardwarezugriff lesend; 2b: gesondert |

### Phase 3 – Kontrollierter Einzeltest (nur nach Freigabe)

| | |
|---|---|
| Ziel | einen Matrix-Pegel `SW PB 9 → Phones 9` setzen und die Wirkung messen |
| Vorgehen | Messaufbau wie T2: Phones 9/10 per Kabel auf Line In 1, Kopfhörerregler Mitte, kein Kopfhörer; `ff802-chanmap`-Signal −40 dBFS auf USB-Kanal 9. Ablauf: (1) Ausgangszustand messen (Pegel an Line In 1); (2) **genau ein** Wort senden, zuerst in Richtung **leiser**: `0x4228 = 0x0000` (aus); (3) messen; (4) Rückbau auf den in Phase 1b/2 **gemessenen** Ausgangswert; (5) messen |
| Voraussetzungen | Phase 2 abgeschlossen; Ausgangswert von `0x4228` bekannt (aus Echo, Dump oder Windows-Aufzeichnung) |
| Ergebnis | Nachweis: Register `0x4228` steuert SW PB 9 → Phones 9; Pegeldifferenz in dB |
| Risiken | falsche Adresse; Pegelsprung beim Rückbau – begrenzt durch −40-dBFS-Signal und fehlende Wandler |
| Erfolgskriterium | Signal verschwindet bei `0x0000` und kehrt beim Rückbau mit dem Ausgangspegel (±0,5 dB) zurück; keine anderen Ausgänge verändert (Kontrolle an Out 1) |
| Freigaben | gesonderte Freigabe mit Testprotokoll |

**Rückbau-Regel:** Ist der Ausgangswert nicht gemessen, wird **nicht**
geschrieben. Ersatzweise ist ein Test zulässig, dessen Endzustand
absolut definiert ist (z. B. `0x4228 = 0x9000`, 0 dB) und den Sie vorab
als neuen Sollzustand akzeptieren.

### Phase 4 – Minimale Mixersteuerung

| | |
|---|---|
| Ziel | CLI: einzelne Verbindungen lesen (soweit möglich), setzen, aus/an, definierten Zustand herstellen |
| Vorgehen | Werkzeug nach Abschnitt 9; Tests im Container mit simuliertem Rawmidi; Allowlist; Protokolldatei |
| Voraussetzungen | Phase 3 erfolgreich |
| Ergebnis | `ff802-mixer get/set/mute` für Matrix und Ausgangs-Volume |
| Risiken | wie Phase 3; zusätzlich Bedienfehler → Bestätigung und Pegelgrenze |
| Erfolgskriterium | wiederholbare Tests am Gerät, alle Änderungen messbar und zurücknehmbar |
| Freigaben | Implementierung; jeder Hardwaretest |

### Phase 5 – Erweiterung

Ganze Matrix, Eingangs-Routing, Ausgangsfunktionen, Speichern/Laden von
Konfigurationen (eigene Dateien, **nicht** `0x3F97`), gegebenenfalls
ALSA/PipeWire-Anbindung oder Oberfläche. Jede neue Registergruppe
einzeln wie Phase 3 bestätigen. Freigaben je Schritt.

## 11. Sicherheits- und Rückbaukonzept

1. **Kein Schreiben ohne Freigabe**, und dann nur einzeln bestätigte
   Wörter aus einer Allowlist. Gesperrt: `0x3D44` (CC-Modus), `0x3F97`
   (Setup speichern), Eingangsparameter `0x08` bei Mic 9–12 (48 V),
   alle unbekannten Adressen, Timer-/Heartbeat-Schreibvorgänge.
2. **Pegel:** Matrix höchstens 0 dB; erste Tests nur in Richtung leiser;
   Messsignal −40 dBFS; während Tests keine Lautsprecher oder Kopfhörer
   am Gerät, Kopfhörerregler höchstens Mitte (Messschleife).
3. **Kein Moduswechsel**, keine Treiberablösung: Zugriff nur über das
   ALSA-Rawmidi-Gerät von `snd-usb-audio`.
4. **Rückbau** nur auf **gemessene** Werte (Echo, Dump oder
   Windows-Aufzeichnung). Ohne gemessenen Ausgangswert kein
   Schreibtest; ein absolut definierter Endzustand ist nur nach
   ausdrücklicher Zustimmung zulässig.
5. **Dokumentation je Schreibtest:** Ausgangszustand (Messung +
   Registerwert), gesendetes Wort (hex, SysEx), Messung danach, Rückbau,
   Messung nach Rückbau, Abweichungen.
6. **Notweg:** TotalMix FX unter Windows/macOS stellt jeden Mixerzustand
   wieder her; der CC-Modus wird dabei verlassen
   ([t4_disable.md](t4_disable.md)).
7. Kein Speichern im Gerät (`0x3F97`), keine automatischen Änderungen.

## 12. Offene technische Fragen (priorisiert)

| Prio | Frage | Weg zur Klärung |
|---|---|---|
| 1 | Meldet das 802 im CC-Modus Registerwerte (spontan, nach Refresh, mit welchem Refresh-Wert)? | Phase 2a/2b |
| 2 | Ist die Playback-Matrix (`0x4228`) auslesbar? | Phase 2; sonst Phase 1b |
| 3 | Stimmen Adresse `0x4228` und Skala (`0x9000` = 0 dB) für unser Gerät? | Phase 1b, Phase 3 |
| 4 | Wertedarstellung unterhalb 0 dB (OSCMix linear vs. FireWire `0x8000` = −65 dB) | Phase 1b (TotalMix-Fader auf bekannte dB-Werte) |
| 5 | Akzeptiert das Gerät SysEx mit Standard-CIN (ALSA) statt CIN 3? | Phase 2b (die Antwort zeigt es) |
| 6 | Zuordnung der 22 USB-Kanäle zu den 30 Software-Playback-Kanälen | T2 mit erfasstem Mixer-Zustand wiederholen |
| 7 | Rolle von `0x3F00` (Heartbeat?) für Meldungen | Phase 2 |
| 8 | Flüchtigkeit des Mixer-Zustands, Verhalten über Aus/Ein und Moduswechsel | nach Phase 3 (Messung vor/nach) |
| 9 | Nutzung von Interface 4 | nachrangig, nur Aufzeichnung unter Windows |
| 10 | Wirkt `PCM Playback Volume` im Gerät? | lesend vergleichen, nachrangig |

## 13. Quellen und Referenzen

**Projekt:** [protocol.md](protocol.md), [cc_disable_port_reset.md](cc_disable_port_reset.md),
[t4_disable.md](t4_disable.md), [mixer_routing.md](mixer_routing.md),
[audio_t2_plan.md](audio_t2_plan.md), [audio_t2_results/](audio_t2_results/README.md),
[audio_t3.md](audio_t3.md), [usb_comm.md](usb_comm.md),
`src/reg_codec.c`, `src/cc_mode.c` (`ff802_cc_disable_midi()`),
`src/disable_check.c`, `tests/test_cc_mode.c`.

**OSCMix upstream** (michaelforney, Zweig `main`, gelesen 2026-11-20):
`README.md`, `oscmix.c` (`setreg`, `writesysex`, `handleregs`,
`handletimer`, `init`), `sysex.c`, `sysex.h`, `device.h`,
`device_ffucxii.c`, `device_ff802.c`, `main.c`, `Makefile`,
`tools/regtool.c`, `doc/ucx2-cc-desc.txt`, `doc/ucx2-usb-desc.txt`,
`doc/ff802-cc-desc.txt`, `doc/ff802-usb-desc.txt`, `LICENSE` –
<https://github.com/michaelforney/oscmix>.

**OSCMix-Fork** (huddx01, Zweig `dev`, gelesen 2026-11-20):
`README.md`, `device_ff802.c` (vollständig: `regtoctl`, `ctltoreg`,
`struct device ff802` mit `.refresh = 0x0812`,
`.flags = DEVICE_MIX_VOLONLY`), `oscmix.c` (bis Zeichen 39929 wörtlich:
`setreg`, `setmixlevel`, `newmix`, `setrefresh`), `sysex.c`,
`device.h`, `main.c`, `LICENSE` – <https://github.com/huddx01/oscmix>.
Nicht gelesen: `intpack.h`, `alsarawio.c`, `alsaseqio.c`, Rest von
`oscmix.c`, Issues.

**FireWire:** Linux `sound/firewire/fireface/ff.c`;
snd-firewire-ctl-services `protocols/fireface/src/latter.rs` (bis etwa
Zeile 1000), `protocols/fireface/src/latter/ff802.rs` (vollständig),
README – <https://github.com/alsa-project/snd-firewire-ctl-services>.
Nicht gelesen: `ff-protocol-latter.c` (HTTP 429).

**Kernel USB:** `sound/usb/mixer_quirks.c`, `quirks-table.h` (jeweils
nur teilweise wörtlich), `format.c`, `implicit.c`, `mixer_quirks.h` –
<https://github.com/torvalds/linux/tree/master/sound/usb>. Nicht
gelesen: `quirks.c` (HTTP 429).

**RME:** Fireface 802 User's Guide, S. 77, 78, 90, 91, 93
(<https://www.manualslib.com/manual/745601/Rme-Audio-Fireface-802.html>);
RME-Forum „Remote controlling Fireface 802 in class compliant mode?“
(<https://forum.rme-audio.de/viewtopic.php?id=31390>).
