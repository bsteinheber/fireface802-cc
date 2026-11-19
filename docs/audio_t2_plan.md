# Hardwaretest T2: Kanalzuordnung mit `ff802-chanmap`

> **Stand:** Werkzeug implementiert und im Container geprüft. **T2 wurde
> laut Rückmeldung vom 2026-11-17 erfolgreich ausgeführt.** Die
> Messergebnisse sind nicht im Repository dokumentiert, und die
> Markierung `FF802-CHANNEL-MAP: UNVERIFIED` im Profil ist noch nicht auf
> `VERIFIED` umgestellt. Der folgende Plan beschreibt den Test, wie er vor
> der Ausführung festgelegt wurde.

## Ziel

Belegen, welcher **USB-Kanalindex** des 22-Kanal-Geräts an welcher
**physischen Buchse** ankommt, für Wiedergabe und Aufnahme. Danach steht
fest, ob die Kanalpaare im Profil stimmen. Bei Übereinstimmung wird
`FF802-CHANNEL-MAP: UNVERIFIED` zu `VERIFIED`, sonst wird das Profil
korrigiert.

| Profilgerät | Erwartete Kanäle | Kabelposition in `ff802-chanmap` |
|---|---|---|
| Line1–Line4 | Wiedergabe 0/1, 2/3, 4/5, 6/7 | `out1` … `out8` (je ein Kanal) |
| Headphones1 | Wiedergabe 8/9 | `hp1` |
| Headphones2 | Wiedergabe 10/11 | `hp2` |
| Line5 | Aufnahme 0/1 | `--input 1`, `--input 2` |

T1 hat nachgewiesen, dass UCM genau diese Indizes an PipeWire weitergibt.
Offen ist allein die Zuordnung zwischen Index und Buchse im Gerät.

## Prinzip

1. **Elektrische Schleife statt Hören.** Am Fireface hängt **nichts**, was
   Schall erzeugt. Ein Patchkabel verbindet einen Ausgang mit einem
   rückwärtigen Line-Eingang. `ff802-chanmap` spielt nacheinander auf den
   Wiedergabe-Indizes 0–11, nimmt alle 22 Eingänge auf und stellt fest, wo
   das Signal ankommt.
2. **Kein Eingriff in PipeWire oder WirePlumber.** Das Werkzeug startet
   `pw-reserve -n Audio<N> -r` selbst als Kindprozess, wobei `<N>` die zur
   Laufzeit ermittelte Kartennummer ist. WirePlumber 0.5.13 reserviert die
   Karte mit Priorität −20, pw-reserve mit 0. WirePlumber gibt die Karte
   deshalb frei und übernimmt sie wieder, sobald `ff802-chanmap` die
   Reservierung beendet. Es gibt keinen Profilwechsel und keine geänderte
   Datei.
3. **Direkter Zugriff auf `hw:CARD=<id>,DEV=0`.** Ohne `plug`, ohne
   Software-Lautstärke und ohne automatische Verstärkung. Der Pegel hängt
   nur vom Werkzeug ab.

## Signal und Auswertung

- **Ablauf je Wiedergabe-Index:** 0,30 s Stille (Rauschreferenz), 0,50 s
  Chirp 300–3000 Hz, 0,30 s Stille, 1,50 s Sinus 1 kHz, 0,50 s Stille
  (Prüfung des Ausklangs). Das sind 3,1 s je Index und 37,2 s je
  Kabelposition.
- **Pegel:** Spitze 0,0099 (−40,09 dBFS). Die Obergrenze 0,01 (−40 dBFS)
  ist eine Konstante. Es gibt keine Option für Pegel, Gerät oder Kanalzahl.
- **Erkennung:** Goertzel bei 1 kHz. Als Treffer gilt ein Pegel über
  −100 dBFS bei mindestens 20 dB Abstand zur Rauschreferenz.
- **Latenz und Polarität:** aus der Kreuzkorrelation mit dem Chirp, gesucht
  bis 250 ms.
- **Kopfhörer am symmetrischen Line-Eingang:** Der Eingang sieht Spitze
  minus Ring, also links mit `+` und rechts mit `−`. Daran werden links und
  rechts unterschieden.

## Schutzmaßnahmen und ihre Tests

