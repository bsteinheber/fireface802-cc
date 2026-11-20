# Hardwaretest T2: Ergebnisse vom 2026-11-17

Dieses Verzeichnis enthält die **unveränderten Originalprotokolle** des
Hardwaretests T2 (Kanalzuordnung). Sie stammen aus `build-t2/` auf dem
Entwicklungsrechner und wurden am 2026-11-20 bytegleich übernommen (Liste
und Prüfsummen unten). Den Testplan beschreibt
[../audio_t2_plan.md](../audio_t2_plan.md).

> **Kurz:** T2 hat **nur Analog Out 1 und Out 2** erfolgreich
> nachgewiesen: Wiedergabe-Index 0 → Out 1 und Index 1 → Out 2, jeweils
> gemessen an Line In 1. An Out 3, 5 und 7 wurde kein Signal erkannt, die
> Messung an Out 4 wurde abgebrochen, alle anderen Positionen wurden nicht
> gemessen. Die Markierung `FF802-CHANNEL-MAP: UNVERIFIED` im Profil
> bleibt deshalb bestehen.

## Messaufbau

- **Gerät:** ursprüngliches RME Fireface 802 (24047128) im CC-Modus,
  ALSA-Karte `F80224047128` am USB-Port `usb-0000:00:14.0-2.4.1.1.3`,
  High Speed; Kubuntu 26.04.1.
- **Werkzeug:** `ff802-chanmap 0.3.0 measure` aus dem Entwicklungs-Build,
  direkt auf `hw:CARD=F80224047128,DEV=0`, S24_3LE, 48000 Hz, 22 Kanäle,
  Periode 1024, Puffer 4096. Die Karte wurde jeweils mit
  `pw-reserve -n Audio1 -r` von PipeWire übernommen und danach
  zurückgegeben (`pw-reserve.log`).
- **Signal:** nacheinander auf den Wiedergabe-Indizes 0–11 Chirp und
  1 kHz-Sinus, Spitze −40,09 dBFS (Grenze −40 dBFS). Erkennung über
  Goertzel bei 1 kHz, Treffer bei mindestens 20 dB über der
  Rauschreferenz (Einzelheiten im Testplan).
- **Schleife:** ein Patchkabel vom jeweiligen Ausgang auf **Line In 1**
  (`input 1` = Aufnahme-Index 0). Lautsprecher und Kopfhörer waren nach
  Plan nicht angeschlossen.
- **ALSA-Mixer vor jeder Messung** (nur gelesen):
  `'Playback Channel Map' 0: 0 0`, `'PCM Playback Volume' 0: 60`. Der
  interne DSP-Mixer des Fireface (Routing, Fader) ist über ALSA nicht
  sichtbar und wurde nicht erfasst.

## Ergebnisse

| Uhrzeit | Position | erwartet | Ergebnis | Treffer | max. Eingang |
|---|---|---|---|---|---|
| 12:52 | Out 1, **ohne Kabel** (Gegenprobe) | – | ok | keiner (wie erwartet) | −55,90 dBFS |
| 12:53 | Out 1 → In 1 | Index 0 | ok | **Index 0 → Capture 0**, −48,53 dBFS, SNR 97,7 dB, Latenz 86,19 ms, Polarität + | −43,88 dBFS |
| 12:56 | Out 2 → In 1 | Index 1 | ok | **Index 1 → Capture 0**, −48,53 dBFS, SNR 101,0 dB, Latenz 86,19 ms, Polarität + | −43,88 dBFS |
| 13:42 | Out 5 → In 1 | Index 4 | ok, 12/12 Schritte | kein Signal erkannt | −59,80 dBFS |
| 13:44 | Out 7 → In 1 | Index 6 | ok, 12/12 Schritte | kein Signal erkannt | −60,35 dBFS |
| 13:53 | Out 3 → In 1 | Index 2 | ok, 12/12 Schritte | kein Signal erkannt | −60,79 dBFS |
| 13:54 | Out 4 → In 1 | Index 3 | **abgebrochen** (Signal) in Schritt 11 von 12 | kein Signal erkannt | −60,19 dBFS |

Nicht gemessen: Out 6, Out 8, Kopfhörer 1 (`hp1`), Kopfhörer 2 (`hp2`),
Out 1 → Line In 2. Eine Auswertung mit `ff802-chanmap report` liegt nicht
vor. Zur Gegenprobe ohne Kabel gibt es nur die Ergebnisdatei; ihr
Ablaufprotokoll wurde von der folgenden Messung `out1-in1.log`
überschrieben.

## Was die Ergebnisse belegen

- **Belegt:** Wiedergabe-Index 0 kommt an Analog Out 1 an, Index 1 an
  Analog Out 2, wie im Profil (`Line1` = Analog Output 1/2). Die
  Gegenprobe ohne Kabel zeigt, dass die Treffer vom Kabel stammen.
