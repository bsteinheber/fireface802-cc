# T3 0.3.0: UCM-Bindung an jedem USB-Port

> **Stand:** `ff802-ucm-profile` 0.3.0 im Dev-Container implementiert und
> getestet. Auf dem Host **nicht installiert**. Die Annahme zur
> Reihenfolge von udev und WirePlumber (unten, A1) ist **nicht
> verifiziert** und wird im Host-Test ausdrücklich geprüft.

## Problem

alsa-lib 1.2.15.3 lädt `USB-Audio/conf.d/0424-3fdd.conf` nicht. Deshalb
bindet `ff802-ucm-bind` das Profil über den Langnamen der Karte. `ucm.conf`
von alsa-ucm-conf 1.2.15.3 sucht genau zwei Dateien:

```
conf.d/USB-Audio/${CardLongName}.conf     <- Bindung (Link auf longname-master.conf)
conf.d/USB-Audio/USB-Audio.conf           <- Ubuntu
```

Der Langname enthält den Port: `RME Fireface 802 (24047128) at
usb-0000:00:14.0-2.4.1.1.3, high speed`. Ohne Eingriff in Ubuntu-Dateien
gibt es keinen Suchpfad ohne Port. Jeder neue Port braucht also eine
eigene Bindung. Bis 0.2.0 entstand sie nur im `postinst` oder von Hand.
Danach musste WirePlumber neu gestartet werden, weil es die Karte schon
ohne Profil geöffnet hatte.

## Lösung (Variante A)

`/usr/lib/udev/rules.d/77-ff802-ucm-bind.rules`:

```
ACTION!="change", GOTO="ff802_ucm_bind_end"
SUBSYSTEM!="sound", GOTO="ff802_ucm_bind_end"
KERNEL!="card[0-9]*", GOTO="ff802_ucm_bind_end"
ATTRS{idVendor}=="0424", ATTRS{idProduct}=="3fdd", ATTRS{product}=="Fireface 802*", RUN+="/usr/sbin/ff802-ucm-bind udev %n"
LABEL="ff802_ucm_bind_end"
```

### Welches Ereignis, warum dieses

Beim Anschließen meldet der Kernel `add` für `card<N>`, die PCM-Geräte
und zuletzt `controlC<N>`. systemds `78-sound-card.rules` reagiert auf
das `add` von `controlC<N>` mit `ATTR{../uevent}="change"`. Dann entsteht
ein **`change` auf `card<N>`**, sobald alle Geräteknoten der Karte da
sind. Auf diesem `change` setzt die Regel `SOUND_INITIALIZED=1`.
PipeWire (alsa-udev) wartet genau auf dieses Ereignis, um eine Karte zu
übernehmen. Beim Systemstart erzeugt das Coldplug (`add` für alle Geräte)
dieselbe Kette.

- **`ACTION=="change"`** statt `add`: Beim `add` von `card<N>` ist die
  Karte noch nicht vollständig. Außerdem ist `change` das Ereignis, das
  PipeWire auswertet. Weitere `change`-Ereignisse, etwa durch
  `udevadm trigger`, sind harmlos, weil der Hook idempotent ist.
- **`KERNEL=="card[0-9]*"`** trifft nur das Kartengerät
  `/sys/class/sound/card<N>`, nicht `controlC<N>`, `pcmC<N>D<M>p` oder
  `midiC<N>D<M>`.
- **`%n`** ist die Kernel-Nummer des Geräts, die Ziffern am Ende von
  `card<N>`, also der ALSA-Kartenindex (`/proc/asound/card<N>`).
- **`ATTRS{…}`** sucht entlang der Elterngeräte. Alle drei Bedingungen
  müssen beim selben Elterngerät zutreffen; das ist hier das USB-Gerät
  (`idVendor`, `idProduct`, `product`). `ATTRS{product}=="Fireface 802*"`
  trifft auch auf ein 802 FS zu. Das schließt erst der Hook aus.
- Die Regel setzt sonst nichts: kein `ENV`, kein `TAG`, keine Rechte.

### Was der Hook prüft (`ff802-ucm-bind udev <N>`)

1. Die Kartennummer muss 0 bis 31 sein, ohne führende Null.
2. `/proc/asound/card<N>/usbid` muss `0424:3fdd` sein.
3. Der Langname aus `/proc/asound/cards` muss **genau** diesem Muster
   entsprechen: `RME Fireface 802 (<1–16 Ziffern>) at usb-<Controller>-<Portpfad>, high speed`.
   Ausgeschlossen sind das 802 FS, andere RME- oder `0424`-Geräte, Full
   Speed und Namen mit `/`, führendem Punkt oder Zeilenumbruch.
4. Bei alsa-lib ≥ 1.2.16 wird keine Bindung angelegt.
5. Existiert schon unsere Bindung, bleibt sie unverändert. Liegt unter dem
   Namen eine fremde Datei oder ein fremder Link, wird sie nicht
   angefasst.
