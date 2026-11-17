#include "ff802/detect.h"

size_t ff802_find_devices(const ff802_usb_dev *devs, size_t n,
                          ff802_match *out, size_t max_out)
{
    size_t found = 0;

    if (devs == NULL)
        return 0;

    for (size_t i = 0; i < n; i++) {
        ff802_mode mode = ff802_classify(devs[i].vid, devs[i].pid);
        if (mode == FF802_MODE_NONE)
            continue;
        if (out != NULL && found < max_out) {
            out[found].dev = &devs[i];
            out[found].mode = mode;
        }
        found++;
    }
    return found;
}

int ff802_format_port_path(const ff802_usb_dev *dev, char *buf, size_t size)
{
    /* Root hubs have no port path; show just the bus. */
    int len = snprintf(buf, size, "%u", (unsigned)dev->bus);
    if (len < 0)
        return len;

    unsigned depth = dev->port_count;
    if (depth > FF802_MAX_PORT_DEPTH)
        depth = FF802_MAX_PORT_DEPTH;

    for (unsigned i = 0; i < depth; i++) {
        size_t used = (size_t)len < size ? (size_t)len : size;
        char sep = i == 0 ? '-' : '.';
        int r;
        /* Without room (or without buffer) only measure the length:
         * snprintf with a NULL destination is only defined for size 0. */
        if (buf != NULL && used < size)
            r = snprintf(buf + used, size - used, "%c%u", sep, (unsigned)dev->ports[i]);
        else
            r = snprintf(NULL, 0, "%c%u", sep, (unsigned)dev->ports[i]);
        if (r < 0)
            return r;
        len += r;
    }
    return len;
}

int ff802_format_device(const ff802_usb_dev *dev, ff802_mode mode,
                        char *buf, size_t size)
{
    char port[48];
    ff802_format_port_path(dev, port, sizeof port);

    return snprintf(buf, size,
                    "Bus %03u Device %03u: ID %04x:%04x mode=%s port=%s bcdDevice=%x.%02x",
                    (unsigned)dev->bus, (unsigned)dev->address,
                    (unsigned)dev->vid, (unsigned)dev->pid,
                    ff802_mode_name(mode), port,
                    (unsigned)(dev->bcd_device >> 8),
                    (unsigned)(dev->bcd_device & 0xFFu));
}

static void print_match(FILE *out, const ff802_usb_dev *dev, ff802_mode mode)
{
    char line[160];
    ff802_format_device(dev, mode, line, sizeof line);
    fprintf(out, "%s\n  %s\n", line, ff802_mode_description(mode));
}

int ff802_report(const ff802_usb_dev *devs, size_t n, FILE *out)
{
    size_t ff802_count = 0;
    size_t other_count = 0;

    if (devs == NULL)
        n = 0;

    fprintf(out, "Scanned %zu USB device(s).\n", n);

    for (size_t i = 0; i < n; i++) {
        ff802_mode mode = ff802_classify(devs[i].vid, devs[i].pid);
        if (ff802_mode_is_ff802(mode)) {
            print_match(out, &devs[i], mode);
            ff802_count++;
        }
    }

    for (size_t i = 0; i < n; i++) {
        ff802_mode mode = ff802_classify(devs[i].vid, devs[i].pid);
        if (mode == FF802_MODE_RME_OTHER) {
            if (other_count == 0)
                fprintf(out, "Note: other RME devices present:\n");
            print_match(out, &devs[i], mode);
            other_count++;
        }
    }

    if (ff802_count > 0) {
        fprintf(out, "Found %zu Fireface 802 device(s).\n", ff802_count);
        return FF802_EXIT_FOUND;
    }

    if (n == 0) {
        fprintf(out, "No USB devices visible. This usually means the program "
                     "has no access to the USB bus\n"
                     "(e.g. it runs inside a container without USB access).\n");
    } else {
        fprintf(out, "No Fireface 802 found. Check power and cable; "
                     "'lsusb' should list 2a39:3fcd\n"
                     "(proprietary mode) or 0424:3fdd (class-compliant mode).\n");
    }
    return FF802_EXIT_NOT_FOUND;
}