| Gefahr | Schutz im Werkzeug | Nachweis (Container) |
|---|---|---|
| Lautes Signal auf Lautsprecher oder Kopfhörer | Ohne `--confirm-loopback-only` wird nichts ausgegeben; das Werkzeug zeigt dann die Prüfliste. Physisch sind während T2 keine Wandler angeschlossen. | `chanmap_cli_needs_confirm` |
| Zu hoher Ausgangspegel durch einen Software-Fehler | Doppelter Wächter: Jeder float-Puffer muss ≤ 0,01 sein, sonst Abbruch ohne Ausgabe. Nach der Umwandlung muss jeder Ganzzahlwert ≤ 0,01 × Vollaussteuerung sein, sonst `-EDOM`. NaN und ∞ werden abgewiesen. | `test_chanmap_dsp`, `test_chanmap_alsa_null`, `check_chanmap_sources`; Mutationen „Wächter aus“ und „Pegel −26 dBFS“ werden erkannt |
| Pegel per Option änderbar | Es gibt keine solche Option. | `chanmap_cli_no_level_option`, `check_chanmap_sources` |
| Verstärkung oder Routing wird verändert | Mixer-Regler werden nur gelesen. Das Programm importiert keine Schreibfunktionen (`snd_ctl_elem_write`, `snd_mixer_*` usw.). Der Mixer-Zustand wird vor und nach der Messung verglichen; eine Abweichung macht das Ergebnis ungültig. | `chanmap_binary_symbols`, `test_chanmap_sim` (Szenario 14) |
| Unerwartet hohe Verstärkung im Gerät | Jede Aufnahmeprobe über −20 dBFS bricht sofort ab. | `test_chanmap_sim` (8, 10) |
| Rückkopplung über den internen Mixer | Abbruch, wenn der Ton nach seinem Ende nicht um 30 dB abklingt oder wenn breitbandig etwas nachklingt. | `test_chanmap_sim` (9, 10) |
| Abbruch durch den Bediener | Ctrl+C, SIGTERM oder SIGHUP: Nach spätestens einer Periode (21 ms) wird kein Testsignal mehr erzeugt. Was schon im ALSA-Puffer liegt (höchstens 4096 Frames = 85 ms, −40 dBFS), läuft noch aus. Danach werden 4 Perioden Stille geschrieben, das Gerät geschlossen und die Reservierung beendet. Ergebnis mit Status „aborted“, Exit 130. | `test_chanmap_sim` (11) |
| Ein-/Ausgabefehler (Underrun, Abstecken) | Abbruch ohne weitere Schreibvorgänge, Gerät geschlossen. | `test_chanmap_sim` (12, 13) |
| Falsches Gerät | Nur die eine Karte mit `usbid` `0424:3fdd`, Langname „Fireface 802“ und ohne „802 FS“. Bei mehreren passenden Karten Abbruch. Vor dem Öffnen der PCM-Geräte muss das Steuergerät Treiber `USB-Audio`, Komponente `USB0424:3fdd` und den Langnamen melden. Die Karten-ID darf nur Buchstaben, Ziffern und `_` enthalten, damit kein anderer Gerätename eingeschleust werden kann. Die Quellen enthalten keine Namen wie `default`, `plughw`, `pulse` usw. | `test_chanmap_card`, `test_chanmap_alsa_null`, `check_chanmap_sources` |
| PipeWire gibt die Karte nicht frei | Bis zu 10 s wird erneut versucht, danach Exit 5 mit Hinweis. Endet pw-reserve vorzeitig, folgt sofort Exit 5. | `test_chanmap_sim` (15–19) |
| Reservierung bleibt hängen | pw-reserve läuft in einer eigenen Prozessgruppe, sodass Ctrl+C nur `ff802-chanmap` erreicht. Es wird mit SIGINT beendet, nach 3 s mit SIGKILL. Stirbt `ff802-chanmap`, beendet `PR_SET_PDEATHSIG` die Reservierung. | `test_chanmap_reserve` |
| Phantomspeisung | Nur rückwärtige Line-Eingänge verwenden, nie die Mikrofoneingänge 9–12. Keine 48-V-Anzeige darf leuchten. Das steht in der Prüfliste des Werkzeugs. | – (Bedienung) |
| Streams ziehen auf die Laptop-Lautsprecher | Vorher alle Audioprogramme beenden. Das steht in der Prüfliste. | – (Bedienung) |

Die Tests über die Kommandozeile laufen nur bis vor die Kartensuche. Sie
sind deshalb auch auf einem Host mit angeschlossenem Fireface harmlos.

## Werkzeug

Gebaut im Dev-Container, ausgeführt auf dem Host als normaler Benutzer
(Root wird abgewiesen):

```sh
build/ff802-chanmap check
build/ff802-chanmap measure --position out1 [--input 1|2] --confirm-loopback-only [--out build-t2]
build/ff802-chanmap report [--out build-t2]
```

- **`check`:** nur lesend. Zeigt Karte, Identität aus dem Steuergerät,
  Zustand von Wiedergabe und Aufnahme, Stream-Formate, alle Mixer-Regler
  und ob `pw-reserve` vorhanden ist. Öffnet kein PCM-Gerät.
