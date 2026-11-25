# fireface802-cc

[English](README.md) | **Deutsch**

**Das ursprüngliche RME Fireface 802 unter Linux verwenden:**
Umschaltung in den Class-Compliant-Modus, von Hand oder automatisch beim
Anschließen, und ein Audioprofil mit sechs Stereo-Ausgängen und einem
Stereo-Eingang für PipeWire.

> **Kurz gesagt:** Das Fireface 802 meldet sich ab Werk mit einem
> RME-eigenen USB-Protokoll, für das Linux keinen Treiber hat. In einer
> zweiten Betriebsart, dem *Class-Compliant-Modus*, spricht es ein
> genormtes USB-Audioprotokoll, das Linux von Haus aus versteht. Dieses
> Projekt schaltet das Gerät von Linux aus in diese Betriebsart (und
> zurück) und sorgt dafür, dass die Ausgänge in der Desktop-Audioverwaltung
> sinnvoll benannt und einzeln nutzbar sind. Auf Wunsch setzt es bei
> jeder Verbindung einen festen Grundzustand des internen Mixers
> (168 Register); es liest den Mixer nicht aus und ersetzt TotalMix FX nicht.

Getestet mit **einem** Gerät (ursprüngliches Fireface 802, nicht 802 FS)
unter Kubuntu 26.04.1 LTS. Kein Projekt von RME, siehe
[Lizenz und Hersteller](#13-lizenz-marken-und-herstellerunabhängigkeit).

## Inhalt

1. [Worum geht es?](#1-worum-geht-es)
2. [Was bedeutet Class Compliant (CC)?](#2-was-bedeutet-class-compliant-cc)
3. [Vom Programm zum Lautsprecher: USB-Audio und der interne Mixer](#3-vom-programm-zum-lautsprecher-usb-audio-und-der-interne-mixer)
4. [Die drei Pakete](#4-die-drei-pakete)
5. [Funktionsumfang und Teststand](#5-funktionsumfang-und-teststand)
6. [Bekannte Einschränkungen](#6-bekannte-einschränkungen)
7. [Sicherheitshinweise](#7-sicherheitshinweise)
8. [Installation](#8-installation)
9. [Bedienung](#9-bedienung)
10. [Fehlersuche](#10-fehlersuche)
11. [Technische Dokumentation](#11-technische-dokumentation)
12. [Entwicklung und Build](#12-entwicklung-und-build)
13. [Lizenz, Marken und Herstellerunabhängigkeit](#13-lizenz-marken-und-herstellerunabhängigkeit)

---

## 1. Worum geht es?

### Das Gerät

Das **RME Fireface 802** ist ein Audiointerface: ein externes Gerät, das
analoge und digitale Audiosignale (Mikrofone, Instrumente, Lautsprecher,
Kopfhörer) mit dem Computer verbindet. Es hat zahlreiche analoge Ein- und
Ausgänge, zwei Kopfhörerausgänge und digitale Schnittstellen. Mit dem
Computer ist es über USB oder FireWire verbunden; dieses Projekt betrifft
nur **USB**.

Im Gerät arbeitet ein eigener digitaler Mixer. Unter Windows und macOS
bedient man ihn mit der RME-Software **TotalMix FX**.

### Das Problem unter Linux

Ab Werk verwendet das Fireface 802 am USB-Anschluss ein
**herstellerspezifisches Protokoll**. RME liefert die passenden Treiber
für Windows und macOS, für Linux nicht. Linux erkennt das Gerät zwar als
USB-Gerät, kann aber keinen Ton darüber ausgeben.

Das Gerät beherrscht zusätzlich einen **Class-Compliant-Modus** (CC), in
dem Linux es mit seinem Standardtreiber betreiben kann. Diesen Modus muss
man aber erst einschalten. Laut RME-Handbuch geht das über eine Option in
TotalMix FX (also unter Windows oder macOS) oder über die optionale
Fernbedienung ARC. Wer zwischen Windows und Linux wechselt, steht daher
immer wieder vor derselben Frage: Wie bekomme ich das Gerät von Linux aus
in den richtigen Modus?

Ist der CC-Modus aktiv, erscheint das Fireface unter Linux als **eine**
Soundkarte mit 22 Wiedergabe- und 22 Aufnahmekanälen. Desktop-Programme
können damit wenig anfangen: Sie erwarten einzelne Stereo-Ausgänge wie
„Kopfhörer“ oder „Analog 1/2“.

### Was dieses Projekt löst

- **Umschalten von Linux aus:** proprietär → CC und CC → proprietär, ohne
  Windows, macOS oder Fernbedienung.
- **Automatisch beim Anschließen** (auf Wunsch): Kommt das Gerät im
  proprietären Modus an, bereitet Linux es für den CC-Modus vor.
- **Verständliche Ausgänge:** sechs Stereo-Ausgänge (Analog 1/2, 3/4, 5/6,
  7/8, Kopfhörer 9/10 und 11/12) und ein Stereo-Eingang (Analog 1/2), die
  in KDE Plasma und anderen PipeWire-Oberflächen einzeln auswählbar sind.

---

## 2. Was bedeutet Class Compliant (CC)?

### Genormte USB-Geräteklassen

Damit nicht jedes USB-Gerät einen eigenen Treiber braucht, gibt es
**USB-Geräteklassen**: öffentliche Normen, die festlegen, wie eine
bestimmte Geräteart mit dem Computer spricht. Tastaturen, Mäuse,
USB-Sticks und Webcams verwenden solche Klassen. Das Betriebssystem bringt
für jede Klasse einen fertigen Treiber mit; deshalb funktionieren diese
Geräte ohne Installation.

Für Audiogeräte gibt es die Klasse **USB Audio**. Ein Gerät, das sich an
diese Norm hält, heißt **class compliant** („klassenkonform“). Linux
betreibt solche Geräte mit dem Kernel-Treiber **`snd-usb-audio`**.

### Die beiden USB-Betriebsarten des Fireface 802

| | Proprietärer Modus | Class-Compliant-Modus |
|---|---|---|
| Protokoll | RME-eigenes USB-Protokoll | genormtes USB-Audioprotokoll |
| Treiber | RME-Treiber für Windows/macOS | unter Linux `snd-usb-audio` (Teil des Kernels) |
| Mixer-Software | TotalMix FX | keine von RME für Linux |
| USB-Kennung (VID:PID) | `2a39:3fcd` | `0424:3fdd` |
| unter Linux | als USB-Gerät sichtbar, aber **kein Ton** | Soundkarte mit 22 Ein- und Ausgabekanälen |

Die USB-Kennung ist die Nummer, mit der sich ein USB-Gerät beim Computer
meldet (Hersteller-ID : Produkt-ID). Das Fireface wechselt sie mit der
Betriebsart; daran erkennt die Software, in welchem Modus es gerade ist.
Beide Kennungen sind an unserem Gerät bestätigt.

### Wie der Ton unter Linux zu den Programmen kommt

Im CC-Modus sind drei Ebenen beteiligt:

| Ebene | Aufgabe |
|---|---|
| **`snd-usb-audio`** | Kernel-Treiber: spricht mit dem Gerät über USB |
| **ALSA** (*Advanced Linux Sound Architecture*) | die Audioschicht von Linux: macht aus dem Gerät eine Soundkarte mit Kanälen. Ein **UCM-Profil** (*Use Case Manager*) beschreibt dabei, welche Kanäle welche Buchsen sind und wie sie heißen. |
| **PipeWire** (mit WirePlumber) | der Audiodienst des Desktops: verteilt den Ton der Programme auf die Ausgänge, regelt Lautstärken, merkt sich Einstellungen. KDE Plasma und andere Oberflächen zeigen, was PipeWire anbietet. |

Ein spezieller RME-Treiber ist dafür **nicht** nötig.

### Wichtig

> **Der CC-Modus ist keine Klang- oder Qualitätseinstellung.** Er legt nur
> fest, *in welcher Sprache* Computer und Audiointerface über USB
> miteinander reden. Wandler, Vorverstärker und der interne Mixer des
> Fireface bleiben dieselben.

Was sich trotzdem unterscheidet: Im CC-Modus gibt es unter Linux kein
TotalMix FX. Der interne Mixer arbeitet weiter; dieses Projekt kann nur
einen festen Grundzustand einstellen, und nur, wenn du das einschaltest
(siehe nächster Abschnitt).

---

## 3. Vom Programm zum Lautsprecher: USB-Audio und der interne Mixer

Dass Linux das Fireface erkennt und Ausgänge anzeigt, heißt noch nicht,
dass an den Buchsen etwas ankommt. Vereinfacht nimmt der Ton diesen Weg:

```text
Linux-Anwendung (z. B. Musikplayer)
       │
       ▼
PipeWire / ALSA            ← hier wirken dieses Projekt (UCM-Profil)
       │                     und die Lautstärkeregler des Desktops
       ▼
USB-Audioübertragung       ← nur im CC-Modus möglich (ff802-cc, ff802-cc-auto)
       │
       ▼
Fireface 802
       │
       ▼
Interner digitaler Mixer   ← entscheidet, welcher USB-Kanal an welche Buchse geht
       │                     (eingestellt z. B. mit TotalMix FX, nicht von Linux)
       ▼
Physische Audioausgänge
       │
       ▼
Lautsprecher / Kopfhörer
```

Drei Dinge müssen also stimmen, und sie sind unabhängig voneinander:

1. **USB-Betriebsmodus:** Das Gerät muss im CC-Modus sein, sonst gibt es
   unter Linux keine Soundkarte. Das erledigt dieses Projekt.
2. **Audiokanäle in ALSA und PipeWire:** Die 22 USB-Kanäle müssen als
   benannte Ausgänge erscheinen und richtig zugeordnet sein. Das erledigt
   das UCM-Profil dieses Projekts.
3. **Routing im internen Mixer:** Im Fireface kommt jeder USB-Kanal
   („Software Playback“) erst über einen Mixer-Regler (*Fader*) an einen
   Hardware-Ausgang. Steht dieser Regler auf null oder ist der Kanal an
   eine andere Buchse geleitet, hört man nichts oder den Ton an der
   falschen Stelle. **Dieses Projekt stellt das nur mit der optionalen
   Mixer-Initialisierung ein** (unten), auf einen festen Grundzustand.

### Unsere Beobachtung

Bei unseren Tests hat Linux die Audiodaten nachweislich an das Fireface
übertragen; trotzdem war an den Kopfhörerausgängen zunächst nichts zu
hören (zeitweise kam der Ton stattdessen an Analog 1/2 heraus). Ursache
waren die Fader im internen Mixer für die betreffenden
Software-Playback-Kanäle. Erst nachdem diese Fader passend eingestellt
waren (mit TotalMix FX bzw. mit dem Fremdwerkzeug oscmix), war der Ton an
den gewünschten Ausgängen hörbar. Danach funktionierten die Ausgänge auch
unter Linux.

Ob die in TotalMix FX eingestellten Werte nach dem Wechsel in den
CC-Modus wirksam bleiben, ist nicht geklärt und von RME nicht
dokumentiert. Unsere Beobachtungen zeigen, dass das nicht immer der Fall
ist: Nach zwischenzeitlichem Windows-Betrieb war das Routing im CC-Modus
ein anderes, als TotalMix FX zuvor angezeigt hatte.

**Optionale Mixer-Initialisierung (ab Version 0.5.0, Paket
`ff802-cc-auto` 0.2.0, nach der Installation ausgeschaltet):** Bei jeder
Verbindung im CC-Modus setzt sie genau 168 Register des internen Mixers
(Konfiguration `ff802-mixer-init-v4`; v3 mit 38 davon am Gerät getestet, v4 im Test):

- jede Verbindung Playback p → Hardware Output o mit p ≠ o (p, o = 1…12)
  aus, 132 Verbindungen, darunter Playback 1/2 → Phones 9/10, die der
  CC-Modus setzt, und jede von TotalMix FX übrig gebliebene Querverbindung,
- Playback 1…12 → Hardware Output 1…12 auf 0 dB,
- die zwölf Ausgangspegel auf 0 dB,
- Input 9 (Mic/Inst 9) → alle zwölf Ausgänge mit 30 % (etwa −10,5 dB),
  auf beiden Kanälen jedes Paars dasselbe Monosignal.

Sonst wird nichts verändert und nichts aus dem Gerät gelesen. Einschalten
mit `ff802-cc-auto mixer-init on`. Einzelheiten:
[docs/mixer_init.md](docs/mixer_init.md).

Beobachtungen, Schlussfolgerungen und offene Fragen im Einzelnen:
[docs/mixer_routing.md](docs/mixer_routing.md).

---

## 4. Die drei Pakete

| Paket | Wofür | Enthält |
|---|---|---|
| **`ff802-cc`** | Modus **von Hand** wechseln, Gerät und Modus erkennen | `ff802-cc` (`check`, `enable`, `disable`), `ff802-detect` |
| **`ff802-cc-auto`** | CC-Modus **automatisch** beim Anschließen vorbereiten | Dienst, udev-Regel, systemd-Units, Steuerbefehl `ff802-cc-auto`, Desktop-Benachrichtigung |
| **`ff802-ucm-profile`** | sechs benannte Stereo-Ausgänge und ein Eingang im Desktop | UCM-Profil, Hilfsprogramm `ff802-ucm-bind`, udev-Regel für die Profilzuordnung |

### `ff802-cc`: manuelle Umschaltung

- `ff802-detect` listet angeschlossene Fireface 802 mit ihrem Modus auf.
  Es öffnet das Gerät nicht und braucht keine Root-Rechte.
- `sudo ff802-cc check` prüft nur lesend, ob ein Gerät im proprietären
  Modus da ist und passt. Es sendet nichts.
- `sudo ff802-cc enable` schaltet proprietär → CC und setzt danach den
  USB-Anschluss des Geräts kurz zurück, damit es sich im CC-Modus neu
  meldet (kein Umstecken nötig).
- `sudo ff802-cc disable` schaltet CC → proprietär.
- Vor jedem Wechsel: Prüfung, dass genau das richtige Gerät gemeint ist,
  Anzeige des Ziels und Bestätigung durch Eintippen von `ENABLE` bzw.
  `DISABLE`. Nichts wird wiederholt. `disable` verweigert, solange die
  Automatik an ist oder auf dem Gerät Audio läuft.

### `ff802-cc-auto`: automatische Umschaltung

- Nach der Installation ist die Automatik **aus**. Einschalten:
  `ff802-cc-auto on` (Administrator), ausschalten: `ff802-cc-auto off`,
  Zustand: `ff802-cc-auto status`.
- Ist sie an und wird das Gerät im proprietären Modus angeschlossen (oder
  ist es beim Systemstart so angeschlossen), sendet ein Systemdienst
  einmal den Umschaltbefehl und setzt danach den USB-Anschluss des Geräts
  zurück, genau wie `ff802-cc enable`; das Gerät meldet sich ohne
  Umstecken im CC-Modus (am Gerät getestet; siehe
  [docs/t4_port_reset.md](docs/t4_port_reset.md)). Nur wenn der Anschluss
  nicht zurückgesetzt werden kann oder das Gerät nicht im CC-Modus
  zurückkommt, bittet eine Desktop-Benachrichtigung, das USB-Kabel
  **kurz zu trennen und wieder einzustecken**.
- Optionale Mixer-Initialisierung, unabhängig von der Automatik und nach
  der Installation ebenfalls aus: `ff802-cc-auto mixer-init on|off|status`,
  einmalig von Hand: `ff802-cc-auto mixer-init dry-run|apply` (siehe
  [Abschnitt 3](#3-vom-programm-zum-lautsprecher-usb-audio-und-der-interne-mixer)).
- Sie schaltet **nur** in Richtung CC, nie zurück, und setzt keinen
  anderen USB-Anschluss zurück. Ein fehlgeschlagener Reset wird bis zum
  nächsten Neustart oder `ff802-cc-auto on` nicht wiederholt.
- Zusammenspiel mit `ff802-cc`: Wer das Gerät unter Linux im proprietären
  Modus haben will, schaltet zuerst die Automatik aus und dann
  `ff802-cc disable`. Sonst würde die Automatik beim nächsten Anschließen
  wieder umschalten; deshalb verweigert `disable` bei eingeschalteter
  Automatik.

### `ff802-ucm-profile`: Audioprofil

- Teilt die 22 Kanäle des Geräts in benannte Stereo-Paare auf:

  | Ausgang in Plasma | USB-Kanäle |
  |---|---|
  | Fireface 802 (…) Analog Output 1/2 | 1–2 |
  | … Analog Output 3/4 | 3–4 |
  | … Analog Output 5/6 | 5–6 |
  | … Analog Output 7/8 | 7–8 |
  | … Headphones Output 9/10 | 9–10 |
  | … Headphones Output 11/12 | 11–12 |
  | … Analog Input 1/2 (Eingang) | Aufnahme 1–2 |

- Alle sechs Ausgänge sind gleichzeitig nutzbar und pro Programm wählbar.
- Es ist keine PipeWire- oder WirePlumber-Konfiguration nötig;
  WirePlumber 0.5 übernimmt die Aufteilung selbst.
- Wegen eines Fehlers in alsa-lib vor 1.2.16 verknüpft `ff802-ucm-bind`
  das Profil mit der angeschlossenen Karte; eine udev-Regel tut das bei
  jedem Anschließen, auch an einem neuen USB-Anschluss.

### Warum drei Pakete?

Die Teile sind unabhängig voneinander und werden je nach Bedarf
installiert:

| Ich möchte … | Pakete |
|---|---|
| unter Linux Musik hören / aufnehmen, Gerät kommt schon im CC-Modus an | `ff802-ucm-profile` |
| zwischen Windows und Linux wechseln, Linux soll das Gerät selbst vorbereiten | `ff802-cc-auto` + `ff802-ucm-profile` |
| den Modus gezielt von Hand wechseln (auch zurück in den proprietären Modus) | `ff802-cc` (+ `ff802-ucm-profile` für den Ton) |
| alles | alle drei |

Kein Paket hängt von einem anderen ab, und keine Datei gehört zwei
Paketen. Die Automatik enthält absichtlich nur die eine Richtung und
keine Rückschaltung; die Rückschaltung und der Port-Reset liegen getrennt
in `ff802-cc`, das man nur mit `sudo` von Hand aufruft.

---

## 5. Funktionsumfang und Teststand

**Automatisierte Tests** laufen im Dev-Container ohne Gerät: 85 Tests mit
simuliertem USB, nachgebautem sysfs, nachgebildeter Soundkarte, einer
privaten PipeWire-Sitzung und Paketinstallation in einer isolierten
Wurzel. Sie greifen nie auf USB-Hardware oder eine echte Soundkarte zu.

**Hardwaretests** wurden von Hand am einen vorhandenen Fireface 802 unter
Kubuntu 26.04.1 durchgeführt.

| Funktion | automatisiert getestet | am Gerät getestet |
|---|:---:|---|
| Erkennung von Gerät und Modus (`ff802-detect`) | ja | ja |
| Diagnose `ff802-cc check` | ja | ja |
| `ff802-cc enable` (proprietär → CC) mit Port-Reset | ja | ja, 2026-11-19 ¹ |
| `ff802-cc disable` (CC → proprietär) | ja | ja, 2026-11-19 ¹ |
| automatische Umschaltung `ff802-cc-auto` mit Port-Reset | ja | ja, 2026-11-23 (ohne Umstecken) |
| Mixer-Initialisierung, 38 Register (`ff802-mixer-init-v3`) | ja | ja, 2026-11-23, zweimal aus- und eingeschaltet ⁴ |
| vollständige 12×12-Playback-Matrix, 168 Register (`ff802-mixer-init-v4`) | ja | ausstehend ⁵ |
| Input 9 mit 30 % auf allen zwölf Ausgängen | ja | ja, Hörtest (E1 für Analog 3, dann alle zwölf) ⁴ |
| UCM-Profil wird von alsa-lib mit der echten Karte geladen (T1) | ja | ja, 29/29 Prüfungen |
| sechs Ausgänge und ein Eingang in PipeWire/Plasma (T3) | ja | ja |
| Profilzuordnung an einem anderen USB-Anschluss | ja | ja, 2026-11-18 |
| Kanalzuordnung gemessen (T2, Kabelschleife auf Line In 1) | ja (Simulation) | nur Analog Out 1 und 2 ² |
| Wiedergabe gehört (T3, über PipeWire) | – | Analog 3/4, Kopfhörer 9/10 und 11/12 ³ |
| Analog 5/6, 7/8 und Line In 2 am Gerät | ja (Simulation) | nicht nachgewiesen ² |
| Startlautstärke 40 % für neue Ausgänge | ja | nicht einzeln dokumentiert |

¹ Getestet mit dem selbst gebauten Programm (`build-write/ff802-cc`) mit
derselben Umschaltlogik wie im Paket `ff802-cc` 0.4.0/0.4.1 (in 0.5.0
unverändert: die Zeitwerte stehen jetzt mit denselben Werten in einer
gemeinsamen Header-Datei).
² Der Messtest T2 vom 2026-11-17 hat nur Analog Out 1 und 2 nachgewiesen
(sowie Line In 1 als Aufnahmekanal 1). An Out 3, 5 und 7 wurde kein
Signal erkannt, die Messung an Out 4 wurde abgebrochen, die übrigen
Positionen wurden nicht gemessen. Das ist kein Nachweis eines Fehlers;
eine mögliche Ursache ist das Routing im internen Mixer. Das Profil
bleibt deshalb intern als „UNVERIFIED“ markiert. Protokolle und
Auswertung: [docs/audio_t2_results/](docs/audio_t2_results/README.md).
³ Hörtest über PipeWire, unabhängig von T2 und keine Messung des
Kanalindex; die Kopfhörer erst, nachdem die Software-Playback-Fader im
Gerät eingestellt waren.
⁴ Hörtests, keine Pegelmessungen. Mit nur einem Durchgang blieb Playback
9…12 → Phones 9…12 direkt nach dem Einschalten ohne Wirkung; der Dienst
sendet die Liste deshalb zweimal im Abstand von 5 s. Die Ursache ist
nicht bekannt. Einzelheiten:
[docs/mixer_v3_integration.md](docs/mixer_v3_integration.md).
⁵ Der Release-Kandidat mit v3 ließ Playback 3/4 zusätzlich auf Phones
11/12 hören (eine Querverbindung im Mixer, nicht in PipeWire); v4
schaltet alle Playback-Querverbindungen ab. Hardwaretest von v4
ausstehend: [docs/mixer_v4.md](docs/mixer_v4.md).

---

## 6. Bekannte Einschränkungen

### Nachgewiesen

- **Mixer nur als fester Grundzustand:** Der interne Mixer des Fireface
  wird nie gelesen; die optionale Initialisierung schreibt 168 feste Werte
  und sonst nichts. Dieses Projekt ist **kein Ersatz für TotalMix FX**.
- **Ohne Mixer-Initialisierung kein Routing:** Ein Ausgang kann in Plasma
  erscheinen und Ton bekommen, ohne dass an der Buchse etwas zu hören ist
  (siehe
  [Abschnitt 3](#3-vom-programm-zum-lautsprecher-usb-audio-und-der-interne-mixer)).
- **Kein Rückgängig für die Mixerwerte:** Es gibt kein Programm, das die
  zwölf Input-9-Verbindungen wieder ausschaltet; Abschalten der
  Initialisierung oder ein älteres Paket lässt sie unverändert.
- **Zweiter Durchgang ohne bekannte Ursache:** Der Dienst sendet die
  Mixerliste zweimal, weil ein einzelner Durchgang direkt nach dem
  Einschalten am Gerät nicht vollständig wirkte (Fußnote ⁴).
- **Kein eigenes Lautstärkelimit:** Neue Ausgänge starten mit 40 %
  (Vorgabe von WirePlumber). Eine einmal geänderte Lautstärke merkt sich
  WirePlumber und stellt sie wieder her, auch 100 %.
- **Nur ein Gerät getestet:** alle Hardwaretests mit genau einem Fireface
  802 (ursprüngliches Modell) an einem Rechner.
- **Nur das ursprüngliche Fireface 802:** Das Fireface 802 FS und andere
  RME-Geräte werden erkannt und **abgelehnt**. Es gibt keine
  Kompatibilitätszusage für andere Modelle.
- **Nur USB**, nicht FireWire. Fertige Pakete der Programme nur für x86-64 (amd64).

### Noch nicht untersucht oder offen

- Verhältnis der Mixer-Zustände zwischen proprietärem und CC-Modus
  (Speichern im Gerät, Verhalten nach Windows-Betrieb).
- Messung der Kanalzuordnung für alle Ausgänge außer Analog 1/2 und für
  Line In 2; T2 hat nur Analog Out 1 und 2 nachgewiesen (siehe
  Fußnote ² oben).
- Andere Linux-Distributionen und -Versionen, andere PipeWire- oder
  WirePlumber-Versionen.
- Bedeutung einzelner Werte der Umschaltbefehle: Sie wurden durch
  Beobachtung ermittelt und sind von RME nicht dokumentiert.
- Ab alsa-lib 1.2.16 ist `ff802-ucm-bind` nicht mehr nötig; mit einer
  solchen Version ist das Profil nicht am Gerät getestet.

---

## 7. Sicherheitshinweise

**Lautstärke**
- Vor dem ersten Anschließen, vor jeder Umschaltung und bevor du zum
  ersten Mal etwas abspielst: Kopfhörerregler am Gerät ganz zurück,
  Kopfhörer absetzen, Verstärker und Aktivlautsprecher leise oder aus.
- Im CC-Modus arbeitet der interne Mixer mit einem Zustand, den dieses
  Projekt nicht kennt und ohne Mixer-Initialisierung auch nicht einstellt.
  Pegel und Routing können anders sein als erwartet, auch lauter.
- Mit eingeschalteter Mixer-Initialisierung stehen die Kopfhörerausgänge
  Phones 9–12 auf 0 dB, und ein Signal an Input 9 ist auf allen zwölf
  Ausgängen zu hören; ein Mikrofon dort kann über Lautsprecher
  rückkoppeln.
- Die Lautstärke in Plasma regelt nur den digitalen Pegel. Der Mixer im
  Gerät kann dahinter zusätzlich verstärken oder abschwächen.
- Gemerkte Lautstärken (auch 100 %) stellt WirePlumber nach jedem
  Anschließen wieder her. Vor der ersten Wiedergabe nach dem Anschließen
  die Lautstärken prüfen.

**Moduswechsel unterbricht den Ton**
- Beim Wechsel meldet sich das Gerät am USB neu an. Die Soundkarte
  verschwindet aus ALSA und PipeWire und erscheint (im CC-Modus) neu.
  Laufende Wiedergaben und Aufnahmen brechen ab; Programme wechseln unter
  Umständen auf ein anderes Gerät.
- Vorher alle Wiedergaben und Aufnahmen beenden. `ff802-cc disable`
  verweigert den Wechsel, solange auf dem Gerät Audio läuft.
- Nach dem Wechsel in den proprietären Modus gibt es unter Linux keinen
  Ton mehr vom Fireface.

**Rechte**
- `ff802-cc check`, `enable` und `disable` brauchen Root-Rechte (`sudo`).
- `ff802-cc-auto on` braucht Administratorrechte (Abfrage über polkit
  oder `sudo`); `off` darf der angemeldete Benutzer ohne Passwort;
  `status` darf jeder.
- `ff802-detect` und `ff802-cc-auto status` lesen nur.

**USB-Schreibzugriffe**
- `ff802-cc enable|disable` und der Dienst von `ff802-cc-auto` senden
  Steuerbefehle an das Gerät. Die Befehle sind durch Beobachtung ermittelt
  und nicht von RME dokumentiert. Benutzung auf eigene Gefahr (siehe
  MIT-Lizenz: keine Gewährleistung). Risiken: [docs/usb_comm.md](docs/usb_comm.md).

**Zurück ohne Linux**
- TotalMix FX unter Windows oder macOS setzt ein Gerät im CC-Modus beim
  Anschließen automatisch in den Normalmodus zurück. Alternativ mit der
  Fernbedienung ARC: Prog und B (Mono) beim Einschalten halten
  ([docs/t4_disable.md](docs/t4_disable.md)).

---

## 8. Installation

### Getestete Umgebung

| | getestet |
|---|---|
| System | Kubuntu 26.04.1 LTS, KDE Plasma, x86-64 |
| Audio | alsa-lib 1.2.15.3, alsa-ucm-conf 1.2.15.3, PipeWire 1.6.2, WirePlumber 0.5.13 |
| Gerät | ein ursprüngliches RME Fireface 802 (nicht 802 FS), über USB |

Abhängigkeiten der Pakete (installiert `apt` automatisch mit):

| Paket | hängt ab von | empfiehlt |
|---|---|---|
| `ff802-cc` | `libc6` (≥ 2.34), `libusb-1.0-0` | – |
| `ff802-cc-auto` | `libc6` (≥ 2.34), `libusb-1.0-0`, `libasound2t64` (≥ 1.2.10), `systemd`, `udev` | `pkexec`, `libnotify-bin` |
| `ff802-ucm-profile` | `alsa-ucm-conf` (≥ 1.2.15.3), `libasound2t64` (≥ 1.2.15.3) | – |

### Pakete beschaffen

Die drei Pakete und die Datei `SHA256SUMS` sind als Anhänge des
GitHub-Releases **v0.5.0** veröffentlicht:
<https://github.com/bsteinheber/fireface802-cc/releases/tag/v0.5.0>

```
ff802-cc_0.5.0_amd64.deb
ff802-cc-auto_0.2.0_amd64.deb
ff802-ucm-profile_0.3.1_all.deb
SHA256SUMS
```

Sie sind reproduzierbar aus dem Tag `v0.5.0` gebaut und lassen sich auch
selbst aus diesem Repository bauen (siehe
[Entwicklung und Build](#12-entwicklung-und-build)).

Prüfsummen prüfen (im Ordner mit den Paketen und der Datei `SHA256SUMS`):

```sh
sha256sum -c SHA256SUMS
```

### Installieren

Vorher den Abschnitt [Sicherheitshinweise](#7-sicherheitshinweise) lesen.

**1. Audioprofil** (am besten mit angeschlossenem Gerät im CC-Modus):

```sh
sudo apt install ./ff802-ucm-profile_0.3.1_all.deb
sudo ff802-ucm-bind status
```

Danach das Fireface einmal trennen und wieder anschließen. Ohne
angeschlossenes Gerät übernimmt die udev-Regel die Zuordnung beim
nächsten Anschließen.

**2. Manuelle Umschaltung** (optional):

```sh
sudo apt install ./ff802-cc_0.5.0_amd64.deb
ff802-cc --version
```

**3. Automatik** (optional):

```sh
sudo apt install ./ff802-cc-auto_0.2.0_amd64.deb
ff802-cc-auto status       # nach der Installation: aus
ff802-cc-auto on           # einschalten (fragt nach dem Administratorpasswort)
ff802-cc-auto mixer-init status   # optionale Mixer-Initialisierung: nach der Installation aus
```

Bei der Installation laden die Pakete nur die Regeln von udev und die
Konfiguration von systemd neu. Sie starten keine Dienste neu und schalten
das Gerät nicht um.

### Installation prüfen

```sh
dpkg -l 'ff802-*'
ff802-cc --version         # ff802-cc 0.5.0 (git <Revision>, Release, USB writes enabled)
ff802-cc-auto status
sudo ff802-ucm-bind status
```

### Aktualisieren und entfernen

```sh
sudo apt install ./<paket>_<neue Version>_<arch>.deb   # Aktualisieren, Einstellungen bleiben
sudo apt remove ff802-cc
sudo apt purge ff802-cc-auto          # entfernt auch den Zustand der Automatik
sudo apt remove ff802-ucm-profile     # entfernt Profil, udev-Regel und Zuordnungen
```

Das Entfernen ändert den USB-Modus des Geräts nicht. Einzelheiten:
[docs/packages.md](docs/packages.md) und die `README.Debian` der Pakete
(`/usr/share/doc/<paket>/README.Debian`).

---

## 9. Bedienung

### Welcher Modus ist aktiv?

```sh
ff802-detect
```

```
Scanned 26 USB device(s).
Bus 003 Device 018: ID 2a39:3fcd mode=proprietary port=3-2.4.1.1.3 bcdDevice=0.01
  Fireface 802, proprietary USB mode
Found 1 Fireface 802 device(s).
```

`mode=proprietary` oder `mode=class-compliant`. Öffnet das Gerät nicht,
ändert nichts. Rückgabewert 0: Gerät gefunden, 1: keins gefunden.

### Von Hand in den CC-Modus wechseln

```sh
sudo ff802-cc check        # optional: nur prüfen, sendet nichts
sudo ff802-cc enable       # zeigt Ziel und Befehl, dann ENABLE eintippen
```

Wirkung: Das Gerät bekommt den Umschaltbefehl, danach wird sein
USB-Anschluss etwa 2 Sekunden abgeschaltet und wieder eingeschaltet. Es
meldet sich als `0424:3fdd` neu an; mit `ff802-ucm-profile` erscheinen die
sechs Ausgänge in Plasma. Klappt der Port-Reset nicht, sendet `enable`
nichts und nennt die Alternative:

```sh
sudo ff802-cc --no-port-reset enable   # danach USB-Kabel selbst kurz trennen
```

### Von Hand in den proprietären Modus wechseln

```sh
ff802-cc-auto off          # nur falls ff802-cc-auto installiert und an ist
sudo ff802-cc disable      # zeigt Ziel und Befehl, dann DISABLE eintippen
```

Wirkung: Die Soundkarte des Fireface verschwindet sofort aus ALSA und
PipeWire; das Gerät meldet sich als `2a39:3fcd` neu an. Unter Linux gibt
es danach keinen Ton mehr vom Fireface, unter Windows/macOS arbeitet es
wieder mit dem RME-Treiber.

Sind mehrere passende Geräte angeschlossen, wählt man eines mit
`--device BUS:ADDRESS` aus (Werte aus `ff802-detect`).

### Automatik

```sh
ff802-cc-auto status       # Zustand, angeschlossenes Gerät, letztes Ergebnis
ff802-cc-auto on           # einschalten (Administrator)
ff802-cc-auto off          # ausschalten (angemeldeter Benutzer)
```

Der Zustand bleibt über Neustarts erhalten. `off` verhindert nur weitere
automatische Umschaltungen; es ändert den Modus des Geräts nicht.

### Audioausgänge prüfen

- In KDE Plasma: Systemeinstellungen → Audio bzw. das Lautstärke-Symbol.
  Dort erscheinen „Fireface 802 (…) Analog Output 1/2“ bis „Headphones
  Output 11/12“ und „Analog Input 1/2“.
- Im Terminal:

  ```sh
  wpctl status               # Geräte und Ausgänge in PipeWire
  pactl list cards           # das Fireface mit dem Profil "HiFi"
  sudo ff802-ucm-bind status # ist das Profil der Karte zugeordnet?
  ```

---

## 10. Fehlersuche

| Beobachtung | wahrscheinliche Ursache | was tun |
|---|---|---|
| `ff802-detect` zeigt `mode=proprietary`, kein Ton | Gerät nicht im CC-Modus | `sudo ff802-cc enable` oder Automatik |
| Automatik an, aber Gerät bleibt proprietär | Port-Reset nicht möglich oder nicht bestätigt (Benachrichtigung), oder Kabel danach nicht getrennt | Kabel kurz trennen; `ff802-cc-auto status`, `journalctl -t ff802-cc-auto -b` |
| Fireface in Plasma als eine Karte ohne die sechs Ausgänge | Profil nicht zugeordnet | `sudo ff802-ucm-bind status`, `journalctl -b -t ff802-ucm-bind`; ggf. `sudo ff802-ucm-bind bind` und Gerät neu anschließen |
| Ausgang sichtbar, Pegel läuft, aber **nichts zu hören** | Routing/Fader im internen Mixer | Routing und Fader im Gerät prüfen (TotalMix FX bzw. oscmix), siehe [docs/mixer_routing.md](docs/mixer_routing.md) |
| Ton am falschen Ausgang | Routing im internen Mixer | wie oben |
| `ff802-cc disable` verweigert | Automatik an oder Audio läuft | `ff802-cc-auto off`, Wiedergabe beenden |
| `ff802-cc`: `access to the USB device denied` | fehlende Rechte | mit `sudo` aufrufen |
| `ff802-cc`: Exit 6 | gesendet, neuer Modus nicht erkannt | Kabel kurz trennen, dann `ff802-detect` |

Rückgabewerte von `ff802-cc`:

| Exit | Bedeutung |
|:---:|---|
| 0 | erfolgreich (bei `enable`/`disable`: neuer Modus erkannt) |
| 1 | kein passendes Gerät, oder mehrere |
| 2 | Bedienfehler, kein Terminal |
| 3 | Vorbedingung nicht erfüllt, nichts gesendet |
| 4 | Übertragung fehlgeschlagen, Gerätezustand unbekannt |
| 5 | nicht gesendet (abgebrochen, Schreiben im Build gesperrt) |
| 6 | gesendet, aber neuer Modus nicht erkannt |

Weitere Diagnose der Automatik: `systemctl list-units --all 'ff802-cc-auto@*' 'ff802-cc-auto-enable@*'`,
`journalctl --user -t ff802-cc-notify -b` (Benachrichtigungen).

---

## 11. Technische Dokumentation

Die Dokumente unter `docs/` sind auf Deutsch.

| Thema | Dokument |
|---|---|
| Registerprotokoll, Steuerbefehle | [docs/protocol.md](docs/protocol.md) |
| Geräteerkennung | [docs/detection.md](docs/detection.md) |
| USB-Kommunikation, Sicherheitskonzept, Risiken | [docs/usb_comm.md](docs/usb_comm.md) |
| `ff802-cc disable` und Port-Reset | [docs/cc_disable_port_reset.md](docs/cc_disable_port_reset.md) |
| Rückweg in den proprietären Modus (Recherche) | [docs/t4_disable.md](docs/t4_disable.md) |
| Automatik: Architektur, Sicherheit, Diagnose, Rückbau | [docs/t4_usb_mode.md](docs/t4_usb_mode.md) |
| Automatischer Port-Reset nach dem Umschaltbefehl | [docs/t4_port_reset.md](docs/t4_port_reset.md) |
| UCM-Profil, Host-Test T1, Lautstärkesicherheit | [docs/audio_ucm.md](docs/audio_ucm.md) |
| Einbindung des UCM-Profils (Varianten, alsa-lib-Fehler) | [docs/audio_ucm_einbindung.md](docs/audio_ucm_einbindung.md) |
| Kanalzuordnung, Messwerkzeug `ff802-chanmap` (T2) | [docs/audio_t2_plan.md](docs/audio_t2_plan.md) |
| Ergebnisse von T2 (Originalprotokolle, Auswertung) | [docs/audio_t2_results/](docs/audio_t2_results/README.md) |
| PipeWire, WirePlumber, Plasma (T3) | [docs/audio_t3.md](docs/audio_t3.md) |
| Profilzuordnung an jedem USB-Anschluss | [docs/audio_t3_usb_port.md](docs/audio_t3_usb_port.md) |
| Pakete, Versionen, Installationsstand | [docs/packages.md](docs/packages.md) |
| Interner Mixer, Software-Playback-Fader, Beobachtungen | [docs/mixer_routing.md](docs/mixer_routing.md) |
| Mixer-Initialisierung (168 Register), Registerliste | [docs/mixer_init.md](docs/mixer_init.md) |
| Hardwaretests B.4, D1, D3, E1; Integrationstest v3 | [docs/mixer_b4.md](docs/mixer_b4.md), [docs/mixer_d1.md](docs/mixer_d1.md), [docs/mixer_d3.md](docs/mixer_d3.md), [docs/mixer_e1.md](docs/mixer_e1.md), [docs/mixer_v3_integration.md](docs/mixer_v3_integration.md), [docs/mixer_v4.md](docs/mixer_v4.md) |
| Release-Notizen v0.5.0 | [docs/release-notes-v0.5.0.md](docs/release-notes-v0.5.0.md) |
| Aufbau des Quellcodes, Build-Optionen, Tests, Entwicklungsphasen | [docs/development.md](docs/development.md) |

Die Namen T1–T4 bezeichnen die Testphasen der Entwicklung: T1 Laden des
UCM-Profils, T2 Kanalzuordnung, T3 Integration in PipeWire/Plasma, T4
USB-Modusverwaltung.

---

## 12. Entwicklung und Build

Entwickelt und gebaut wird in einem **Dev-Container** (`.devcontainer/`)
mit allen Werkzeugen; auf dem Rechner mit dem Fireface werden keine
Entwicklungswerkzeuge gebraucht.

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure     # 85 Tests, ohne Hardware
```

Im Standard-Build kann **kein Programm Daten an das Gerät senden**. Die
Schreibfunktion wird nur mit `-DFF802_ENABLE_USB_WRITE=ON` einkompiliert;
die Pakete `ff802-cc` und `ff802-cc-auto` sind so gebaut.

Debian-Pakete (unter `build/deb/`, reproduzierbar):

```sh
cmake --build build --target deb        # ff802-ucm-profile
cmake --build build --target deb-auto   # ff802-cc-auto
cmake --build build --target deb-cc     # ff802-cc, nur aus einem sauberen Commit
```

Details zu Aufbau, Build-Optionen, Tests und Paketierung:
[docs/development.md](docs/development.md), [docs/packages.md](docs/packages.md).

---

## 13. Lizenz, Marken und Herstellerunabhängigkeit

| Dateien | Lizenz | Copyright |
|---|---|---|
| alle Dateien, soweit unten nicht anders angegeben (Quellcode, Skripte, Tests, Paketierung, Dokumentation) | MIT, siehe [LICENSE](LICENSE) | 2026 Bernhard Steinheber |
| [audio/ucm2/](audio/ucm2/) (UCM-Profil) | BSD-3-Clause, siehe [LICENSES/BSD-3-Clause.txt](LICENSES/BSD-3-Clause.txt) | 2026 Bernhard Steinheber |
| [packaging/ff802-ucm/longname-master.conf](packaging/ff802-ucm/longname-master.conf) | BSD-3-Clause | 2026 Bernhard Steinheber; 2019-2026 Advanced Linux Sound Architecture (ALSA) project |

- Das UCM-Profil steht unter derselben Lizenz wie `alsa-ucm-conf`.
  `longname-master.conf` enthält Abschnitte aus `USB-Audio.conf` von
  `alsa-ucm-conf` und trägt deshalb zusätzlich den Copyright-Hinweis des
  ALSA-Projekts.
- Die Debian-Pakete enthalten dieselbe Zuordnung in
  `/usr/share/doc/<paket>/copyright` (Format DEP-5).
- Die Dokumentation zitiert kurze, belegte Stellen aus dem RME-Handbuch
  und aus Forenbeiträgen. Diese Zitate bleiben Eigentum ihrer Urheber
  und fallen nicht unter die MIT-Lizenz.
- Die Dateien in `tests/audio/fixtures/t1/` sind Ausgaben von `alsaucm`
  für dieses Profil.

**Herstellerunabhängigkeit:** Dieses Projekt steht in keiner Verbindung zu
RME und wird von RME weder unterstützt noch geprüft. Die Umschaltbefehle
stammen aus eigener Beobachtung, nicht aus Unterlagen von RME.

**Marken:** RME, Fireface und TotalMix sind Marken ihrer jeweiligen
Inhaber. Sie werden hier nur verwendet, um die unterstützte Hardware zu
benennen.
