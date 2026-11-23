/*
 * Simulated USB backend of the test program ff802-cc-autoswitch-sim.
 * Does not link libusb: the devices come from the fake sysfs tree below
 * $FF802_AUTO_ROOT/sys, every call is appended to $FF802_AUTO_ROOT/usb.log
 * ("write ep=0x0C bytes=01 00 44 BD bus=3 addr=19" for a transfer).
 *
 * Fault injection by environment variables:
 *   FF802_SIM_INIT_ERR=1          backend initialisation fails
 *   FF802_SIM_LAYOUT=bad          endpoint 0x0C missing
 *   FF802_SIM_OPEN_ERR=access|nodev
 *   FF802_SIM_KDRV=1              kernel driver bound to interface 1
 *   FF802_SIM_CLAIM_ERR=busy
 *   FF802_SIM_WRITE_ERR=timeout|pipe|nodev|io
 *   FF802_SIM_WRITE_SHORT=<n>     report n bytes transferred
 *   FF802_SIM_REPLUG_ON_OPEN=1    after open, the device reconnects
 *                                 (sysfs devnum + 1)
 *   FF802_SIM_UNPLUG_ON_OPEN=1    after open, the device is removed
 *   FF802_SIM_UNPLUG_ON_WRITE=1   the device is removed during the write
 *   FF802_SIM_WRITE_DELAY_MS=<n>  the write takes n ms
 */
#ifndef FF802_SIM_USB_H
#define FF802_SIM_USB_H

#include "ff802/auto_state.h"
#include "ff802/usb_backend.h"
#include "ff802/usb_port.h"

ff802_usb_err sim_usb_open(const ff802_auto_paths *p, ff802_usb_backend *be);
void sim_usb_close(ff802_usb_backend *be);

/*
 * Port reset hook (ff802_port_ops) for ff802_port_switch: no real sleep.
 * The first sleep is the "off" time: the device directory of kernel_name
 * disappears ("port off <ms> disable=<v>" in usb.log, v read through the
 * port found at setup). Then, by
 *   FF802_SIM_REENUM=cc|prop|none   (unset: none)
 *   FF802_SIM_REENUM_AFTER=<polls>  (default 3)
 * it comes back after that many 100 ms polls with devnum + 1, as
 * 0424:3fdd (cc) or unchanged 2a39:3fcd (prop), or not at all.
 *   FF802_SIM_PORT_STUCK=1          "disable" becomes unwritable during the
 *                                   off time (the "0" write fails)
 */
const ff802_port_ops *sim_port_setup(const ff802_auto_paths *p, const char *kernel_name,
                                     ff802_port_ops *ops);

#endif /* FF802_SIM_USB_H */
