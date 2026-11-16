# ALSA-UCM-Profil für das Fireface 802 (Class-Compliant-Modus)

> **Stand: Prototyp, nicht installiert, nicht aktiviert.**
> Die Kanalzuordnung ist **unbestätigt** (`FF802-CHANNEL-MAP: UNVERIFIED`).
> Auf dem Host wurde noch nichts ausgeführt. T1, T2 und T3 brauchen jeweils
> eine eigene Freigabe.

## Ziel

Unter KDE Plasma sollen sechs **eigenständig auswählbare** Stereo-Ausgabegeräte
und ein Stereo-Eingabegerät erscheinen:

| UCM-Gerät | Bezeichnung (UCM-Comment) | Richtung | USB-Kanäle (unbestätigt) | Priorität |
|---|---|---|---|---|
| `Headphones 1` | Headphones Output 9/10 | Wiedergabe | 8, 9 | 255 (Standard) |
| `Headphones 2` | Headphones Output 11/12 | Wiedergabe | 10, 11 | 200 |
| `Line 1` | Analog Output 1/2 | Wiedergabe | 0, 1 | 192 |
| `Line 2` | Analog Output 3/4 | Wiedergabe | 2, 3 | 191 |
| `Line 3` | Analog Output 5/6 | Wiedergabe | 4, 5 | 190 |
| `Line 4` | Analog Output 7/8 | Wiedergabe | 6, 7 | 189 |
| `Line 5` | Analog Input 1/2 | Aufnahme | 0, 1 | 192 |

AES/EBU, ADAT und die übrigen Eingänge erhalten keine eigenen Geräte. Sie
bleiben über das Profil `pro-audio` erreichbar. Die Geräte- und
Kommentarnamen folgen den RME-UCX-Profilen von upstream und sind englisch.

## Dateien

```
audio/ucm2/USB-Audio/conf.d/0424-3fdd.conf        Erkennung, wählt das Profil
audio/ucm2/USB-Audio/RME/Fireface-802.conf        Profil: Kanalbelegung, Use Case "HiFi"
audio/ucm2/USB-Audio/RME/Fireface-802-HiFi.conf   SplitPCM-Definitionen und 7 Geräte
tests/audio/check_ucm_profile.cmake               statische Prüfung (Struktur, Inhalt, Sicherheit)
tests/audio/test_ucm_syntax.c                     Syntaxprüfung mit dem Parser von alsa-lib
tests/audio/test_ucm_t1.sh                        simulierter Test des T1-Skripts
tools/hw-test/ff802-ucm-t1.sh                     isolierter, nur lesender Host-Test T1
```

Der Verzeichnisaufbau unter `audio/ucm2/` entspricht dem von
`/usr/share/alsa/ucm2/`. So lassen sich die Dateien unverändert in eine
Testkopie, ein Paket oder einen Upstream-Beitrag übernehmen.

## Funktionsweise

Die folgenden Aussagen sind im Quellcode der Host-Versionen nachvollzogen:
alsa-ucm-conf 1.2.15.3, alsa-lib 1.2.15.3, PipeWire 1.6.2, WirePlumber 0.5.13.

### 1. Einbindung (alsa-ucm-conf, alsa-lib)

- `USB-Audio/USB-Audio.conf` (Syntax 8) enthält
  `If.opt.Append.Include.opt { File "/USB-Audio/conf.d/${var:USBID1}-${var:USBID2}.conf" Optional true }`.
- **alsa-lib, `ucm_cond.c`:** Ab Syntax 8 darf ein `If` ohne `Condition`
  stehen, wenn es einen `Append`-Block hat. Alle `If`-Blöcke werden der Reihe
  nach ausgewertet, und der `Append`-Inhalt wird sofort an Ort und Stelle
  ausgewertet (`uc_mgr_evaluate_inplace`: zuerst `Define`, dann `Include`).
  Ein `Define.ProfileName` in unserer `conf.d`-Datei steht deshalb fest,
  **bevor** `If.inc` prüft, ob `ProfileName` leer ist („UCM is not supported“).
- **Zusätzliche Absicherung:** Die `conf.d`-Datei setzt `ProfileName` nur, wenn
  `${CardLongName}` „Fireface 802“ enthält und nicht „802 FS“. Passt der Name
  nicht, bleibt alles wie bisher, denn alsa-lib meldet dann „UCM is not
  supported“.
- **Vorbild:** Das Fireface UCX meldet sich im CC-Modus ebenfalls mit der
  Microchip-ID (`USB0424:3fb9`).

### 2. SplitPCM

