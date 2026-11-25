# fireface802-cc v0.5.0

For the **original RME Fireface 802** (not the 802 FS) on Linux, tested
with one unit on Kubuntu 26.04.1 LTS.

## Packages

| Package | Version | Changes since v0.4.1 |
|---|---|---|
| `ff802-cc-auto` | **0.2.0** | automatic USB port reset after the switch command (no replugging); optional mixer initialisation (38 registers); new program `ff802-mixer-init` |
| `ff802-cc` | **0.5.0** | no functional change (shared port reset timing, new project version) |
| `ff802-ucm-profile` | 0.3.1 | unchanged |

Checksums: `SHA256SUMS` of the release. Verify with
`sha256sum -c SHA256SUMS`.

## What is new

**Automatic switching without replugging.** When the unit is connected
in proprietary mode (or present at boot) and the automation is on, the
service sends the class-compliant switch command and then resets the
USB hub port of exactly this unit, with the same code and timing as
`ff802-cc enable`. A new unit, `ff802-cc-auto-enable@.service`, may write
only the `disable` attribute of that port. If the reset is not possible
or does not end in class-compliant mode, a notification asks you to
replug the cable; a failed reset is not repeated until the next boot or
`ff802-cc-auto on`.

**Optional mixer initialisation (off after installation).** At every
connection in class-compliant mode it writes exactly 168 registers of the
internal mixer through the ALSA sequencer (configuration
`ff802-mixer-init-v4`):

- every Playback p -> Hardware Output o with p != o (p, o = 1..12) off
  (132 routings, including Playback 1/2 -> Phones 9/10 set by the
  class-compliant mode),
- Playback 1..12 -> Hardware Output 1..12 at 0 dB,
- the twelve output levels at 0 dB,
- Input 9 (Mic/Inst 9) -> all twelve outputs at 30 % (about -10.5 dB),
  the same mono signal on both channels of each pair.

The service sends the list twice, 5 s apart. Turn it on with
`ff802-cc-auto mixer-init on`; run it once by hand with
`ff802-cc-auto mixer-init apply` or `ff802-mixer-init --apply`.

## Tested

- Automated: 85 tests in the dev container without hardware (simulated
  USB, sysfs, ALSA sequencer, PipeWire; package installation in an
  isolated root; reproducible package builds).
- On the unit (2026-11-23): automatic switch with port reset; mixer
  initialisation with 38 registers (v3) after power-on, twice; playback on all
  output pairs and Input 9 on all twelve outputs (listening tests).

## Known limitations

- The mixer is never read; the initialisation writes fixed values and
  has no undo. No program sets the twelve Input 9 routings back to off;
  turning the initialisation off or downgrading leaves them as they are.
- A single pass right after power-on left Playback 9..12 -> Phones 9..12
  without effect on the unit; the second pass fixes this. The cause is
  not known.
- Levels were checked by listening, not measured.
- With the initialisation on, Phones 9-12 are at 0 dB and Input 9 is on
  all outputs: keep volumes low, no microphone in front of speakers.
- One unit, one computer, Kubuntu 26.04.1 only; USB only.

Details (German): [mixer_init.md](mixer_init.md),
[t4_port_reset.md](t4_port_reset.md),
[mixer_v3_integration.md](mixer_v3_integration.md),
[packages.md](packages.md).
