# fireface802-cc

**English** | [Deutsch](README.de.md)

**Using the original RME Fireface 802 under Linux:**
switching to class compliant mode, by hand or automatically when the unit
is connected, and an audio profile with six stereo outputs and one stereo
input for PipeWire.

> **In short:** Out of the box, the Fireface 802 talks to the computer
> over an RME-specific USB protocol for which Linux has no driver. In a
> second operating mode, *class compliant mode*, it speaks a standardised
> USB audio protocol that Linux understands out of the box. This project
> switches the unit into that mode from Linux (and back) and makes its
> outputs appear in the desktop audio settings with sensible names, each
> usable on its own. Optionally, it sets a fixed basic state of the
> internal mixer of the Fireface at every connection (168 registers); it
> does not read the mixer and does not replace TotalMix FX.

Tested with **one** unit (original Fireface 802, not the 802 FS) on
Kubuntu 26.04.1 LTS. Not an RME project, see
[License and manufacturer](#13-license-trademarks-and-independence-from-the-manufacturer).

## Contents

1. [What is this about?](#1-what-is-this-about)
2. [What does class compliant (CC) mean?](#2-what-does-class-compliant-cc-mean)
3. [From application to speaker: USB audio and the internal mixer](#3-from-application-to-speaker-usb-audio-and-the-internal-mixer)
4. [The three packages](#4-the-three-packages)
5. [Features and test status](#5-features-and-test-status)
6. [Known limitations](#6-known-limitations)
7. [Safety notes](#7-safety-notes)
8. [Installation](#8-installation)
9. [Usage](#9-usage)
10. [Troubleshooting](#10-troubleshooting)
11. [Technical documentation](#11-technical-documentation)
12. [Development and build](#12-development-and-build)
13. [License, trademarks and independence from the manufacturer](#13-license-trademarks-and-independence-from-the-manufacturer)

---

## 1. What is this about?

### The unit

The **RME Fireface 802** is an audio interface: an external device that
connects analogue and digital audio signals (microphones, instruments,
speakers, headphones) to the computer. It has many analogue inputs and
outputs, two headphone outputs and digital interfaces. It connects to the
computer over USB or FireWire; this project is about **USB** only.

The unit contains its own digital mixer. On Windows and macOS it is
operated with the RME software **TotalMix FX**.

### The problem on Linux

Out of the box, the Fireface 802 uses a **vendor-specific protocol** on
its USB port. RME supplies drivers for it for Windows and macOS, but not
for Linux. Linux does see the unit as a USB device, but cannot play any
sound through it.

The unit also offers a **class compliant mode** (CC), in which Linux can
run it with its standard driver. This mode has to be switched on first,
though. According to the RME manual, this is done with an option in
TotalMix FX (that is, on Windows or macOS) or with the optional ARC remote
control. Anyone switching between Windows and Linux therefore keeps
facing the same question: how do I get the unit into the right mode from
Linux?

Once CC mode is active, the Fireface appears on Linux as **one** sound
card with 22 playback and 22 capture channels. Desktop applications
cannot do much with that: they expect individual stereo outputs such as
"Headphones" or "Analog 1/2".

### What this project solves

- **Switching from Linux:** proprietary → CC and CC → proprietary,
  without Windows, macOS or remote control.
- **Automatically when connected** (optional): if the unit arrives in
  proprietary mode, Linux prepares it for CC mode.
- **Understandable outputs:** six stereo outputs (Analog 1/2, 3/4, 5/6,
  7/8, Headphones 9/10 and 11/12) and one stereo input (Analog 1/2), each
  selectable on its own in KDE Plasma and other PipeWire front ends.

---

## 2. What does class compliant (CC) mean?

### Standardised USB device classes

So that not every USB device needs its own driver, there are **USB
device classes**: public standards that define how a certain kind of
device talks to the computer. Keyboards, mice, USB sticks and webcams use
such classes. The operating system ships a ready-made driver for each
class; that is why these devices work without installing anything.

For audio devices there is the **USB Audio** class. A device that follows
this standard is called **class compliant**. Linux runs such devices with
the kernel driver **`snd-usb-audio`**.

### The two USB operating modes of the Fireface 802

| | Proprietary mode | Class compliant mode |
|---|---|---|
| Protocol | RME-specific USB protocol | standardised USB audio protocol |
| Driver | RME driver for Windows/macOS | on Linux `snd-usb-audio` (part of the kernel) |
| Mixer software | TotalMix FX | none from RME for Linux |
| USB ID (VID:PID) | `2a39:3fcd` | `0424:3fdd` |
| on Linux | visible as a USB device, but **no sound** | sound card with 22 input and output channels |

The USB ID is the number a USB device reports to the computer (vendor
ID : product ID). The Fireface changes it with the operating mode; that
is how the software tells which mode it is in. Both IDs are confirmed on
our unit.

### How the sound reaches applications on Linux

In CC mode, three layers are involved:

| Layer | Task |
|---|---|
| **`snd-usb-audio`** | kernel driver: talks to the unit over USB |
| **ALSA** (*Advanced Linux Sound Architecture*) | the audio layer of Linux: turns the unit into a sound card with channels. A **UCM profile** (*Use Case Manager*) describes which channels are which sockets and what they are called. |
| **PipeWire** (with WirePlumber) | the desktop audio service: routes the sound of applications to the outputs, controls volumes, remembers settings. KDE Plasma and other front ends show what PipeWire offers. |

No special RME driver is **needed** for this.

### Important

> **CC mode is not a sound or quality setting.** It only determines *which
> language* the computer and the audio interface use to talk to each other
> over USB. Converters, preamps and the internal mixer of the Fireface
> stay the same.

What does differ: in CC mode there is no TotalMix FX on Linux. The
internal mixer keeps working; this project can only set a fixed basic
state of it, and only if you turn that on (see next section).

---

## 3. From application to speaker: USB audio and the internal mixer

Linux recognising the Fireface and showing outputs does not yet mean
that anything arrives at the sockets. Simplified, the sound takes this
path:

```text
Linux application (e.g. music player)
       │
       ▼
PipeWire / ALSA            ← this is where this project (UCM profile)
       │                     and the desktop volume controls act
       ▼
USB audio transfer         ← only possible in CC mode (ff802-cc, ff802-cc-auto)
       │
       ▼
Fireface 802
       │
       ▼
Internal digital mixer     ← decides which USB channel goes to which socket
       │                     (set e.g. with TotalMix FX, not by Linux)
       ▼
Physical audio outputs
       │
       ▼
Speakers / headphones
```

So three things have to be right, and they are independent of each
other:

1. **USB operating mode:** the unit has to be in CC mode, otherwise there
   is no sound card on Linux. This project takes care of that.
2. **Audio channels in ALSA and PipeWire:** the 22 USB channels have to
   appear as named outputs and be mapped correctly. The UCM profile of
   this project takes care of that.
3. **Routing in the internal mixer:** inside the Fireface, each USB
   channel ("Software Playback") reaches a hardware output only through a
   mixer control (*fader*). If that fader is at zero, or the channel is
   routed to another socket, you hear nothing, or the sound comes out in
   the wrong place. **This project sets it only with the optional mixer
   initialisation** (below), to a fixed basic state.

### Our observation

In our tests, Linux demonstrably transferred the audio data to the
Fireface; even so, nothing was audible at the headphone outputs at first
(for a while the sound came out at Analog 1/2 instead). The cause was the
faders in the internal mixer for the Software Playback channels concerned.
Only after these faders had been set appropriately (with TotalMix FX, or
with the third-party tool oscmix) was the sound audible at the intended
outputs. After that, the outputs also worked on Linux.

Whether the values set in TotalMix FX stay in effect after switching to
CC mode is not clear and is not documented by RME. Our observations show
that this is not always the case: after the unit had been used on Windows
in between, the routing in CC mode differed from what TotalMix FX had
shown before.

**Optional mixer initialisation (from version 0.5.0, package
`ff802-cc-auto` 0.2.1, off after installation):** at every connection in
CC mode it sets exactly 168 registers of the internal mixer (configuration
`ff802-mixer-init-v4`, tested on the unit):

- every Playback p -> Hardware Output o with p != o (p, o = 1..12) off,
  132 routings, including Playback 1/2 -> Phones 9/10 that the CC mode
  sets and any crossed routing left from TotalMix FX,
- Playback 1..12 -> Hardware Output 1..12 at 0 dB,
- the twelve output levels at 0 dB,
- Input 9 (Mic/Inst 9) -> all twelve outputs at 30 % (about -10.5 dB),
  the same mono signal on both channels of each pair.

Nothing else is changed and nothing is read from the unit. Turn it on
with `ff802-cc-auto mixer-init on`. Details:
[docs/mixer_init.md](docs/mixer_init.md) (German).

Observations, conclusions and open questions in detail:
[docs/mixer_routing.md](docs/mixer_routing.md) (German).

---

## 4. The three packages

| Package | Purpose | Contains |
|---|---|---|
| **`ff802-cc`** | switch the mode **by hand**, detect the unit and its mode | `ff802-cc` (`check`, `enable`, `disable`), `ff802-detect` |
| **`ff802-cc-auto`** | prepare CC mode **automatically** when the unit is connected | service, udev rule, systemd units, control command `ff802-cc-auto`, desktop notification |
| **`ff802-ucm-profile`** | six named stereo outputs and one input on the desktop | UCM profile, helper `ff802-ucm-bind`, udev rule for the profile binding |

### `ff802-cc`: manual switching

- `ff802-detect` lists connected Fireface 802 units with their mode. It
  does not open the unit and does not need root.
- `sudo ff802-cc check` only checks, read-only, whether a unit in
  proprietary mode is present and suitable. It sends nothing.
- `sudo ff802-cc enable` switches proprietary → CC and then briefly
  resets the unit's USB port so that it re-appears in CC mode (no
  replugging needed).
- `sudo ff802-cc disable` switches CC → proprietary.
- Before every switch: a check that exactly the right unit is meant, the
  target is shown, and you confirm by typing `ENABLE` or `DISABLE`.
  Nothing is retried. `disable` refuses while the automation is on or
  while audio is running on the unit.

### `ff802-cc-auto`: automatic switching

- After installation the automation is **off**. Turn it on:
  `ff802-cc-auto on` (administrator), off: `ff802-cc-auto off`, state:
  `ff802-cc-auto status`.
- When it is on and the unit is connected in proprietary mode (or is
  connected like that at boot), a system service sends the switch command
  once and then resets the USB port of the unit, exactly like
  `ff802-cc enable`; the unit appears in CC mode without unplugging
  (tested on the unit; see [docs/t4_port_reset.md](docs/t4_port_reset.md)).
  Only if the port cannot be reset, or the unit does not come back in CC
  mode, a desktop notification asks you to **briefly unplug and replug**
  the USB cable.
- Optional mixer initialisation, independent of the automation and also
  off after installation: `ff802-cc-auto mixer-init on|off|status`, once
  by hand: `ff802-cc-auto mixer-init dry-run|apply` (see
  [section 3](#3-from-application-to-speaker-usb-audio-and-the-internal-mixer)).
- It switches **only** towards CC, never back, and resets no other USB
  port. A failed reset is not repeated until the next boot or
  `ff802-cc-auto on`.
- Working together with `ff802-cc`: if you want the unit in proprietary
  mode on Linux, first turn the automation off, then run
  `ff802-cc disable`. Otherwise the automation would switch it again at
  the next connection; that is why `disable` refuses while the automation
  is on.

### `ff802-ucm-profile`: audio profile

- Splits the 22 channels of the unit into named stereo pairs:

  | Output in Plasma | USB channels |
  |---|---|
  | Fireface 802 (…) Analog Output 1/2 | 1–2 |
  | … Analog Output 3/4 | 3–4 |
  | … Analog Output 5/6 | 5–6 |
  | … Analog Output 7/8 | 7–8 |
  | … Headphones Output 9/10 | 9–10 |
  | … Headphones Output 11/12 | 11–12 |
  | … Analog Input 1/2 (input) | capture 1–2 |

- All six outputs can be used at the same time and chosen per
  application.
- No PipeWire or WirePlumber configuration is needed; WirePlumber 0.5
  does the splitting itself.
- Because of a bug in alsa-lib before 1.2.16, `ff802-ucm-bind` binds the
  profile to the connected card; a udev rule does this on every
  connection, also on a new USB port.

### Why three packages?

The parts are independent of each other and are installed as needed:

| I want to … | Packages |
|---|---|
| listen to music / record on Linux, the unit already arrives in CC mode | `ff802-ucm-profile` |
| switch between Windows and Linux, Linux should prepare the unit itself | `ff802-cc-auto` + `ff802-ucm-profile` |
| switch the mode deliberately by hand (also back to proprietary mode) | `ff802-cc` (+ `ff802-ucm-profile` for sound) |
| everything | all three |

No package depends on another, and no file belongs to two packages. The
automation deliberately contains only the one direction and no switching
back; switching back and the port reset live separately in `ff802-cc`,
which you only run by hand with `sudo`.

---

## 5. Features and test status

**Automated tests** run in the dev container without a unit: 85 tests
with simulated USB, a reconstructed sysfs, an emulated sound card, a
private PipeWire session and package installation into an isolated root.
They never access USB hardware or a real sound card.

**Hardware tests** were carried out by hand on the one available Fireface
802 on Kubuntu 26.04.1.

| Feature | automated tests | tested on the unit |
|---|:---:|---|
| detection of unit and mode (`ff802-detect`) | yes | yes |
| diagnosis `ff802-cc check` | yes | yes |
| `ff802-cc enable` (proprietary → CC) with port reset | yes | yes, 2026-11-19 ¹ |
| `ff802-cc disable` (CC → proprietary) | yes | yes, 2026-11-19 ¹ |
| automatic switching `ff802-cc-auto` with port reset | yes | yes, 2026-11-23 (no replugging) |
| mixer initialisation, 38 registers (`ff802-mixer-init-v3`) | yes | yes, 2026-11-23, two power cycles ⁴ |
| full 12 x 12 playback matrix, 168 registers (`ff802-mixer-init-v4`) | yes | yes, 2026-11-25, check script and listening test ⁵ |
| Input 9 at 30 % on all twelve outputs | yes | yes, listening test (E1 for Analog 3, then all twelve) ⁴ |
| UCM profile is loaded by alsa-lib with the real card (T1) | yes | yes, 29/29 checks |
| six outputs and one input in PipeWire/Plasma (T3) | yes | yes |
| profile binding on another USB port | yes | yes, 2026-11-18 |
| channel mapping measured (T2, cable loop to Line In 1) | yes (simulation) | only Analog Out 1 and 2 ² |
| playback heard (T3, through PipeWire) | – | Analog 3/4, Headphones 9/10 and 11/12 ³ |
| Analog 5/6, 7/8 and Line In 2 on the unit | yes (simulation) | not demonstrated ² |
| start volume 40 % for new outputs | yes | not individually documented |

¹ Tested with the self-built program (`build-write/ff802-cc`) with the
same switching logic as in the package `ff802-cc` 0.4.0/0.4.1 (unchanged in
0.5.0: the timing moved to a shared header with the same values).
² The measurement test T2 of 2026-11-17 demonstrated only Analog Out 1
and 2 (and Line In 1 as capture channel 1). No signal was detected at
Out 3, 5 and 7, the measurement at Out 4 was aborted, and the other
positions were not measured. This does not prove a fault; one possible
cause is the routing in the internal mixer. The profile therefore stays
marked "UNVERIFIED" internally. Logs and evaluation:
[docs/audio_t2_results/](docs/audio_t2_results/README.md) (German).
³ Listening test through PipeWire, independent of T2 and not a
measurement of the channel index; the headphones only after the Software
Playback faders in the unit had been set.
⁴ Listening tests, not level measurements. With a single pass, Playback
9..12 -> Phones 9..12 had no effect right after power-on; the service
therefore sends the list twice, 5 s apart. The cause is not known.
Details: [docs/mixer_v3_integration.md](docs/mixer_v3_integration.md)
(German).
⁵ Cause of a crossed routing found with the release candidate (v3):
Playback 3/4 was also heard on Phones 11/12, with no such link in
PipeWire. The unit keeps its mixer settings across power cycles, and v3
switched off only Playback 1/2 -> Phones 9/10, so a crossed routing still
set in the mixer stayed in effect. v4 sets all 144 routings of the
12 x 12 matrix, whatever the previous state was; with it Playback 3/4 is
heard only on Analog 3/4. Details: [docs/mixer_v4.md](docs/mixer_v4.md)
(German).

---

## 6. Known limitations

### Confirmed

- **Mixer only as a fixed basic state:** the internal mixer of the
  Fireface is never read; the optional initialisation writes 168 fixed
  values and nothing else. This project is **not a replacement for
  TotalMix FX**.
- **Without the mixer initialisation no routing is set up:** an output
  can appear in Plasma and receive sound without anything being audible
  at the socket (see
  [section 3](#3-from-application-to-speaker-usb-audio-and-the-internal-mixer)).
- **No undo of the mixer values:** there is no program that sets the
  twelve Input 9 routings back to off; turning the initialisation off or
  installing an older package leaves them as they are.
- **Second pass without a known cause:** the service sends the mixer
  list twice because a single pass right after power-on was not fully
  effective on the unit (footnote ⁴).
- **No volume limit of its own:** new outputs start at 40 % (WirePlumber
  default). WirePlumber remembers a volume once it has been changed and
  restores it, also 100 %.
- **Only one unit tested:** all hardware tests with exactly one Fireface
  802 (original model) on one computer.
- **Only the original Fireface 802:** the Fireface 802 FS and other RME
  devices are detected and **refused**. There is no compatibility promise
  for other models.
- **USB only**, not FireWire. Ready-made packages of the programs only for
  x86-64 (amd64).

### Not yet investigated or open

- How the mixer states of proprietary and CC mode relate to each other
  (storage in the unit, behaviour after use on Windows).
- Measurement of the channel mapping for all outputs except Analog 1/2
  and for Line In 2; T2 demonstrated only Analog Out 1 and 2 (see
  footnote ² above).
- Other Linux distributions and versions, other PipeWire or WirePlumber
  versions.
- Meaning of individual values in the switch commands: they were found by
  observation and are not documented by RME.
- From alsa-lib 1.2.16 on, `ff802-ucm-bind` is no longer needed; the
  profile has not been tested on the unit with such a version.

---

## 7. Safety notes

**Volume**
- Before connecting for the first time, before every switch and before
  playing anything for the first time: turn the headphone knobs on the
  unit fully down, take off headphones, turn amplifiers and active
  speakers down or off.
- In CC mode the internal mixer works with a state that this project
  does not know; without the mixer initialisation it does not set it
  either. Levels and routing can differ from what you expect, including
  being louder.
- With the mixer initialisation on, the headphone outputs Phones 9-12 are
  set to 0 dB, and a signal at Input 9 is heard on all twelve outputs; a
  microphone there can feed back through speakers.
- The volume in Plasma only controls the digital level. The mixer in the
  unit can additionally amplify or attenuate behind it.
- WirePlumber restores remembered volumes (including 100 %) after every
  connection. Check the volumes before the first playback after
  connecting.

**Switching the mode interrupts the sound**
- When switching, the unit re-registers on USB. The sound card disappears
  from ALSA and PipeWire and re-appears (in CC mode). Running playback and
  recording stop; applications may move to another device.
- Stop all playback and recording first. `ff802-cc disable` refuses to
  switch while audio is running on the unit.
- After switching to proprietary mode, there is no more sound from the
  Fireface on Linux.

**Permissions**
- `ff802-cc check`, `enable` and `disable` need root (`sudo`).
- `ff802-cc-auto on` needs administrator rights (prompt via polkit, or
  `sudo`); the logged-in user may run `off` without a password; anyone
  may run `status`.
- `ff802-detect` and `ff802-cc-auto status` only read.

**USB writes**
- `ff802-cc enable|disable` and the service of `ff802-cc-auto` send
  control commands to the unit. The commands were found by observation
  and are not documented by RME. Use at your own risk (see the MIT
  license: no warranty). Risks: [docs/usb_comm.md](docs/usb_comm.md)
  (German).

**Back without Linux**
- TotalMix FX on Windows or macOS automatically resets a unit in CC mode
  to normal mode when it is connected. Alternatively with the ARC remote
  control: hold Prog and B (Mono) while switching the unit on
  ([docs/t4_disable.md](docs/t4_disable.md), German).

---

## 8. Installation

### Tested environment

| | tested |
|---|---|
| System | Kubuntu 26.04.1 LTS, KDE Plasma, x86-64 |
| Audio | alsa-lib 1.2.15.3, alsa-ucm-conf 1.2.15.3, PipeWire 1.6.2, WirePlumber 0.5.13 |
| Unit | one original RME Fireface 802 (not 802 FS), over USB |

Package dependencies (`apt` installs them automatically):

| Package | depends on | recommends |
|---|---|---|
| `ff802-cc` | `libc6` (≥ 2.34), `libusb-1.0-0` | – |
| `ff802-cc-auto` | `libc6` (≥ 2.34), `libusb-1.0-0`, `libasound2t64` (≥ 1.2.10), `systemd`, `udev` | `pkexec`, `libnotify-bin` |
| `ff802-ucm-profile` | `alsa-ucm-conf` (≥ 1.2.15.3), `libasound2t64` (≥ 1.2.15.3) | – |

### Getting the packages

The three packages and the `SHA256SUMS` file are published as assets of
the GitHub release **v0.5.0**:
<https://github.com/bsteinheber/fireface802-cc/releases/tag/v0.5.0>

```
ff802-cc_0.5.0_amd64.deb
ff802-cc-auto_0.2.1_amd64.deb
ff802-ucm-profile_0.3.1_all.deb
SHA256SUMS
```

They are built reproducibly from tag `v0.5.0`, and you can also build
them yourself from this repository (see
[Development and build](#12-development-and-build)).

Verify the checksums (in the folder with the packages and the
`SHA256SUMS` file):

```sh
sha256sum -c SHA256SUMS
```

### Installing

Read the [safety notes](#7-safety-notes) first.

**1. Audio profile** (preferably with the unit connected in CC mode):

```sh
sudo apt install ./ff802-ucm-profile_0.3.1_all.deb
sudo ff802-ucm-bind status
```

Then unplug the Fireface once and plug it in again. If no unit is
connected, the udev rule does the binding at the next connection.

**2. Manual switching** (optional):

```sh
sudo apt install ./ff802-cc_0.5.0_amd64.deb
ff802-cc --version
```

**3. Automation** (optional):

```sh
sudo apt install ./ff802-cc-auto_0.2.1_amd64.deb
ff802-cc-auto status       # after installation: off
ff802-cc-auto on           # turn on (asks for the administrator password)
ff802-cc-auto mixer-init status   # optional mixer initialisation: off after installation
```

During installation, the packages only reload the udev rules and the
systemd configuration. They do not restart any services and do not
switch the unit.

### Checking the installation

```sh
dpkg -l 'ff802-*'
ff802-cc --version         # ff802-cc 0.5.0 (git <revision>, Release, USB writes enabled)
ff802-cc-auto status
sudo ff802-ucm-bind status
```

### Updating and removing

```sh
sudo apt install ./<package>_<new version>_<arch>.deb   # update, settings are kept
sudo apt remove ff802-cc
sudo apt purge ff802-cc-auto          # also removes the automation state
sudo apt remove ff802-ucm-profile     # removes profile, udev rule and bindings
```

Removing does not change the USB mode of the unit. Details:
[docs/packages.md](docs/packages.md) (German) and the `README.Debian` of
the packages (`/usr/share/doc/<package>/README.Debian`).

---

## 9. Usage

### Which mode is active?

```sh
ff802-detect
```

```
Scanned 26 USB device(s).
Bus 003 Device 018: ID 2a39:3fcd mode=proprietary port=3-2.4.1.1.3 bcdDevice=0.01
  Fireface 802, proprietary USB mode
Found 1 Fireface 802 device(s).
```

`mode=proprietary` or `mode=class-compliant`. Does not open the unit,
changes nothing. Exit status 0: unit found, 1: none found.

### Switching to CC mode by hand

```sh
sudo ff802-cc check        # optional: check only, sends nothing
sudo ff802-cc enable       # shows target and command, then type ENABLE
```

Effect: the unit receives the switch command, then its USB port is
switched off for about 2 seconds and on again. It re-appears as
`0424:3fdd`; with `ff802-ucm-profile` the six outputs appear in Plasma.
If the port reset is not possible, `enable` sends nothing and names the
alternative:

```sh
sudo ff802-cc --no-port-reset enable   # then briefly unplug the USB cable yourself
```

### Switching to proprietary mode by hand

```sh
ff802-cc-auto off          # only if ff802-cc-auto is installed and on
sudo ff802-cc disable      # shows target and command, then type DISABLE
```

Effect: the sound card of the Fireface immediately disappears from ALSA
and PipeWire; the unit re-appears as `2a39:3fcd`. After that there is no
more sound from the Fireface on Linux; on Windows/macOS it works with the
RME driver again.

If several suitable units are connected, select one with
`--device BUS:ADDRESS` (values from `ff802-detect`).

### Automation

```sh
ff802-cc-auto status       # state, connected unit, last result
ff802-cc-auto on           # turn on (administrator)
ff802-cc-auto off          # turn off (logged-in user)
```

The state survives reboots. `off` only prevents further automatic
switches; it does not change the mode of the unit.

### Checking the audio outputs

- In KDE Plasma: System Settings → Sound, or the volume icon. There you
  find "Fireface 802 (…) Analog Output 1/2" to "Headphones Output 11/12"
  and "Analog Input 1/2".
- In a terminal:

  ```sh
  wpctl status               # devices and outputs in PipeWire
  pactl list cards           # the Fireface with the profile "HiFi"
  sudo ff802-ucm-bind status # is the profile bound to the card?
  ```

---

## 10. Troubleshooting

| Observation | likely cause | what to do |
|---|---|---|
| `ff802-detect` shows `mode=proprietary`, no sound | unit not in CC mode | `sudo ff802-cc enable` or the automation |
| automation on, but the unit stays proprietary | port reset not possible or not confirmed (notification), or cable not unplugged after it | briefly unplug the cable; `ff802-cc-auto status`, `journalctl -t ff802-cc-auto -b` |
| Fireface in Plasma as one card without the six outputs | profile not bound | `sudo ff802-ucm-bind status`, `journalctl -b -t ff802-ucm-bind`; if needed `sudo ff802-ucm-bind bind` and reconnect the unit |
| output visible, level meter moves, but **nothing audible** | routing/faders in the internal mixer | check routing and faders in the unit (TotalMix FX or oscmix), see [docs/mixer_routing.md](docs/mixer_routing.md) |
| sound at the wrong output | routing in the internal mixer | as above |
| `ff802-cc disable` refuses | automation on or audio running | `ff802-cc-auto off`, stop playback |
| `ff802-cc`: `access to the USB device denied` | missing permissions | run with `sudo` |
| `ff802-cc`: exit 6 | sent, new mode not detected | briefly unplug the cable, then `ff802-detect` |

Exit status of `ff802-cc`:

| Exit | Meaning |
|:---:|---|
| 0 | success (for `enable`/`disable`: new mode detected) |
| 1 | no suitable unit, or several |
| 2 | usage error, no terminal |
| 3 | precondition not met, nothing sent |
| 4 | transfer failed, unit state unknown |
| 5 | not sent (aborted, writing disabled in the build) |
| 6 | sent, but new mode not detected |

Further diagnosis of the automation: `systemctl list-units --all 'ff802-cc-auto@*' 'ff802-cc-auto-enable@*'`,
`journalctl --user -t ff802-cc-notify -b` (notifications).

---

## 11. Technical documentation

The documents under `docs/` are in German.

| Topic | Document |
|---|---|
| Register protocol, control commands | [docs/protocol.md](docs/protocol.md) |
| Device detection | [docs/detection.md](docs/detection.md) |
| USB communication, safety concept, risks | [docs/usb_comm.md](docs/usb_comm.md) |
| `ff802-cc disable` and port reset | [docs/cc_disable_port_reset.md](docs/cc_disable_port_reset.md) |
| Way back to proprietary mode (research) | [docs/t4_disable.md](docs/t4_disable.md) |
| Automation: architecture, safety, diagnosis, removal | [docs/t4_usb_mode.md](docs/t4_usb_mode.md) |
| Automatic port reset after the switch command | [docs/t4_port_reset.md](docs/t4_port_reset.md) |
| UCM profile, host test T1, volume safety | [docs/audio_ucm.md](docs/audio_ucm.md) |
| Integration of the UCM profile (variants, alsa-lib bug) | [docs/audio_ucm_einbindung.md](docs/audio_ucm_einbindung.md) |
| Channel mapping, measurement tool `ff802-chanmap` (T2) | [docs/audio_t2_plan.md](docs/audio_t2_plan.md) |
| Results of T2 (original logs, evaluation) | [docs/audio_t2_results/](docs/audio_t2_results/README.md) |
| PipeWire, WirePlumber, Plasma (T3) | [docs/audio_t3.md](docs/audio_t3.md) |
| Profile binding on every USB port | [docs/audio_t3_usb_port.md](docs/audio_t3_usb_port.md) |
| Packages, versions, installation status | [docs/packages.md](docs/packages.md) |
| Internal mixer, Software Playback faders, observations | [docs/mixer_routing.md](docs/mixer_routing.md) |
| Mixer initialisation (168 registers), register list | [docs/mixer_init.md](docs/mixer_init.md) |
| Hardware tests B.4, D1, D3, E1; integration test v3 | [docs/mixer_b4.md](docs/mixer_b4.md), [docs/mixer_d1.md](docs/mixer_d1.md), [docs/mixer_d3.md](docs/mixer_d3.md), [docs/mixer_e1.md](docs/mixer_e1.md), [docs/mixer_v3_integration.md](docs/mixer_v3_integration.md), [docs/mixer_v4.md](docs/mixer_v4.md) |
| Release notes v0.5.0 | [docs/release-notes-v0.5.0.md](docs/release-notes-v0.5.0.md) |
| Source layout, build options, tests, development phases | [docs/development.md](docs/development.md) |

The names T1–T4 denote the test phases of development: T1 loading the
UCM profile, T2 channel mapping, T3 integration into PipeWire/Plasma, T4
USB mode management.

---

## 12. Development and build

Development and builds happen in a **dev container** (`.devcontainer/`)
with all tools; the computer with the Fireface needs no development
tools.

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure     # 85 tests, no hardware
```

In the default build, **no program can send data to the unit**. The
write function is only compiled in with `-DFF802_ENABLE_USB_WRITE=ON`;
the packages `ff802-cc` and `ff802-cc-auto` are built that way.

Debian packages (in `build/deb/`, reproducible):

```sh
cmake --build build --target deb        # ff802-ucm-profile
cmake --build build --target deb-auto   # ff802-cc-auto
cmake --build build --target deb-cc     # ff802-cc, only from a clean commit
```

Details on layout, build options, tests and packaging:
[docs/development.md](docs/development.md), [docs/packages.md](docs/packages.md)
(German).

---

## 13. License, trademarks and independence from the manufacturer

| Files | License | Copyright |
|---|---|---|
| all files unless stated otherwise below (source code, scripts, tests, packaging, documentation) | MIT, see [LICENSE](LICENSE) | 2026 Bernhard Steinheber |
| [audio/ucm2/](audio/ucm2/) (UCM profile) | BSD-3-Clause, see [LICENSES/BSD-3-Clause.txt](LICENSES/BSD-3-Clause.txt) | 2026 Bernhard Steinheber |
| [packaging/ff802-ucm/longname-master.conf](packaging/ff802-ucm/longname-master.conf) | BSD-3-Clause | 2026 Bernhard Steinheber; 2019-2026 Advanced Linux Sound Architecture (ALSA) project |

- The UCM profile is under the same license as `alsa-ucm-conf`.
  `longname-master.conf` contains parts of `USB-Audio.conf` from
  `alsa-ucm-conf` and therefore also carries the copyright notice of the
  ALSA project.
- The Debian packages contain the same assignment in
  `/usr/share/doc/<package>/copyright` (DEP-5 format).
- The documentation quotes short, referenced passages from the RME manual
  and from forum posts. These quotations remain the property of their
  authors and are not covered by the MIT license.
- The files in `tests/audio/fixtures/t1/` are output of `alsaucm` for
  this profile.

**Independence from the manufacturer:** this project is not affiliated
with RME and is neither supported nor reviewed by RME. The switch
commands come from our own observation, not from RME documents.

**Trademarks:** RME, Fireface and TotalMix are trademarks of their
respective owners. They are used here only to name the supported
hardware.