- **Belegt:** Line In 1 ist Aufnahme-Index 0.
- **Nicht belegt:** die Zuordnung aller übrigen Ausgänge und von Line In 2.

## Grenzen der Aussagekraft

Dass an Out 3, 5 und 7 (und bis zum Abbruch an Out 4) **kein Signal
erkannt** wurde, ist **kein Nachweis**, dass diese Ausgänge unter Linux
nicht funktionieren oder dass das Profil falsch ist. Ebenso wenig ist es
ein Nachweis für eine andere Zuordnung. Aus den Protokollen allein lässt
sich die Ursache nicht bestimmen. Denkbar sind unter anderem:

- **Routing im internen Mixer des Fireface:** Ein Wiedergabekanal
  erreicht eine Buchse nur über einen geöffneten Fader im Gerät (siehe
  [../mixer_routing.md](../mixer_routing.md)). Dieser Zustand wurde in T2
  nicht erfasst. Im CC-Modus ist laut RME-Handbuch Analog 1/2 das
  Standardziel der Wiedergabe.
- **Messaufbau:** Steckposition oder Kabel, die in den Protokollen nicht
  festgehalten sind.
- Beim Abbruch an Out 4 liegt nur die Meldung „abort requested at step 11“
  vor; der Grund (z. B. Strg+C) ist nicht protokolliert.

Unabhängig von T2 wurden in **T3** Analog 3/4, Kopfhörer 9/10 und
Kopfhörer 11/12 unter Kubuntu mit **realer Wiedergabe** erfolgreich
gehört, die Kopfhörer nach Korrektur der Software-Playback-Fader in
TotalMix FX ([../audio_t3.md](../audio_t3.md)). Das ist ein Hörtest über
PipeWire, keine Messung des Kanalindex; die T2-Messwerte ändert er nicht.

## Dateien

| Datei | Inhalt | SHA-256 |
|---|---|---|
| `out1-in1-ohne-kabel.txt` | Ergebnis Gegenprobe ohne Kabel | `1191344316d66a970d64ca705a9b17545465969bc9d5f998a4fcc354daee8503` |
| `out1-in1.log` | Ablauf Out 1 → In 1 | `3adb7af0b90403f9e165ec482386e9dd9f79ff1f9cd37749632ad2c8de53bf34` |
| `out1-in1.txt` | Ergebnis Out 1 → In 1 | `7a2d567ae9f75b1a30c41adb43502be543286d13b3b3fe0fb7157ae7aa27a2d0` |
| `out2-in1.log` | Ablauf Out 2 → In 1 | `2eb20010664662310caaefdfa74136519908df524264bc97afb85515d6ccd214` |
| `out2-in1.txt` | Ergebnis Out 2 → In 1 | `7444818ed3fcefed62cb42edd17974052891071c123715a8f40b24b185714f0b` |
| `out3-in1.log` | Ablauf Out 3 → In 1 | `a0ef6264ed065b27f19bc500f775069b866c96582ca250c721bd798b676a9d96` |
| `out3-in1.txt` | Ergebnis Out 3 → In 1 | `71f3e7dcd6acaf4af63eda453a8eab6ae6531ce6dde6b962b1b0deb130dd7257` |
| `out4-in1.log` | Ablauf Out 4 → In 1 (abgebrochen) | `782beba58f0b4ee570217094e41aede300d1dbd78f9e356c115a9abcfeba0daf` |
| `out4-in1.txt` | Ergebnis Out 4 → In 1 (abgebrochen) | `e6994070f90d434b074fbf1c9f84ea8c50e2a8c3457ff336af744fa34a0d8748` |
| `out5-in1.log` | Ablauf Out 5 → In 1 | `a66cdfee789043eb8393af3724068b96ca35ee2764baf67c49d9338eabba1394` |
| `out5-in1.txt` | Ergebnis Out 5 → In 1 | `cdf37f62be19906ceb77f0df2b9e44b51a4aa382641dbc631770c424479c8ab3` |
| `out7-in1.log` | Ablauf Out 7 → In 1 | `03a2e73899d645309239474bb3c7e23d28db8adb536cb111c9ce7685ac591e8b` |
| `out7-in1.txt` | Ergebnis Out 7 → In 1 | `5aa2193248d9e8be6ea171d2c97c938f8e134dcbabc1c98b97051508de129177` |
| `pw-reserve.log` | Ausgaben von `pw-reserve` aller Läufe | `737a5d86eb3be30c904a35b0b99258ba6d7856d8d53d07e90aa941d8da082484` |

Die Ergebnisdateien (`*.txt`) sind tabulatorgetrennt; die Zeile `detail`
endet bei leerem Feld mit einem Tabulator. Das ist das Originalformat
und wurde nicht verändert.
