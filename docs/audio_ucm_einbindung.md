# Produktive UCM-Einbindung unter alsa-lib 1.2.15.3

> **Stand 2026-11-17: entschieden und umgesetzt.** Variante **A1** ist als
> Debian-Paket `ff802-ucm-profile` (0.1.0, Dokumentation ergänzt in 0.2.0) umgesetzt (`packaging/`, siehe
> [Umsetzung](#umsetzung)). Es ist im Container in einer isolierten Wurzel
> geprüft und **nicht installiert**. Eine Installation auf dem Host braucht
> eine eigene Freigabe, erst nach T2 (Kanalzuordnung) und T3
> (PipeWire-/KDE-Sitzung, im Container nachgewiesen und als Host-Test vorbereitet: [audio_t3.md](audio_t3.md)). Dieses Dokument bleibt als Begründung der
> Entscheidung erhalten.

## Ausgangslage

- **Profil:** Das Profil (`Fireface-802.conf`, `Fireface-802-HiFi.conf`)
  ist mit der echten Karte geprüft. T1-Lauf 2 hat 29 von 29 Prüfungen
  bestanden, siehe [audio_ucm.md](audio_ucm.md#t1-lauf-2-am-2026-11-17).
  Offen ist nur, **wie** alsa-lib das Profil für die Karte findet.
- **Problem:** Der vorgesehene Weg ist die Datei
  `USB-Audio/conf.d/0424-3fdd.conf`, eingebunden aus `USB-Audio.conf` über
  ein Include mit `Optional true`. Dieser Weg funktioniert mit alsa-lib
  < 1.2.16 nicht. Die `access()`-Prüfung verwendet den unaufgelösten Pfad und
  verwirft die Datei. Behoben ist das upstream mit Commit `62c24074`,
  enthalten ab 1.2.16. Ubuntu 26.04 liefert 1.2.15.3-1ubuntu1.1 ohne diesen
  Fix.
- **Rahmen:** Ubuntu 26.04 ist ein LTS-Release. Ohne Stable Release Update
  (SRU) bleibt alsa-lib dort auf 1.2.15.3. Auch alsa-ucm-conf wird laufend
  aktualisiert: 1.2.15.3-1ubuntu1.7 enthält elf Ubuntu-Patches, einer davon
  ändert `USB-Audio.conf`.

## Optionen

### A1: eigene Hauptdatei für den Langnamen der Karte (neue Datei, keine Änderung)

`ucm.conf` sucht **vor** `USB-Audio.conf` nach
`conf.d/USB-Audio/${CardLongName}.conf`. Diese Suche läuft über
`parse_toplevel_path()`, die den Pfad korrekt auflöst, und ist vom Fehler
nicht betroffen. Eine neue Datei

```
/usr/share/alsa/ucm2/conf.d/USB-Audio/RME Fireface 802 (24047128) at usb-0000:00:14.0-2.4.1.1.3, high speed.conf
```

übernimmt dann die Rolle von `USB-Audio.conf` für genau diese Karte. Sie
setzt `SplitPCMPeriodTime` wie `USB-Audio.conf`, bindet unsere unveränderte
`conf.d/0424-3fdd.conf` **ohne** `Optional` ein (samt Absicherung gegen 802
FS) und lädt dann das Profil. Die Datei hat 15 Zeilen.

**Nachgewiesen im Container** mit alsa-lib 1.2.15.3-1ubuntu1.1, dem
unveränderten UCM-Baum von alsa-ucm-conf 1.2.15.3-1ubuntu1.7 und der
nachgebildeten Karte mit dem Langnamen des Hosts:
- `dump`, `dump` mit SplitPCM und `_devices/HiFi` sind **byte-gleich** mit den
  Ausgaben von T1-Lauf 2 auf dem Host.
- `USB-Audio.conf` bleibt unverändert.
- Ein anderer USB-Port oder der Name „802 FS“ ergeben „UCM is not supported“,
  also das heutige Verhalten.

| | |
|---|---|
| Installation | Vier neue Dateien unter `/usr/share/alsa/ucm2/`: drei Profildateien und die Hauptdatei. Keine vorhandene Datei wird geändert. Am saubersten als kleines lokales `.deb`, dann gehören die Dateien dpkg und lassen sich rückstandsfrei entfernen. |
| Updates von alsa-ucm-conf | Sie berühren die neuen Dateien nicht. Die Hauptdatei hängt nur von `common/pcm/split.conf` (über das Profil) und von der Syntax 8 ab. |
| Update auf alsa-lib ≥ 1.2.16 | Die Hauptdatei funktioniert weiter und hat Vorrang. Danach kann sie entfallen, weil `conf.d/0424-3fdd.conf` dann von selbst greift. |
| Schwäche | Der Langname enthält **Seriennummer, USB-Port-Pfad und Geschwindigkeit**. An einem anderen Port oder Hub, etwa durch ein anderes Dock, greift das Profil nicht. Das ist harmlos: Es gelten dann die bisherigen Profile. Für jeden weiteren Port wäre eine eigene Datei nötig. |
| Schwäche | Wenn alsa-ucm-conf später selbst ein Profil für das 802 mitbringt, verdeckt unsere Datei es. Dann muss sie entfernt werden. |
| Wartung | gering. `ff802-ucm-bind status` meldet, ob die angeschlossene Karte gebunden ist und ob die Umgehung noch nötig ist. |

### A2: `USB-Audio.conf` per `dpkg-divert` ersetzen

Funktioniert an jedem Port. Dafür übernehmen wir eine Datei, die Ubuntu
regelmäßig ändert. Nach jedem Update von alsa-ucm-conf ist unsere Kopie
veraltet: Neue Geräteunterstützung fehlt, bis sie von Hand nachgezogen wird.
Ein Fehler in der Kopie trifft **alle** USB-Audiogeräte. **Nicht empfohlen.**

### A3: eigener vollständiger UCM-Baum über `ALSA_CONFIG_UCM2`

So arbeiten T1 und T3 zu Testzwecken. Produktiv bedeutet das:
- Die Variable ersetzt den UCM-Baum für **alle** Karten, auch für das interne
  `sof-soundwire` des Laptops. Für diese Karte bringt Ubuntu laufend Patches.
- Der Baum veraltet bei jedem Update.
- Andere ALSA-Programme ohne diese Variable sehen ein anderes Profil.

**Nur für Tests.**

### B1: auf alsa-lib ≥ 1.2.16 warten

Das ist die sauberste Lösung. Unser ursprünglicher Entwurf, nur
`conf.d/0424-3fdd.conf` hinzuzufügen, funktioniert dann an jedem Port und ohne
weitere Datei. Der Zeitpunkt ist offen: Für 26.04 LTS geht das nur über ein
SRU oder ein Upgrade auf ein späteres Release. Der Test `ucm_alsa_eval`
meldet automatisch, wenn die eingesetzte alsa-lib den `conf.d`-Weg
beherrscht.

### B2: SRU bei Ubuntu anregen

Der Fix `62c24074` ändert 3 Zeilen und repariert eine dokumentierte Funktion
(„Optional configuration … conf.d“), die heute für alle Geräte wirkungslos
ist. Damit ist er ein plausibler Kandidat für ein SRU, auch wenn der Ausgang
und die Dauer ungewiss sind. Unser Nachweis lässt sich als Testfall
beilegen: ein minimales `Optional`-Include ohne Karte.

### B3: alsa-lib selbst patchen und bauen

Technisch klein, betrieblich riskant. Wir würden eine zentrale
Systembibliothek pflegen. 2026 kamen bereits zwei Sicherheitsupdates
(CVE-2026-25068, CVE-2026-56109), und jedes müsste sofort nachgebaut werden,
dazu apt-Pinning. **Nicht empfohlen.**

### C: Beitrag zu alsa-ucm-conf upstream

Langfristig ist das der beste Weg, weil dann keine lokalen Dateien mehr
nötig sind. Es käme ein Eintrag `If.fireface-802` in `USB-Audio.conf` dazu,
wie beim UCX. Sinnvoll erst nach T2 (Kanalzuordnung bestätigt). Ubuntu erreicht
der Beitrag frühestens mit einem späteren Release.

## Bewertung

| Kriterium | A1 Langname | A2 divert | A3 eigener Baum | B1 warten | B3 eigene alsa-lib | C upstream |
|---|---|---|---|---|---|---|
| wirkt mit 1.2.15.3 | ja (nachgewiesen) | ja | ja | nein | ja | nein |
| ändert Systemdateien | nein (nur neue) | ja | nein | nein | ja (Bibliothek) | nein |
| robust gegen alsa-ucm-conf-Updates | ja | nein | nein | ja | ja | ja |
| andere Karten betroffen | nein | alle USB | alle | nein | alle | nein |
| unabhängig vom USB-Port | **nein** (ab 0.3.0: ja, udev-Hook) | ja | ja | ja | ja | ja |
| Wartungsaufwand | gering | hoch | hoch | keiner | hoch | keiner |
| rückstandsfrei entfernbar | ja (Paket) | ja | ja | – | aufwendig | – |

## Umsetzung

Umgesetzt ist A1 mit diesen Teilen:

| Teil | Datei im Repository | installiert als |
|---|---|---|
| Profil | `audio/ucm2/USB-Audio/RME/Fireface-802*.conf` | `/usr/share/alsa/ucm2/USB-Audio/RME/` |
| Auswahl (für alsa-lib ≥ 1.2.16) | `audio/ucm2/USB-Audio/conf.d/0424-3fdd.conf` | `/usr/share/alsa/ucm2/USB-Audio/conf.d/` |
| Hauptdatei (Umgehung) | `packaging/ff802-ucm/longname-master.conf` | `/usr/share/ff802-ucm/` |
| Gerätezuordnung | `packaging/ff802-ucm/ff802-ucm-bind` | `/usr/sbin/ff802-ucm-bind` |
| Maintainer-Skripte | `packaging/ff802-ucm/{postinst,prerm,postrm}` | – |
| Bau | `packaging/build-deb.sh`, Ziel `deb` in CMake | – |

- **`ff802-ucm-bind bind`** legt
  `/usr/share/alsa/ucm2/conf.d/USB-Audio/<Langname>.conf` als relativen
  Symlink auf die Hauptdatei an.
  - Akzeptiert wird nur genau ein originales Fireface 802: `usbid`
    `0424:3fdd`, Langname mit „Fireface 802“ und ohne „802 FS“.
  - Eine vorhandene fremde Datei wird nie überschrieben (Exit 4).
  - Langnamen mit `/` werden abgewiesen.
  - Jede angelegte Zuordnung wird in `/var/lib/ff802-ucm/bindings`
    vermerkt.
  - Bei alsa-lib ≥ 1.2.16 legt `bind` nichts an, mit dem Hinweis, dass die
    Zuordnung nicht nötig ist; `--force` bindet trotzdem.
- **`postinst`** bindet die angeschlossene Karte automatisch. Fehlt sie,
  meldet es das, ohne dass die Installation scheitert. Es startet keinen
  Dienst neu.
- **`prerm` (remove) und `postrm`** entfernen alle selbst angelegten Links.
  `postrm` erkennt sie an ihrem genauen Ziel und räumt auch dann auf, wenn
  `ff802-ucm-bind` fehlt. `purge` entfernt `/var/lib/ff802-ucm`. Bei einem
  Upgrade bleiben die Zuordnungen erhalten.
- **Prüfung vor der Installation:** `tools/hw-test/ff802-ucm-t1-host.sh
  --package <deb>` prüft den Aufbau des Pakets nur lesend auf dem Host.
  Das ist T1 mit den Paketdateien und der Hauptdatei unter dem Langnamen
  der Karte, `USB-Audio.conf` bleibt unverändert.
- **Tests:** `deb_package`, `ucm_alsa_eval` (T1 mit Paketaufbau) und
  `ucm_t1_script`, siehe [audio_ucm.md](audio_ucm.md#prüfstatus).

Installation, Aktualisierung und Entfernung beschreibt
`/usr/share/doc/ff802-ucm-profile/README.Debian`
(`packaging/ff802-ucm/README.Debian`).

## Noch offen

- **B2:** Ein SRU für Commit `62c24074` bei Ubuntu wäre der Weg zu einer
  Lösung ohne Umgehung. Es ist nicht angeregt. Dafür wäre ein Text für
  Launchpad nötig, kein automatischer Versand.
- **C:** ein Beitrag zu alsa-ucm-conf, nach T2.
- **Sobald alsa-lib ≥ 1.2.16 installiert ist:** `sudo ff802-ucm-bind unbind`.
  Die `conf.d`-Datei übernimmt dann, und der Test `ucm_alsa_eval` zeigt
  diesen Wechsel an.
- **USB-Port:** Seit 0.3.0 bindet eine udev-Regel das Fireface an jedem
  Port automatisch, siehe [audio_t3_usb_port.md](audio_t3_usb_port.md). Dass
  das vor der ersten Verarbeitung durch WirePlumber geschieht, ist im
  Host-Test noch zu bestätigen.
