# T3: Fireface 802 in PipeWire, WirePlumber und KDE Plasma

> **Stand:** Im Dev-Container mit PipeWire 1.6.2-1ubuntu1.2 und
> WirePlumber 0.5.13-1ubuntu1.1 (Ubuntu 26.04, wie auf dem Host)
> vollständig nachgewiesen (`pw_integration`, 48 Prüfungen). Auf dem Host
> **noch nicht installiert**; der Host-Test ist vorbereitet
> (`tools/hw-test/ff802-t3-host.sh`).

## Ergebnis

Das vorhandene UCM-Profil mit SplitPCM reicht aus. **Es braucht keine
eigene WirePlumber-Erweiterung und keine PipeWire-/WirePlumber-
Konfiguration.** Das Paket `ff802-ucm-profile` (ab 0.2.0) installiert
weiterhin nur die UCM-Dateien und die Gerätebindung; geändert hat sich
nur die Dokumentation.

| Ziel | Gerät in Plasma (Pulse-Name `…HiFi__<X>__sink`) | Playback | Nachweis im Container |
|------|------|----------|------|
| Analog 1/2 | Fireface 802 (…) Analog Output 1/2 (`Line1`) | 0/1 | ✓ Signal nur auf Kanal 0/1 |
| Analog 3/4 | … Analog Output 3/4 (`Line2`) | 2/3 | ✓ |
| Analog 5/6 | … Analog Output 5/6 (`Line3`) | 4/5 | ✓ |
| Analog 7/8 | … Analog Output 7/8 (`Line4`) | 6/7 | ✓ |
| Kopfhörer 9/10 (Standard) | … Headphones Output 9/10 (`Headphones1`) | 8/9 | ✓, höchste Priorität im Fireface |
| Kopfhörer 11/12 | … Headphones Output 11/12 (`Headphones2`) | 10/11 | ✓ |
| Analog IN 1/2 | … Analog Input 1/2 (`Line5`, Quelle) | Capture 0/1 | ✓ |

Die Kanalzuordnung ist die des Profils. Physisch bestätigt sind bisher nur
Analog OUT 1 → Playback 0 und OUT 2 → Playback 1 (T2).

## Wie es funktioniert

1. **WirePlumber 0.5.13** setzt für jede ALSA-Karte
   `api.alsa.split-enable = true` (`monitors/alsa.lua`,
   `applyDefaultDeviceProperties`).
2. **ACP in PipeWire 1.6.2** öffnet UCM dann mit `<<<SplitPCM=1>>>`. Die
   alsa-lib erzeugt keine dshare/dsnoop-Geräte, sondern meldet pro
   UCM-Gerät `PlaybackChannels`, `PlaybackChannelN` und
   `PlaybackChannelPosN`. ACP fasst alle Geräte, die dieselbe
   Hardware-PCM per Split nutzen, in **ein** Profil „HiFi“ mit sieben
   Mappings zusammen (`alsa-ucm.c`: „PlaybackPCM must not be the same as
   any selected device, except when both split“). Beim Probing öffnet nur
   das erste Mapping die Hardware-PCM mit 22 Kanälen.
3. WirePlumber legt pro Split-Gerät einen versteckten Hardware-Knoten an
   (`alsa_output.hw_<CardId>_0`, `Audio/Sink/Internal`, 22 Kanäle, Lautstärke
   1,0) sowie je ein Loopback. Dessen Eingang ist der sichtbare
   `Audio/Sink` mit Namen, Beschreibung und Priorität des UCM-Geräts,
   dessen Ausgang führt mit `audio.position = [AUXn,AUXm]` auf die
   Hardwarekanäle.
4. Plasma sieht über `pipewire-pulse` genau die sechs Sinks und die eine
   Quelle. Die internen 22-Kanal-Knoten werden nicht angeboten.

Alle sechs Sinks sind gleichzeitig nutzbar (geprüft: sechs Streams
zugleich, Kanäle 0–11 mit Signal, 12–21 still). Jede Anwendung lässt sich
einzeln verschieben. WirePlumber merkt sich das Ziel pro Anwendung
(`stream-properties`) über Neustart und erneutes Anstecken hinweg.

