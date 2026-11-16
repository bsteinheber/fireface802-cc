# Registerprotokoll des RME Fireface 802 (proprietärer USB-Modus)

Dieses Dokument beschreibt die Kodierung der Registerbefehle, wie sie in
`include/ff802/reg_codec.h` und `src/reg_codec.c` umgesetzt ist. Es betrifft
ausschließlich das **ursprüngliche Fireface 802** (nicht das 802 FS).

> **Stand:** Die Kodierung wurde anhand von Windows-USB-Aufzeichnungen
> abgeleitet und gegen diese getestet. Das Verhalten des Geräts unter Linux
> ist noch nicht überprüft.

## USB-Rahmenbedingungen (nur zur Einordnung)

Im proprietären Modus verwendet das Gerät folgende Parameter. Sie werden
erst in einer späteren Entwicklungsphase benötigt. Der hier beschriebene
Protokollteil kommt ohne USB-Zugriffe aus.

| Parameter              | Wert     |
|------------------------|----------|
| Vendor-ID              | `0x2A39` |
| Product-ID             | `0x3FCD` |
| USB-Konfiguration      | 1        |
| Interface              | 1        |
| Alternate Setting      | 0        |
| Bulk-OUT-Endpunkt      | `0x0C`   |
| Kernel-Treiber auf IF 1| keiner   |

## Aufbau eines Registerbefehls

Ein Registerbefehl ist ein 32-Bit-Wort:

```
 31 30                          16 15                             0
+--+------------------------------+--------------------------------+
|P |      Adresse (15 Bit)        |          Wert (16 Bit)         |
+--+------------------------------+--------------------------------+
```

| Bits   | Bedeutung                                               |
|--------|---------------------------------------------------------|
| 31     | Paritätsbit P                                           |
| 30..16 | Registeradresse, `0x0000` bis `0x7FFF`                  |
| 15..0  | Registerwert, `0x0000` bis `0xFFFF`                     |

Formal:

```
wort = (P << 31) | (adresse << 16) | wert
```

### Paritätsregel (ungerade Parität)

Das Paritätsbit wird so gewählt, dass das **gesamte 32-Bit-Wort eine
ungerade Anzahl gesetzter Bits** enthält:

- Haben Adresse und Wert zusammen eine **gerade** Anzahl gesetzter Bits,
  wird `P = 1` gesetzt.
- Haben sie bereits eine **ungerade** Anzahl, bleibt `P = 0`.

Daraus folgt: Jede Änderung genau eines Bits, auch des Paritätsbits selbst,
erzeugt ein Wort mit gerader Bitanzahl und damit einen erkennbaren
Paritätsfehler. `ff802_reg_decode()` weist solche Wörter mit
`FF802_ERR_PARITY` zurück.

Adressen größer als `0x7FFF` passen nicht in 15 Bit. `ff802_reg_encode()`
lehnt sie mit `FF802_ERR_ADDR_RANGE` ab, statt sie stillschweigend
abzuschneiden.

### Byte-Reihenfolge

Das Wort wird in **Little-Endian-Reihenfolge** übertragen, also das
niederwertigste Byte zuerst:

```
wort 0xB3B2B1B0  ->  Bytes  B0 B1 B2 B3
```

Die Umwandlung erfolgt explizit per Bitverschiebung
(`ff802_word_to_le()` / `ff802_word_from_le()`). Sie ist damit unabhängig
von der Byte-Reihenfolge des Rechners, auf dem das Programm läuft.

## Umschaltung des Class-Compliant-Modus

Register `0x3D44` steuert den Class-Compliant-Modus (CC-Modus):

| Wert     | Bedeutung        |
|----------|------------------|
| `0x0001` | CC aktivieren    |
| `0x0000` | CC deaktivieren  |

### Herleitung der aufgezeichneten Befehle

Adresse `0x3D44` = `011 1101 0100 0100` (binär) hat **7** gesetzte Bits.

**CC aktivieren** (Wert `0x0001`, 1 gesetztes Bit):

```
adresse << 16 | wert  = 0x3D440001     7 + 1 = 8 Bits (gerade)
-> P = 1
wort                  = 0xBD440001     9 Bits (ungerade)
Bytes (LE)            = 01 00 44 BD    ✓ entspricht der Aufzeichnung
```

**CC deaktivieren** (Wert `0x0000`, 0 gesetzte Bits):

```
adresse << 16 | wert  = 0x3D440000     7 + 0 = 7 Bits (ungerade)
-> P = 0
wort                  = 0x3D440000     7 Bits (ungerade)
Bytes (LE)            = 00 00 44 3D    ✓ entspricht der Aufzeichnung
```

Die beiden Befehle unterscheiden sich also im niederwertigsten Wertbit und
im Paritätsbit. Das ist eine direkte Folge der ungeraden Parität.

### Wirksamwerden

Laut den Windows-Aufzeichnungen wird die Umschaltung erst nach einer
USB-Trennung bzw. erneuten Geräteinitialisierung wirksam. Ob sich das Gerät
unter Linux ebenso verhält, ist noch zu prüfen.

## Zuordnung zum Code

| Funktion                   | Aufgabe                                          |
|----------------------------|--------------------------------------------------|
| `ff802_reg_encode()`       | Adresse und Wert zu einem Wort mit Parität kodieren |
| `ff802_reg_decode()`       | Wort prüfen und in Adresse und Wert zerlegen     |
| `ff802_reg_parity_ok()`    | Prüfen, ob das Wort ungerade Parität hat         |
| `ff802_word_to_le()`       | Wort zu 4 Bytes (Little Endian)                  |
| `ff802_word_from_le()`     | 4 Bytes (Little Endian) zu Wort                  |
| `ff802_reg_encode_bytes()` | Kodieren und direkt serialisieren                |
| `ff802_cc_command()`       | Fertigen 4-Byte-Befehl für CC an/aus erzeugen    |