6. Der Hook trägt die Bindung zuerst in `/var/lib/ff802-ucm/bindings` ein
   und legt erst dann den Link an. So kennt `unbind` bzw. `postrm` jeden
   Link, den er erzeugt hat.
7. `flock` schützt gegen gleichzeitige Ereignisse. Exit-Code ist immer 0.
   Jede Entscheidung wird protokolliert (`journalctl -t ff802-ucm-bind`).
   Ein Lauf dauert etwa 20 ms.

Der Hook löscht nie etwas, startet nichts neu und greift weder auf
WirePlumber noch auf Benutzersitzungen oder deren Zustand zu.

## Annahmen (nicht als gesichert betrachtet)

| Nr. | Annahme | Begründung | Prüfung |
|---|---|---|---|
| A1 | Die Bindung existiert, bevor WirePlumber die Karte öffnet | udev gibt ein Ereignis normalerweise erst an Programme wie PipeWire weiter, wenn die `RUN`-Programme beendet sind; PipeWire wartet auf das `change` mit `SOUND_INITIALIZED` | **Host-Test, Schritte 4 und 7** |
| A2 | Ubuntu 26.04 erzeugt das `change` auf `card<N>` wie beschrieben | Inhalt von `78-sound-card.rules` (systemd) | Host-Test, Schritt 1 (nur lesend) |
| A3 | `systemd-udevd` darf `/usr` schreiben (kein `ProtectSystem`) | Standard-Unit von systemd | Host-Test, Schritt 1 (nur lesend) |
| A4 | `idVendor`, `idProduct` und `product` liegen am selben Elterngerät | USB-Geräteknoten | Host-Test, Schritt 1 (`udevadm info -a`) |
| A5 | Die Knotennamen hängen nicht vom Port ab, daher gelten gespeicherte Lautstärken und Standardausgänge am neuen Port | `verify-02 PERSISTENT` nach dem Portwechsel; Container-Test | Host-Test, Schritt 4 |

Trifft A1 nicht zu, ist die Folge harmlos. Die Bindung entsteht trotzdem,
aber die Karte erscheint beim ersten Anschluss an diesem Port ohne
Profil. Erneutes Anschließen behebt das, wie bisher. Es gibt keinen
automatischen WirePlumber-Neustart.

## Zusammenspiel

- **T4 (`ff802-cc-auto`)** ist unverändert und unabhängig: Der Hook
  reagiert auf die Soundkarte, gleich ob T4, TotalMix FX oder die ARC das
  Gerät in den CC-Modus versetzt hat.
- **WirePlumber-Einstellungen** (Lautstärken, Standardausgang,
  Ausgänge pro Anwendung) werden nicht verändert.
- **alsa-lib ≥ 1.2.16:** Es entstehen keine neuen Bindungen. Vorhandene
  sind gleichwertig zur `conf.d`-Datei; `sudo ff802-ucm-bind unbind`
  entfernt sie.

## Installation, Aktualisierung, Rückbau

```sh
cmake --build build --target deb      # build/deb/ff802-ucm-profile_0.3.0_all.deb (Container)
sudo apt install ./ff802-ucm-profile_0.3.0_all.deb     # Host, Administrator
```

- `postinst`: `ff802-ucm-bind auto` wie bisher, dann
  `udevadm control --reload-rules`. Kein `trigger`, kein Neustart.
- Upgrade 0.2.0 → 0.3.0: Bindungen bleiben erhalten.
- `apt remove`/`purge`: Regel, Werkzeug, Profil und **alle** Bindungen
  werden entfernt, auch die vom Hook angelegten. Danach lädt `postrm` die
  udev-Regeln neu. Fremde Dateien bleiben unberührt.
- Zurück auf 0.2.0: `sudo apt install --allow-downgrades
  ./ff802-ucm-profile_0.2.0_all.deb`. Die Regel entfällt, die Bindungen
  bleiben erhalten.

## Tests (Container)

| Test | Inhalt |
|---|---|
| `ucm_bind_udev` | Hook: neue und unbekannte Ports, vorhandene Bindungen bleiben, wiederholte und 10 gleichzeitige Ereignisse, fremde Datei/fremder Link, 11 Arten fremder Geräte und unsicherer Namen, ungültige Kartennummern, alsa-lib ≥ 1.2.16, fehlende Dateien, schreibgeschützter Zustand, ohne logger/flock, Laufzeit; Regel statisch |
| `deb_package` | Paketinhalt mit Regel, Maintainer-Skripte ohne `trigger`, Upgrade, Hook im installierten Baum, Entfernen/Purge löschen alle Bindungen und die Regel, Wurzel wie vorher |
| `pw_integration` Abschnitt 10 | echte PipeWire-/WirePlumber-Sitzung: Portwechsel A → B mit dem Hook vor dem Anstecken; sechs Sinks und die Quelle sofort, kein WirePlumber-Neustart, gespeicherte Lautstärke (am Hardwarekanal gemessen) und Standardausgang unverändert, WirePlumber-Zustand vom Hook unberührt; Port C ohne Hook: kein Profil |

