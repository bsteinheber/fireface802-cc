/*
 * Parse the Fireface 802 UCM files with the real alsa-lib configuration
 * parser (the same parser UCM uses to load its files) and cross-check the
 * number of devices in the HiFi file.
 *
 * Usage: test_ucm_syntax <ucm2 dir>
 *
 * This only validates syntax and structure. Evaluating the profile (macros,
 * conditions, ${CardComponents}) requires a real sound card and happens in
 * host test T1.
 */
#include <alsa/asoundlib.h>

#include <stdio.h>
#include <string.h>

static int failures;

static snd_config_t *load(const char *dir, const char *rel)
{
    char path[1024];
    snd_config_t *top = NULL;
    snd_input_t *in = NULL;
    int err;

    snprintf(path, sizeof path, "%s/%s", dir, rel);

    err = snd_config_top(&top);
    if (err < 0) {
        fprintf(stderr, "snd_config_top: %s\n", snd_strerror(err));
        failures++;
        return NULL;
    }
    err = snd_input_stdio_open(&in, path, "r");
    if (err < 0) {
        fprintf(stderr, "%s: cannot open: %s\n", path, snd_strerror(err));
        snd_config_delete(top);
        failures++;
        return NULL;
    }
    err = snd_config_load(top, in);
    snd_input_close(in);
    if (err < 0) {
        fprintf(stderr, "%s: parse error: %s\n", path, snd_strerror(err));
        snd_config_delete(top);
        failures++;
        return NULL;
    }
    printf("%s: parsed\n", rel);
    return top;
}

static int count_children(snd_config_t *node)
{
    snd_config_iterator_t i, next;
    int n = 0;

    snd_config_for_each(i, next, node)
        n++;
    return n;
}

int main(int argc, char **argv)
{
    snd_config_t *cfg, *devices;

    if (argc != 2) {
        fprintf(stderr, "usage: %s <ucm2 dir>\n", argv[0]);
        return 2;
    }

    cfg = load(argv[1], "USB-Audio/conf.d/0424-3fdd.conf");
    if (cfg)
        snd_config_delete(cfg);

    cfg = load(argv[1], "USB-Audio/RME/Fireface-802.conf");
    if (cfg)
        snd_config_delete(cfg);

    cfg = load(argv[1], "USB-Audio/RME/Fireface-802-HiFi.conf");
    if (cfg) {
        if (snd_config_search(cfg, "SectionDevice", &devices) < 0) {
            fprintf(stderr, "HiFi: no SectionDevice node\n");
            failures++;
        } else if (count_children(devices) != 7) {
            fprintf(stderr, "HiFi: expected 7 devices, parser found %d\n",
                    count_children(devices));
            failures++;
        } else {
            printf("HiFi: 7 devices\n");
        }
        snd_config_delete(cfg);
    }

    snd_config_update_free_global();
    printf("%d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
