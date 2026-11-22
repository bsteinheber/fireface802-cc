/*
 * Simulated MIDI backend of the test program ff802-cc-autoswitch-sim.
 * Does not link libasound. The sequencer ports come from the file
 * $FF802_AUTO_ROOT/seq-ports, one port per line:
 *
 *   <client> <port> kernel|user <card> w|- <name ...>
 *
 * (missing file: no ports). Every call is appended to
 * $FF802_AUTO_ROOT/midi.log ("sysex F0 00 20 0D ... F7" for a message).
 *
 * Fault injection by environment variables:
 *   FF802_SIM_SEQ_OPEN_ERR=1        opening the sequencer fails
 *   FF802_SIM_SEQ_LIST_ERR=1        listing the ports fails
 *   FF802_SIM_SEQ_CONNECT_ERR=busy|perm
 *   FF802_SIM_SEQ_SEND_ERR_AT=<n>   message n (1-based) fails
 *   FF802_SIM_MIXER_WRITE=0         behave like a build without
 *                                   FF802_ENABLE_MIXER_WRITE
 *   FF802_SIM_MIXER_TIMEOUT_MS=<n>  wait for card/port (default 300)
 *   FF802_SIM_SEQ_PORTS_AFTER=<n>   the port list is empty for the first
 *                                   n lists (port appears late)
 */
#ifndef FF802_SIM_MIDI_H
#define FF802_SIM_MIDI_H

#include "ff802/auto_state.h"
#include "ff802/midi_backend.h"
#include "ff802/mixer_run.h"

/* Factory and options for the simulation below p's root. */
void sim_midi_setup(const ff802_auto_paths *p, ff802_midi_factory *f,
                    ff802_mixer_options *o);

#endif /* FF802_SIM_MIDI_H */