Die Reihenfolge innerhalb von udev (A1) lässt sich im Container nicht
prüfen: Dort gibt es kein udevd.

## Host-Test (Freigabe nötig)

Ausgangslage: 0.2.0 und T4 installiert, Fireface im CC-Modus an Port A
gebunden. Zuerst die Lautstärkesicherheit herstellen: Kopfhörer ab,
Verstärker aus, keine Wiedergabe.

1. **Nur lesend: Voraussetzungen A2–A4.**
   ```sh
   grep -nE 'controlC|change|card\*|SOUND_INITIALIZED' /usr/lib/udev/rules.d/78-sound-card.rules
   systemctl show systemd-udevd -p ProtectSystem -p ReadOnlyPaths -p InaccessiblePaths
   cat /proc/asound/cards                                    # Kartennummer N
   udevadm info -a /sys/class/sound/cardN | grep -E 'KERNEL==|ATTRS\{(idVendor|idProduct|product)\}'
   ```
   Erwartet wird:
   - `78-sound-card.rules` enthält `KERNEL=="controlC*", ATTR{../uevent}="change"` und `ENV{SOUND_INITIALIZED}="1"`.
   - `ProtectSystem=no`, keine Sperre von `/usr`.
   - `KERNEL=="cardN"`, und die drei `ATTRS` stehen im selben Block.

   Weicht etwas ab: **abbrechen**, nicht installieren.
2. **Ausgangszustand sichern:**
   ```sh
   sudo ff802-ucm-bind status
   ls -l /usr/share/alsa/ucm2/conf.d/USB-Audio/
   sha256sum ~/.local/state/wireplumber/*
   systemctl --user show wireplumber -p ExecMainPID
   ```
3. **Installation (Administrator):** `sudo apt install
   ./ff802-ucm-profile_0.3.0_all.deb`. Erwartet: `already bound` (Port A),
   `ff802-ucm-bind --version` meldet 0.3.0. Prüfen, ohne auszuführen:
   `udevadm test --action=change /sys/class/sound/cardN 2>&1 | grep ff802`
   zeigt `RUN` mit `/usr/sbin/ff802-ucm-bind udev N`.
4. **Hauptprüfung A1 und A5:**
   - In einem zweiten Terminal
     `udevadm monitor --udev --property --subsystem-match=sound` starten.
   - Fireface abstecken und an einem **noch nie benutzten** Port B anstecken.
   - Zeitstempel vergleichen (beide monoton, in Sekunden seit dem Start):
     `journalctl -b -o short-monotonic -t ff802-ucm-bind` zeigt
     `udev cardN: bound: … usb-…-<Port B>…`. Diese Zeile muss **vor** dem
     im Monitor angezeigten `UDEV [<Zeit>] change …/cardN (sound)` mit
     `SOUND_INITIALIZED=1` liegen: Erst dieses Ereignis erreicht PipeWire.
   - Ohne Neustart müssen die sechs Ausgänge und der Eingang in Plasma
     erscheinen. `ExecMainPID` von WirePlumber muss unverändert sein,
     `pactl list cards` zeigt das HiFi-Profil.
   - `tools/hw-test/ff802-t3-host.sh verify` besteht (PERSISTENT).
     Lautstärken und Standardausgang sind wie vorher (`sha256sum`: nur
     Änderungen durch WirePlumber selbst).
5. **Erneut an Port B anschließen:** `already bound`, keine neue Datei
   in `conf.d/USB-Audio`.
6. **Port C** (zum Beispiel ein anderer Hub): wie Schritt 4.
7. **Kaltstart mit dem Fireface an einem neuen Port D:** Die Bindung
   entsteht beim Start (Journal), nach der Anmeldung sind die sechs
   Ausgänge da.
8. **Andere Geräte:** Ein anderes USB-Audiogerät anschließen. Im Journal
   darf dafür kein Eintrag von `ff802-ucm-bind` erscheinen.
9. **Zusammenspiel mit T4:** Fireface nach Windows (proprietärer Modus)
   anschließen. T4 schaltet um, nach dem erneuten Anschließen erscheint
   „Class-Compliant-Modus aktiv“ und die sechs Ausgänge.
10. **Rückbau:**
    - Vollständig: `sudo apt purge ff802-ucm-profile`. Danach ist
      `ls /usr/share/alsa/ucm2/conf.d/USB-Audio/` nur noch
      `USB-Audio.conf`, und `/usr/lib/udev/rules.d/77-ff802-ucm-bind.rules`
      fehlt.
    - Das Fireface einmal neu anschließen (von Hand, nicht automatisch);
      es hat dann wieder die Profile ohne UCM.
    - Alternativ zurück auf 0.2.0:
      `sudo apt install --allow-downgrades ./ff802-ucm-profile_0.2.0_all.deb`.
      Danach zeigt `ff802-ucm-bind status` die Bindungen weiter an.
