/*
 * T4: persistent and per-boot state of the automatic mode switch.
 *
 *   <state_dir>/mode        "on" enables the automation. Missing, "off"
 *                           or anything else: off. Persistent (/var/lib),
 *                           written only by ff802-cc-auto on|off.
 *   <run_dir>/lock          serialises all events (flock)
 *   <run_dir>/attempts/<k>  one file per handled connection
 *                           (<kernel name>_<bus>_<devnum>); /run is empty
 *                           after a reboot, devnum changes on reconnect
 *   <run_dir>/pending       a CC switch was prepared on this connection
 *   <run_dir>/event         last event for the desktop notification
 *                           (seq, kind, time, port, busnum, devnum, detail)
 *   <run_dir>/seq           sequence number of the last event
 *   <run_dir>/last-result   outcome of the last handled event
 *
 * All files are written atomically (temporary file + rename). The
 * notification side only reads them; nothing here talks to a desktop
 * session.
 */
#ifndef FF802_AUTO_STATE_H
#define FF802_AUTO_STATE_H

#include <stddef.h>

#include "ff802/auto_sysfs.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FF802_AUTO_PATH_MAX 192u

typedef struct ff802_auto_paths {
    char run_dir[FF802_AUTO_PATH_MAX];
    char state_dir[FF802_AUTO_PATH_MAX];
    char sysfs_root[FF802_AUTO_PATH_MAX];
} ff802_auto_paths;

/* /run/ff802-cc-auto, /var/lib/ff802-cc-auto, /sys */
void ff802_auto_default_paths(ff802_auto_paths *p);

/* The same layout below <root> (tests): <root>/run/ff802-cc-auto,
 * <root>/var/lib/ff802-cc-auto, <root>/sys. Returns 0 on success. */
int ff802_auto_paths_below(ff802_auto_paths *p, const char *root);

typedef enum ff802_auto_mode {
    FF802_AUTO_MODE_OFF = 0,     /* missing file or "off" */
    FF802_AUTO_MODE_ON,          /* exactly "on" */
    FF802_AUTO_MODE_INVALID      /* anything else: treated as off */
} ff802_auto_mode;

ff802_auto_mode ff802_auto_read_mode(const ff802_auto_paths *p);
const char *ff802_auto_mode_str(ff802_auto_mode m);

/*
 * Create run_dir (0755) and take the exclusive lock. Blocks until the
 * lock is free. Returns the lock fd, or -1.
 */
int ff802_auto_lock(const ff802_auto_paths *p);
void ff802_auto_unlock(int fd);

/*
 * Record that this connection is being handled. Returns 1 if it was not
 * handled before (record created), 0 if a record exists already, -1 on
 * error. The record is created before any USB access, so a crash can
 * never lead to a second attempt on the same connection.
 */
int ff802_auto_claim_connection(const ff802_auto_paths *p,
                                const ff802_sysfs_dev *d);

typedef enum ff802_auto_event_kind {
    FF802_EVENT_PREPARED = 0,   /* enable sent: reconnect needed */
    FF802_EVENT_CC_ACTIVE,      /* device came back in CC mode */
    FF802_EVENT_FAILED          /* switch not possible / state unknown */
} ff802_auto_event_kind;

const char *ff802_auto_event_kind_str(ff802_auto_event_kind k);

/*
 * Write <run_dir>/event with the next sequence number. detail is one
 * line; control characters are replaced. Returns the sequence number
 * (>= 1) or -1.
 */
long ff802_auto_write_event(const ff802_auto_paths *p,
                            ff802_auto_event_kind kind,
                            const ff802_sysfs_dev *d, const char *detail);

/* <run_dir>/pending: written after a successful enable transfer. */
int ff802_auto_set_pending(const ff802_auto_paths *p, const ff802_sysfs_dev *d);

/* 1 if a pending switch is recorded for this port (kernel name). */
int ff802_auto_pending_for(const ff802_auto_paths *p, const char *kernel_name);

int ff802_auto_clear_pending(const ff802_auto_paths *p);

/* <run_dir>/last-result: "outcome=<word>\nport=...\ndetail=...\n" */
int ff802_auto_write_result(const ff802_auto_paths *p, const char *outcome,
                            const char *kernel_name, const char *detail);

#ifdef __cplusplus
}
#endif

#endif /* FF802_AUTO_STATE_H */
