/*
 * Fake ALSA control device for container tests of the UCM profile.
 *
 * An external control plugin (snd_ctl_ext) that presents a card with the
 * identification of the Fireface 802 in class compliant mode. UCM only
 * reads the card info (driver, name, long name, components), so this is
 * enough to let the real alsa-lib evaluate the complete UCM tree, including
 * USB-Audio.conf, conditions, includes and the SplitPCM macros, without a
 * sound card. The plugin has no control elements; nothing can be written.
 *
 * It is loaded only through a private ALSA configuration that the test
 * passes via ALSA_CONFIG_PATH to its own processes:
 *
 *   ctl_type.ff802fake.lib "<path>/libfake_ctl_ff802.so"
 *   ctl.!hw { @args [ CARD ] @args.CARD.type string type ff802fake card $CARD }
 *
 * Card fields can be changed with FF802_FAKE_LONGNAME, FF802_FAKE_NAME,
 * FF802_FAKE_ID and FF802_FAKE_COMPONENTS. FF802_FAKE_LONGNAME_FILE names
 * a file whose first line, if the file exists, is used as the long name;
 * it is read at every open, so a running PipeWire session sees the card
 * "on another USB port" after the next reconnect.
 *
 * snd_ctl_ext cannot report "components", but UCM needs them
 * (${CardComponents}, USB id). The plugin therefore wraps the card_info
 * callback. This depends on the private layout of snd_ctl_t and its ops
 * table and is only enabled for the alsa-lib version it was written for
 * (1.2.15.x, layout from src/control/control_local.h).
 */
#include <alsa/asoundlib.h>
#include <alsa/control_external.h>

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Leading part of struct _snd_ctl (alsa-lib 1.2.15.x control_local.h). */
struct ctl_prefix {
    void *open_func;
    char *name;
    snd_ctl_type_t type;
    const void *ops;
    void *private_data;
};

/* snd_ctl_ops_t of alsa-lib 1.2.15.x: 32 function pointers, card_info is
 * the fifth one. */
#define OPS_COUNT 32
#define OPS_CARD_INFO 4

/* struct snd_ctl_card_info (kernel uapi): components follows
 * card, pad, id[16], driver[16], name[32], longname[80], reserved_[16],
 * mixername[80]. */
#define CARD_INFO_SIZE 376
#define COMPONENTS_OFFSET 248
#define COMPONENTS_SIZE 128

typedef int (*card_info_fn)(snd_ctl_t *, snd_ctl_card_info_t *);
typedef void (*any_fn)(void);

struct fake {
    snd_ctl_ext_t ext;
    any_fn ops[OPS_COUNT];
    card_info_fn orig_card_info;
    char components[COMPONENTS_SIZE];
};

static const char *env_or(const char *name, const char *def)
{
    const char *v = getenv(name);
    return v && v[0] ? v : def;
}

/* Long name: first line of $FF802_FAKE_LONGNAME_FILE, else the variable. */
static void long_name(char *buf, size_t size)
{
    const char *path = getenv("FF802_FAKE_LONGNAME_FILE");
    FILE *fp = (path && path[0]) ? fopen(path, "r") : NULL;

    if (fp != NULL) {
        if (fgets(buf, (int)size, fp) != NULL) {
            buf[strcspn(buf, "\n")] = '\0';
            fclose(fp);
            if (buf[0] != '\0')
                return;
        } else {
            fclose(fp);
        }
    }
    strncpy(buf, env_or("FF802_FAKE_LONGNAME",
                        "RME Fireface 802 (24047128) at usb-0000:00:14.0-2.4.1.1.3, high speed"),
            size - 1);
    buf[size - 1] = '\0';
}

static int fake_card_info(snd_ctl_t *ctl, snd_ctl_card_info_t *info)
{
    struct fake *f = ((struct ctl_prefix *)(void *)ctl)->private_data;
    int err = f->orig_card_info(ctl, info);

    if (err < 0)
        return err;
    memcpy((char *)info + COMPONENTS_OFFSET, f->components, COMPONENTS_SIZE);
    return 0;
}

static int elem_count(snd_ctl_ext_t *ext)
{
    (void)ext;
    return 0;
}

static int elem_list(snd_ctl_ext_t *ext, unsigned int offset, snd_ctl_elem_id_t *id)
{
    (void)ext; (void)offset; (void)id;
    return -EINVAL;
}

static snd_ctl_ext_key_t find_elem(snd_ctl_ext_t *ext, const snd_ctl_elem_id_t *id)
{
    (void)ext; (void)id;
    return SND_CTL_EXT_KEY_NOT_FOUND;
}