**Standardausgang:** Alle Split-Sinks haben `priority.session = 1100`.
Innerhalb des Fireface entscheidet die Port-Priorität aus UCM
(`PlaybackPriority` 255 bei Headphones1, `find-best-default-node.lua`), also
deterministisch Kopfhörer 9/10. Systemweit legt der Nutzer den Standard mit
`wpctl set-default` fest. WirePlumber speichert ihn und stellt ihn nach
Neustart und erneutem Anstecken wieder her. Ist das Fireface abgesteckt,
fällt der Standard auf das andere Gerät zurück; es wird also nicht dauerhaft
verdrängt (geprüft).

Ohne konfigurierten Standard und mit niedrigerer Priorität des internen
Geräts (im Test 1000) wird das Fireface beim Anstecken automatisch Standard.
Laufende Streams wechseln dann auf Kopfhörer 9/10. Die Prioritäten der
Host-Geräte sichert `precheck` (`before/sinks-full.txt`).

## Lautstärkesicherheit

| Frage | Befund | Nachweis |
|-------|--------|----------|
| Mit welcher Lautstärke entstehen die sechs Sinks? | 0,40 in Plasma/wpctl (kubisch) = 0,064 linear = **−23,9 dB**. Quelle: 1,0. Hardware-Knoten: 1,0 (keine Verstärkung dahinter) | `pw_integration` §2; am Fake-PCM gemessen: Spitzenwert 536830 von 8388607 = 0,0640 |
| Woher kommt der Wert? | WirePlumber-Einstellung `device.routes.default-sink-volume = 0.064`, angewendet auf die Geräteroute (`device/apply-routes.lua`). Die Route-Lautstärke wirkt als Software-Lautstärke auf die Loopback-Senke (ObjectConfig, `monitors/alsa.lua`) | Quellcode, §2 |
| Ist sie bei der Erzeugung zuverlässig begrenzt? | Ja, auch im kritischen Fall: Eine Anwendung spielt bereits auf einem anderen Gerät, und das Fireface erscheint. Der Stream wechselt auf Kopfhörer 9/10, an der Hardware kam nie mehr als 0,064 an (dreimal mit frischem Zustand) | §4 |
| Lässt sie sich nur für das Fireface niedriger einstellen? | Nicht mit vorhandenen Mechanismen. WirePlumber 0.5.13 kennt die Startlautstärke nur global (`device.routes.default-sink-volume`), eine Änderung träfe alle Geräte. Eine eigene Erweiterung ist nicht gerechtfertigt; −23,9 dB ist bereits zurückhaltend | Quellcode |
| Beeinflusst gespeicherter Zustand die Begrenzung? | **Ja.** Eine vom Nutzer geänderte Lautstärke speichert WirePlumber (`~/.local/state/wireplumber/default-routes`) und stellt sie beim nächsten Anstecken oder Neustart wieder her, auch 100 %. Nicht geänderte Ausgänge bekommen jedes Mal wieder 0,064 | §5, §8 |
| Bleibt die Einstellung nach Neustart und Anstecken erhalten? | Ja, für Lautstärke, Standardgerät und Ziel pro Anwendung | §5–§7 |

**Sonderfall Pulse-Schnittstelle (Plasma):** `pipewire-pulse` setzt die
Lautstärke über die Geräteroute nur, wenn es die aktive Route der Karte
kennt. Sonst setzt es nur die Knotenlautstärke
(`pulse-server.c`, `do_set_volume`). Unmittelbar nach einer anderen
Routenänderung kann das passieren. Diese Knotenlautstärke speichert
WirePlumber nicht; beim nächsten Anwenden der Route kommt der gespeicherte
Wert zurück. Deshalb prüft `ff802-t3-host.sh check` auch den gespeicherten
Zustand, und `limit` arbeitet mit `wpctl` und wartet, bis der gespeicherte
Zustand stimmt.

**Regel für den Host:** Vor jeder Audioausgabe muss
`ff802-t3-host.sh check` bestehen. Es bricht mit Exit 6 (SAFETY) ab, wenn
ein Fireface-Sink über 40 % steht oder für das Fireface eine höhere
Lautstärke gespeichert ist. Zusätzlich gelten die körperlichen Maßnahmen
aus [audio_ucm.md](audio_ucm.md) (Kopfhörerregler zu, Verstärker aus).