- `Fireface-802-HiFi.conf` definiert mit `Macro.SplitPCM` zwei Aufteilungen
  des 22-Kanal-Geräts: `ff802_stereo_out` und `ff802_stereo_in`. Jedes
  UCM-Gerät wählt mit `Macro.pcm_split.SplitPCMDevice` zwei Kanäle aus.
- **Mit SplitPCM-Unterstützung der Anwendung** (Präfix `<<<SplitPCM=1>>>`)
  liefert UCM `PlaybackPCM "hw:<Karte>,0"`, `PlaybackChannels 22` und die
  Kanalindizes. Die Anwendung teilt dann selbst auf. **Ohne** diese
  Unterstützung verwendet alsa-lib die `dshare`- bzw. `dsnoop`-Geräte
  `ff802_stereo_out:…`.
- Es gibt keine Einschränkung der Abtastrate: Nach `/proc/asound/card1/stream0`
  stehen 22 Kanäle bei allen Raten bis 192 kHz zur Verfügung.

### 3. Sechs Geräte statt sechs Ports (PipeWire-ACP, WirePlumber)

- **ACP, `alsa-ucm.c`:** Jedes UCM-Gerät erhält ein eigenes Mapping
  (`"Mapping HiFi: <Gerät>: sink"`) und wird damit zu einer eigenen Senke bzw.
  Quelle.
- **Ohne SplitPCM** dürften Geräte mit demselben `PlaybackPCM` nicht
  gleichzeitig in einem Profil stehen. Dann wäre immer nur ein Ausgang aktiv,
  umschaltbar wie ein Port. **Mit SplitPCM** gilt eine Ausnahme im Code
  („…except when both split“). Die Profilbildung nimmt die größtmögliche
  Gerätegruppe. Da es keine `ConflictingDevice` gibt, entsteht **ein**
  HiFi-Profil mit allen sieben Geräten gleichzeitig.
- **WirePlumber, `monitors/alsa.lua`:** setzt `api.alsa.split-enable = true`
  und erzeugt je Split-Gerät
  - eine sichtbare `Audio/Sink` (bzw. `Audio/Source`) mit eigener
    Software-Lautstärke. Das ist die Seite, die KDE anzeigt.
  - einen internen Stream (`Stream/Output/Audio/Internal`,
    `audio.position [AUXn, AUXn+1]`) zu
  - **einem** verborgenen Hardwareknoten (`Audio/Sink/Internal`, 22 Kanäle).
- Beim Entfernen des Geräts entfernt `removeNode` alle diese Objekte wieder.
  Die Ausgänge existieren nur, solange das Fireface angeschlossen ist.
- Das Zuordnen von Anwendungen und die Lautstärke je Ausgang sind damit
  vorhandene Funktionen von PipeWire und KDE. Eigener Laufzeitcode ist nicht
  nötig.

### 4. Standardausgang

- `Headphones 1` hat die höchste `PlaybackPriority` des Profils (255). Ein
  Test stellt sicher, dass das so bleibt.
- **Offen:** ob dieser Wert die Priorität anderer Karten übersteigt und ob
  WirePlumber ihn tatsächlich als Standard wählt. Das bisher gewählte
  Standardgerät (Stereo-Profil) verschwindet mit dem Profilwechsel.
- Eine zusätzliche WirePlumber-Regel wird **nur** entwickelt, wenn T2 zeigt,
  dass sie nötig ist. Ausschlaggebend sind dafür die `priority.session`-Werte
  aller Senken in `pw-dump`.

## Prüfstatus

| Prüfung | Ort | Status |
|---|---|---|
| `ucm_profile_static` | Container | ✓ bestanden. Prüft gegen eine eigene Erwartungstabelle: Geräte, Richtungen, Kanalpaare, Kommentare; `HWChannels 22` und 22 FL/FR-Positionen in beiden SplitPCM; eindeutige Kanäle; `Headphones 1` mit der höchsten Priorität; Absicherung gegen 802 FS; genau ein Use Case; Kennzeichnung der Kanalzuordnung; keine `cset`/`exec`/Sequenzen/Mixer-Elemente/Raten/Konflikte/Digitalgeräte; kein Schreibfehler `PlackbackPriority`. Zwölf eingebaute Fehler wurden alle erkannt. |
| `ucm_alsa_syntax` | Container | **übersprungen**, bis der Container mit `libasound2-dev` neu gebaut ist |
| `ucm_t1_script` | Container | ✓ bestanden (32 Prüfungen mit nachgebautem UCM-Verzeichnis, nachgebautem procfs und protokollierendem `alsaucm`) |
| UCM-Auswertung mit echter Karte | Host, T1 | ausstehend |
| Geräte in PipeWire/KDE, Prioritäten, Lautstärken | Host, T2 | ausstehend |
| physische Kanalzuordnung | Host, T3 | ausstehend |