static int get_attribute(snd_ctl_ext_t *ext, snd_ctl_ext_key_t key, int *type,
                         unsigned int *acc, unsigned int *count)
{
    (void)ext; (void)key; (void)type; (void)acc; (void)count;
    return -EINVAL;
}

static int get_integer_info(snd_ctl_ext_t *ext, snd_ctl_ext_key_t key,
                            long *imin, long *imax, long *istep)
{
    (void)ext; (void)key; (void)imin; (void)imax; (void)istep;
    return -EINVAL;
}

static int read_integer(snd_ctl_ext_t *ext, snd_ctl_ext_key_t key, long *value)
{
    (void)ext; (void)key; (void)value;
    return -EINVAL;
}

static int write_integer(snd_ctl_ext_t *ext, snd_ctl_ext_key_t key, long *value)
{
    (void)ext; (void)key; (void)value;
    return -EPERM;
}

static int read_event(snd_ctl_ext_t *ext, snd_ctl_elem_id_t *id, unsigned int *mask)
{
    (void)ext; (void)id; (void)mask;
    return -EAGAIN;
}

static void fake_close(snd_ctl_ext_t *ext)
{
    free(ext);
}

static const snd_ctl_ext_callback_t callbacks = {
    .close = fake_close,
    .elem_count = elem_count,
    .elem_list = elem_list,
    .find_elem = find_elem,
    .get_attribute = get_attribute,
    .get_integer_info = get_integer_info,
    .read_integer = read_integer,
    .write_integer = write_integer,
    .read_event = read_event,
};

SND_CTL_PLUGIN_DEFINE_FUNC(ff802fake)
{
    snd_config_iterator_t i, next;
    struct fake *f;
    long card = 1;
    int err;

    (void)root;

    if (strncmp(snd_asoundlib_version(), "1.2.15", 6) != 0 ||
        snd_ctl_card_info_sizeof() != CARD_INFO_SIZE) {
        SNDERR("ff802fake: written for alsa-lib 1.2.15.x, found %s",
               snd_asoundlib_version());
        return -ENOSYS;
    }

    snd_config_for_each(i, next, conf) {
        snd_config_t *n = snd_config_iterator_entry(i);
        const char *id, *str;

        if (snd_config_get_id(n, &id) < 0)
            continue;
        if (!strcmp(id, "comment") || !strcmp(id, "type") || !strcmp(id, "hint"))
            continue;
        if (!strcmp(id, "card")) {
            if (snd_config_get_integer(n, &card) < 0) {
                char *end;

                if (snd_config_get_string(n, &str) < 0)
                    return -EINVAL;
                /* a card id (UCM uses hw:<CardId>) means the fake card 1 */
                card = strtol(str, &end, 10);
                if (end == str || *end != '\0')
                    card = 1;
            }
            continue;
        }
        SNDERR("ff802fake: unknown field %s", id);
        return -EINVAL;
    }

    f = calloc(1, sizeof(*f));
    if (f == NULL)
        return -ENOMEM;

    f->ext.version = SND_CTL_EXT_VERSION;
    f->ext.card_idx = (int)card;
    strncpy(f->ext.id, env_or("FF802_FAKE_ID", "F80224047128"), sizeof(f->ext.id) - 1);
    strncpy(f->ext.driver, "USB-Audio", sizeof(f->ext.driver) - 1);
    strncpy(f->ext.name, env_or("FF802_FAKE_NAME", "Fireface 802 (24047128)"),
            sizeof(f->ext.name) - 1);
    long_name(f->ext.longname, sizeof(f->ext.longname));
    strncpy(f->ext.mixername, "USB Mixer", sizeof(f->ext.mixername) - 1);
    strncpy(f->components, env_or("FF802_FAKE_COMPONENTS", "USB0424:3fdd"),
            sizeof(f->components) - 1);
    f->ext.poll_fd = -1;
    f->ext.callback = &callbacks;
    f->ext.private_data = f;

    err = snd_ctl_ext_create(&f->ext, name, mode);
    if (err < 0) {
        free(f);
        return err;
    }

    /* Replace the shared, read-only ops table by a private copy whose
     * card_info also fills in the components. */
    {
        struct ctl_prefix *ctl = (struct ctl_prefix *)f->ext.handle;
        memcpy(f->ops, ctl->ops, sizeof(f->ops));
        f->orig_card_info = (card_info_fn)f->ops[OPS_CARD_INFO];
        f->ops[OPS_CARD_INFO] = (any_fn)fake_card_info;
        ctl->ops = f->ops;
    }

    *handlep = f->ext.handle;
    return 0;
}

SND_CTL_PLUGIN_SYMBOL(ff802fake)
