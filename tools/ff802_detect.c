/*
 * ff802-detect: read-only diagnostic tool that lists RME Fireface 802
 * devices and their operating mode.
 *
 * The tool only enumerates USB devices. It does not open devices, claim
 * interfaces or send any data.
 */
#include <stdio.h>
#include <string.h>

#include "ff802/detect.h"
#include "ff802/usb_enum.h"

#ifndef FF802_VERSION
#define FF802_VERSION "unknown"
#endif
#ifndef FF802_GIT_REVISION
#define FF802_GIT_REVISION "unknown"
#endif
#ifndef FF802_BUILD_TYPE
#define FF802_BUILD_TYPE "unspecified"
#endif

static void usage(FILE *out)
{
    fprintf(out,
            "Usage: ff802-detect [--help] [--version]\n"
            "\n"
            "Lists RME Fireface 802 devices (original model, not 802 FS)\n"
            "and their USB mode. Read-only: no device is opened and no\n"
            "data is sent to the device.\n"
            "\n"
            "Exit status:\n"
            "  0  at least one Fireface 802 found\n"
            "  1  no Fireface 802 found\n"
            "  2  usage error or USB enumeration failed\n");
}

int main(int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            usage(stdout);
            return 0;
        }
        if (strcmp(argv[i], "-V") == 0 || strcmp(argv[i], "--version") == 0) {
            printf("ff802-detect %s (git %s, %s)\n", FF802_VERSION,
                   FF802_GIT_REVISION, FF802_BUILD_TYPE);
            return 0;
        }
        fprintf(stderr, "ff802-detect: unknown argument '%s'\n\n", argv[i]);
        usage(stderr);
        return FF802_EXIT_ERROR;
    }

    ff802_usb_dev *devs = NULL;
    size_t count = 0;
    int err = ff802_usb_enumerate(&devs, &count);
    if (err < 0) {
        fprintf(stderr, "ff802-detect: USB enumeration failed: %s\n",
                ff802_usb_strerror(err));
        return FF802_EXIT_ERROR;
    }

    int code = ff802_report(devs, count, stdout);
    ff802_usb_free(devs);
    return code;
}