## Container-Test `pw_integration`

Voraussetzungen (Dockerfile): `pipewire`, `pipewire-bin`, `pipewire-pulse`,
`wireplumber`, `pulseaudio-utils`, `libspa-0.2-dev` aus resolute(-updates)
sowie alsa-lib 1.2.15.x. Der Test prüft die Reihen PipeWire 1.6.x und
WirePlumber 0.5.x und protokolliert die genauen Versionen. Validiert mit
1.6.2-1ubuntu1.2 / 0.5.13-1ubuntu1.1. Ohne die Pakete wird der Test
übersprungen, im Dev-Container schlägt er dann fehl („rebuild the dev
container“). Mit `-DFF802_PW_ROOT=<Verzeichnis>` lässt er sich auch gegen
einen entpackten Paketbaum ausführen.

Aufbau: eine private PipeWire-Sitzung (pipewire, wireplumber,
pipewire-pulse). Laufzeit-, Konfigurations-, Zustands- und Home-Verzeichnis
liegen unter `build/tests/audio/pw-scratch`. Der UCM-Baum ist der
Systembaum mit dem Paketaufbau (T1 `prepare --package`). Unverändert laufen
WirePlumbers ALSA-Monitor, ACP, UCM, die Loopbacks und der Graph.
Nachgebildet sind nur:

| Ersatz | Wofür |
|--------|-------|
| `fake_spa_enum_ff802.c` | ersetzt nur PipeWires udev-Erkennung. Der Container sieht in `/sys/class/sound` die Karten des Hosts, hat aber kein `/dev/snd`. Meldet hw:1, solange `enum/present` existiert (Anstecken/Abstecken) |
| `fake_ctl_ff802.c` | Kartendaten des Fireface (wie in T1) |
| `fake_pcm_ff802.c` | Hardware-PCM: 22 Kanäle S24_3LE, Zeiger im Echtzeittakt. Misst den Spitzenpegel jedes Wiedergabekanals, liefert pro Aufnahmekanal einen festen Wert |
| `LibraryConfig` in der privaten Kopie der Bindungs-Hauptdatei | alsa-lib gibt jeder UCM-Karte eine eigene Bibliothekskonfiguration mit fest eingebautem `ctl.hw`/`pcm.hw` (`type hw`), die die globale Konfiguration nicht erreicht. Der Block setzt dort `ctl_type.hw`/`pcm_type.hw` auf die Fakes. Die Profildateien bleiben unverändert |
| Null-Sink „Other device“ | Stellvertreter für die interne Soundkarte |
| Stubs für `sudo`, `apt-get`, `systemctl`, `dpkg-query` | nur für den Abschnitt mit dem Host-Skript |

Es wird nichts abgespielt oder aufgenommen. Testsignale enden im Fake-PCM.

## Im Container nicht nachweisbar

Diese Eigenschaften hängen am echten Gerät oder am echten Desktop und
bleiben für den Host-Test:

- **Physische Kanalzuordnung** außer Analog OUT 1/2: nur T2 mit Messung.
- **Echte Kartendaten:** udev-Eigenschaften (`ID_ID`, `ID_SERIAL`) und damit
  die genauen Knoten- und Kartennamen. Das Host-Skript erkennt die Geräte
  deshalb über Eigenschaften (`alsa.components`, `device.profile.name`,
  Split-Position), nicht über feste Namen.
- **Echtes USB-Timing** der 22-Kanal-PCM (Periodengröße, Xruns), auch bei
  anderen Raten als 48 kHz und mit allen Sinks gleichzeitig.
- **Ob die echte Hardware die Startlautstärke so umsetzt:** Der Fake misst
  den Digitalpegel. Der analoge Pegel hängt am Gerätemixer (Totalmix-Zustand
  im CC-Modus) und an den Kopfhörerreglern.
- **Hardware-Mixer:** Das Profil nutzt keine Mixer-Elemente, die
  Lautstärke ist Software. Ob die Karte selbst Regler meldet und ACP sie
  nutzt, zeigt erst der Host.
- **Plasma-Oberfläche:** Anzeige und Auswahl in den Systemeinstellungen und
  im Plasmoid. Im Container geprüft ist nur dieselbe Pulse-Schnittstelle,
  die Plasma benutzt.