Im Container lässt sich UCM nicht vollständig auswerten. Bedingungen wie
`${CardComponents}` oder `${CardLongName}` brauchen eine echte Soundkarte.

## Host-Test T1 (isoliert, nur lesend)

**Zweck:** Prüfen, ob alsa-lib das Profil mit der echten Karte findet und
auswertet. Dabei werden keine Systemdateien geändert, kein PCM geöffnet,
kein Ton ausgegeben, und PipeWire bleibt unberührt.

Voraussetzungen: Fireface im CC-Modus angeschlossen, `alsaucm` vorhanden
(Paket `alsa-utils`). Als normaler Benutzer ausführen, nicht als Root.

```sh
cd /home/bernhard/Develop/fireface802-cc
tools/hw-test/ff802-ucm-t1.sh prepare   # private Kopie in ~/.cache/ff802-ucm-t1
tools/hw-test/ff802-ucm-t1.sh run       # nur lesende alsaucm-Abfragen
tools/hw-test/ff802-ucm-t1.sh clean     # vollständiger Rückbau
```

**`prepare`:**
- kopiert `/usr/share/alsa/ucm2` nach `~/.cache/ff802-ucm-t1/ucm2` und ergänzt
  die drei Profildateien
- bricht ab, wenn das System eine dieser Dateien bereits mitbringt, wenn der
  `conf.d`-Mechanismus fehlt oder wenn eine der beteiligten Dateien
  Steuerbefehle enthält (`cset`, `exec`, Boot- oder Default-Sequenzen)

**`run`:**
- ermittelt die einzige Karte mit `usbid` `0424:3fdd`
- prüft deren Langnamen
- führt die folgenden Abfragen aus und legt die Ausgaben unter
  `~/.cache/ff802-ucm-t1/results/` ab:
  - `alsaucm -c hw:N list _verbs` ohne die Testkopie, als Vergleich
  - mit `ALSA_CONFIG_UCM2=<Testkopie>`, gesetzt nur für die jeweiligen
    `alsaucm`-Prozesse: `list _verbs`, `list _devices/HiFi`, `dump text`
    sowie `dump text` mit Präfix `<<<SplitPCM=1>>>`
- `alsaucm` öffnet dabei nur das Steuergerät der Karte zum Lesen. In
  alsa-lib 1.2.15.3 führt `snd_use_case_mgr_open` keine Boot- oder
  Default-Sequenzen aus. `set`, `reset`, `_boot` und `_verb` verwendet das
  Skript nicht.
- **Erwartet:** Der Vergleich ohne Testkopie meldet „UCM is not supported“.
  Mit Testkopie gibt es den Verb `HiFi` mit sieben Geräten, Verweise auf
  `ff802_stereo_out`/`ff802_stereo_in` und, im SplitPCM-Fall,
  `PlaybackChannels 22`, `CaptureChannels 22` und `PlaybackPCM "hw:…"`.

**Rückbau:** `clean` löscht `~/.cache/ff802-ucm-t1`, aber nur, wenn das
Verzeichnis die Markierungsdatei des Skripts enthält. Sonst wurde nichts
verändert.

**Wenn `run` scheitert:** Die Ergebnisdateien zeigen, ob es am Langnamen
(Absicherung in `conf.d`), an der Syntax oder an den Werten liegt.
`card.txt` enthält den erkannten Langnamen.

## Lautstärkesicherheit vor T2 und T3

Wir verlassen uns nicht auf Standardwerte von WirePlumber. Vor dem ersten
praktischen Audiotest gilt diese Reihenfolge:

1. **Physisch:**
   - beide Kopfhörerregler am Fireface ganz nach links
   - Kopfhörer abnehmen
   - Endstufen bzw. Aktivlautsprecher an Analog 1–8 ausschalten oder abklemmen
2. **Keine laufende Wiedergabe:** Alle Wiedergaben beenden (Browser, Player).
   WirePlumber verschiebt Streams ohne festes Ziel zum neuen Standardgerät. Ein
   laufender Stream könnte also nach der Aktivierung sofort auf
   „Headphones 1“ landen.
3. **Ausgangslage festhalten (nur lesend):** `wpctl status`, `wpctl settings`
   (darunter `device.routes.default-sink-volume`), Kopie von
   `~/.local/state/wireplumber/` für den Rückbau.