- **`measure`:** eine Kabelposition. Schreibt
  - `build-t2/<pos>-in<n>.txt`, die Ergebnisdatei, auch bei einem Abbruch,
    dann mit Status
  - `build-t2/<pos>-in<n>.log` mit vollständigem Ablauf, Mixer-Zustand und
    Treffern
  - `build-t2/pw-reserve.log`
- **`report`:** vergleicht alle Ergebnisdateien mit dem Profil und gibt
  eine Tabelle sowie ein Urteil aus: `VERIFIED`, `MISMATCH` oder
  `INCOMPLETE`.

| Exit | Bedeutung |
|---|---|
| 0 | Messung vollständig, bzw. `report`: `VERIFIED` |
| 1 | `report`: `MISMATCH` |
| 2 | Bedienfehler, fehlende Bestätigung, Root, mehrere Karten |
| 3 | kein originales Fireface 802 |
| 4 | Sicherheitsabbruch (Eingangspegel, Rückkopplung, Ausgangswächter, Mixer verändert) |
| 5 | Gerät blieb belegt oder Reservierung gescheitert |
| 6 | Audiofehler (Öffnen, Ein-/Ausgabe) |
| 7 | `report`: `INCOMPLETE` |
| 130 | abgebrochen |

## Ablauf T2 (nach Freigabe)

**0. Vorprüfung, nur lesend:** `build/ff802-chanmap check`. Erwartet wird
die Karte `F80224047128` und eine Identität ohne Abweichung; vermerken,
welches Format das Gerät nennt (vermutlich S24_3LE). Außerdem eine Kopie von
`~/.local/state/wireplumber/` und die Ausgabe von `wpctl status` für den
Vergleich danach.

**1. Am Gerät:**
- alle Lautsprecher, Endstufen und Kopfhörer abstecken
- Kopfhörerregler auf Minimum
- keine 48-V-Anzeige leuchtet
- alle Audioprogramme beenden
- ein symmetrisches Klinkenkabel (TRS–TRS) von **Analog Out 1** auf
  **Line In 1**

**2. Messungen**, jeweils mit `--confirm-loopback-only`:

| Kabel | Befehl |
|---|---|
| Out 1 … Out 8 → Line In 1 | `measure --position out1` … `out8` |
| Kopfhörer 1 → Line In 1 (Regler etwa Mitte, keine Kopfhörer) | `measure --position hp1` |
| Kopfhörer 2 → Line In 1 | `measure --position hp2` |
| Out 1 → **Line In 2** | `measure --position out1 --input 2` |

**3. Auswertung:** `build/ff802-chanmap report` liefert `VERIFIED`,
`MISMATCH` mit Begründung je Position oder `INCOMPLETE`.

**4. Rückbau:**
- Nichts zu tun: `ff802-chanmap` beendet die Reservierung selbst.
- `wpctl status` zeigt das Fireface wieder mit dem bisherigen Profil.
- `~/.local/state/wireplumber/` mit der Sicherung vergleichen.
- Erst dann die Lautsprecher wieder anschließen.

**5. Abschluss im Container:**
- bei `VERIFIED`: Markierung auf `VERIFIED` setzen
- bei `MISMATCH`: Profil korrigieren, T1 wiederholen

## Was T2 nicht prüft

- PipeWire und KDE, Prioritäten, Lautstärken: Das ist **T3**, siehe
  [audio_ucm.md](audio_ucm.md#ausblick-t3-sitzungstest).
- AES/EBU und ADAT: nicht Teil des Profils.
- Wiedergabe-Indizes 12–21 werden nicht angesteuert.

## Grenzen der Container-Prüfung

- **Simuliertes Gerät:** Das simulierte Gerät bildet Zuordnung, Latenz,
  Rauschen, Rückkopplung und Fehler nach, nicht aber das Zeitverhalten
  eines echten USB-Geräts.
- **ALSA-Backend:** Es ist gegen das ALSA-Plugin `null` geprüft. Das
  unterstützt `snd_pcm_link` nicht, deshalb läuft dort der Rückfallweg, bei
  dem die Aufnahme getrennt gestartet wird. Ob `snd_pcm_link`, Format,
  Periode und Puffer am Fireface wie erwartet funktionieren, zeigt erst
  `check` bzw. die erste Messung auf dem Host.
- **Reservierung:** Das Zusammenspiel mit der echten D-Bus-Reservierung ist
  aus den Quellen von WirePlumber 0.5.13 und PipeWire 1.6.2 abgeleitet und
  mit einem nachgebauten pw-reserve geprüft, nicht mit dem echten Dienst.