- **Priorität der anderen Host-Geräte** und damit, ob das Fireface beim
  Anstecken ohne konfigurierten Standard automatisch Standard wird.
- **Echter Neustart** (Benutzersitzung, systemd) und **echtes
  Abstecken/Anstecken** (udev-Ereignisse, Gerätereservierung über D-Bus,
  die im Container fehlt).
- **Installation mit dpkg/apt auf dem Host** und Rücknahme mit dem
  `systemctl`-Neustart von WirePlumber.

## Host-Test T3 (vorbereitet, nicht ausgeführt)

Voraussetzung: Ihre Freigabe. Das Skript fragt vor jeder Änderung nach und
gibt selbst nie Audio aus. Ausgaben liegen in `build-t3-host/`.

```sh
cmake --build build --target deb            # build/deb/ff802-ucm-profile_0.2.0_all.deb
tools/hw-test/ff802-t3-host.sh precheck     # nur lesend; zeichnet den Ausgangszustand auf
tools/hw-test/ff802-t3-host.sh install build/deb/ff802-ucm-profile_0.2.0_all.deb
# Sicherheitsmaßnahmen, dann Fireface aus/ein (oder USB ab/an)
tools/hw-test/ff802-t3-host.sh check        # Referenz; muss vor jeder Ausgabe bestehen
tools/hw-test/ff802-t3-host.sh set-default  # Kopfhörer 9/10 als Systemstandard
tools/hw-test/ff802-t3-host.sh check
systemctl --user restart pipewire pipewire-pulse wireplumber
tools/hw-test/ff802-t3-host.sh check        # PERSISTENT nach Dienst-Neustart
# Fireface ab- und wieder anstecken
tools/hw-test/ff802-t3-host.sh check        # PERSISTENT nach erneutem Anstecken
# Neustart des Rechners
tools/hw-test/ff802-t3-host.sh check        # PERSISTENT nach Neustart
tools/hw-test/ff802-t3-host.sh rollback     # Paket entfernen, WirePlumber-Zustand zurück, prüfen
```

Was die Schritte prüfen:

- **precheck:** PipeWire 1.6.x, WirePlumber 0.5.x, alsa-lib 1.2.15.x, Dienste
  aktiv, genau ein originales Fireface 802 im CC-Modus, Paket nicht
  installiert, keine Bindung, noch keine Split-Sinks. Zeichnet auf: Dateien
  (Paket, Bindungen, Konfigurationen), andere Geräte mit Lautstärke,
  Standardgeräte, Karten und eine Kopie des WirePlumber-Zustands.
- **install:** Das Paket enthält nur die erwarteten Dateien. Hinweise zur
  Lautstärkesicherheit, Bestätigung durch Eintippen, `sudo apt-get install`,
  `ff802-ucm-bind status`.
- **check:** sechs Split-Sinks und eine Quelle mit Beschreibung und
  Split-Position; SAFETY für jede aktuelle Lautstärke und für die
  gespeicherten Fireface-Routen; Standardgerät; andere Geräte vorhanden
  (Lautstärkeänderungen werden angezeigt); Vergleich mit der Referenz.
- **limit:** senkt nur Fireface-Sinks über 40 % auf 40 % und wartet, bis
  das gespeichert ist.
- **set-default:** `wpctl set-default` für Kopfhörer 9/10; wartet, bis
  WirePlumber den konfigurierten Standard gespeichert hat (sonst ginge er
  bei einem sofortigen Neustart verloren).
- **rollback:** Bestätigung, `sudo apt-get purge`, WirePlumber anhalten,
  Zustand aus `precheck` exakt wiederherstellen, WirePlumber starten.
  Prüft danach Dateien, Prüfsummen des Zustands, dass keine Split-Sinks
  mehr da sind und dass die anderen Geräte vorhanden sind. Exit 4, wenn
  das nicht verifiziert ist. Änderungen, die Sie zwischendurch an anderen
  Geräten vorgenommen haben, gehen dabei mit zurück; die Bestätigung weist
  darauf hin.

Erst nach bestandenem `check` und mit den körperlichen Sicherheitsmaßnahmen
darf Audio ausgegeben werden, etwa für die restlichen T2-Messungen. Das
ist nicht Teil dieses Skripts.