4. **Direkt nach der Aktivierung in T2, vor jeder Wiedergabe:**
   - Alle sechs neuen Senken stummschalten und auf 0 setzen. Die Objekt-IDs
     werden dabei zur Laufzeit über den `node.name` aus `pw-dump` ermittelt,
     es gibt keine festen IDs.
   - Mit `wpctl get-volume` kontrollieren, dass jede Senke `[MUTED]` meldet.
   - Den verborgenen Hardwareknoten zusätzlich stummschalten. Er ist ein
     Hauptschalter für alle Ausgänge, und eine Stummschaltung dort kann nichts
     lauter machen.
5. **In T3:**
   - Testsignal mit höchstens −40 dBFS. Die Datei wird im Container erzeugt,
     und der niedrige Pegel gilt unabhängig von der Software-Lautstärke.
   - Pro Schritt wird genau eine Senke freigegeben, mit niedriger Lautstärke,
     und nur in kleinen Schritten erhöht.
   - Die Kopfhörerregler am Gerät werden erst danach behutsam aufgedreht.
6. **Nach dem Test:** Die gespeicherten Lautstärken in WirePlumber kontrollieren
   bzw. die gesicherte Kopie zurückspielen.

Die Schritte 4 bis 6 werden vor T2 als eigenes Skript ausgearbeitet und
gesondert freigegeben.

## Ausblick T2 und T3 (noch nicht vorbereitet)

- **T2, Sitzungstest:** `ALSA_CONFIG_UCM2` wird nur für die Benutzerdienste
  `pipewire` und `wireplumber` gesetzt, danach werden beide neu gestartet.
  Dabei wechselt das Kartenprofil auf HiFi, und das lässt sich nicht
  vermeiden. Geprüft werden: sieben Geräte, Namen in KDE, Prioritäten,
  Startlautstärken, Speicherung nach erneutem Anschließen.
  **Rückbau:** Variable entfernen, gesicherten WirePlumber-Zustand
  zurückspielen, Dienste neu starten.
- **T3, Kanalzuordnung:** Signaltest je Ausgang nach den obigen
  Sicherheitsregeln. Erst danach wird `FF802-CHANNEL-MAP` auf `VERIFIED`
  gesetzt bzw. die Zuordnung korrigiert.

## Paketierung und Upstream

- **Lokales Paket (später zu entscheiden):** Es würde **neue** Dateien unter
  `/usr/share/alsa/ucm2/USB-Audio/` ablegen, ohne vorhandene Dateien zu
  ändern. Eine Datei mit `ALSA_CONFIG_UCM2` ist dafür ungeeignet, weil die
  Variable das gesamte UCM-Verzeichnis ersetzt.
- **Upstream (alsa-ucm-conf):** Statt `conf.d` käme ein Eintrag in
  `USB-Audio.conf` dazu, analog zum UCX:
  ```
  If.fireface-802 {
  	Condition {
  		Type String
  		Haystack "${CardComponents}"
  		Needle "USB0424:3fdd"
  	}
  	True.Define.ProfileName "RME/Fireface-802"
  }
  ```
  Dazu kämen `RME/Fireface-802.conf` und `RME/Fireface-802-HiFi.conf`. Erst
  nach T3 und mit entfernter Markierung `UNVERIFIED` sinnvoll.
- **Upstream-Hinweis:** Die vorhandenen RME-UCX-Profile schreiben
  `PlackbackPriority`. Diese Prioritäten werden deshalb ignoriert. Unser
  Profil verwendet `PlaybackPriority`, und der statische Test verhindert den
  Schreibfehler.

## Risiken vor T2 und T3

1. **Kanalzuordnung unbestätigt.** Im CC-Modus kann der im Gerät gespeicherte
   Mixer-Zustand (TotalMix) die Zuordnung beeinflussen.
2. **Automatischer Profilwechsel:** Sobald ein UCM-Profil für die Karte
   wirksam ist, entfallen die bisherigen Profile, und WirePlumber wählt HiFi.
   Laufende Streams können umziehen.
3. **Standardausgang:** Die UCM-Priorität 255 reicht möglicherweise nicht
   gegenüber anderen Karten.
4. **Speicherung der Lautstärke** der Split-Senken über Neustarts ist
   erwartet, aber nicht nachgewiesen. Ebenso unbelegt ist der Startwert neuer
   Routen.
5. **Zugriff auf SPDIF/ADAT** neben dem HiFi-Profil (über die Ports des
   verborgenen Hardwareknotens) ist unbelegt. Sicher möglich ist er über das
   Profil `pro-audio`, dann allerdings ohne die sechs Stereo-Geräte.
6. **Langname der Karte:** Enthält er nicht „Fireface 802“, greift das Profil
   nicht. Dieser Fehlerfall ist harmlos; T1 deckt ihn auf.
