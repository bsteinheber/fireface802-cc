/*
 * Fake ALSA card enumerator for container tests of the PipeWire integration.
 *
 * A SPA device plugin that stands in for PipeWire's udev enumerator
 * ("api.alsa.enum.udev"). The dev container sees the sound cards of the
 * host in /sys/class/sound but has no /dev/snd, so the real enumerator can
 * never find the fake Fireface 802. This plugin reports exactly one card,
 * hw:1, with the properties the udev enumerator would report for the
 * Fireface 802 on the host. Everything after that is unchanged PipeWire and
 * WirePlumber code: WirePlumber's monitors/alsa.lua creates the ACP device
 * (real alsa-lib, UCM and ACP against the fake control and PCM plugins),
 * the split nodes and the loopbacks.
 *
 * Plugging is simulated with a file: the card is present while
 * $FF802_FAKE_ENUM_DIR/present exists. Removing the file removes the card
 * (as unplugging would), creating it again adds the card again.
 *
 * It is loaded only by the test's private WirePlumber configuration
 * (context.spa-libs maps api.alsa.enum.udev to this library).
 */
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <unistd.h>

#include <spa/monitor/device.h>
#include <spa/monitor/utils.h>
#include <spa/support/log.h>
#include <spa/support/loop.h>
#include <spa/support/plugin.h>
#include <spa/utils/keys.h>
#include <spa/utils/names.h>
#include <spa/utils/result.h>
#include <spa/utils/string.h>

struct impl {
    struct spa_handle handle;
    struct spa_device device;
    struct spa_log *log;
    struct spa_loop *main_loop;
    struct spa_hook_list hooks;
    struct spa_source notify;
    char dir[PATH_MAX];
    char present_path[PATH_MAX + 16];
    int emitted;
};

static const struct spa_dict_item device_info_items[] = {
    { SPA_KEY_DEVICE_API, "udev" },
    { SPA_KEY_DEVICE_NICK, "alsa-udev" },
    { SPA_KEY_API_UDEV_MATCH, "sound" },
};

static void emit_card(struct impl *this)
{
    struct spa_device_object_info info = SPA_DEVICE_OBJECT_INFO_INIT();
    /* as the udev enumerator reports the card (alsa-udev.c), with the udev
     * properties of a USB sound card (ID_ID, ID_PATH, ...) */
    struct spa_dict_item items[] = {
        { SPA_KEY_DEVICE_ENUM_API, "udev" },
        { SPA_KEY_DEVICE_API, "alsa" },
        { SPA_KEY_MEDIA_CLASS, "Audio/Device" },
        { SPA_KEY_API_ALSA_PATH, "hw:1" },
        { SPA_KEY_API_ALSA_CARD, "1" },
        { SPA_KEY_API_ALSA_CARD_NAME, "Fireface 802 (24047128)" },
        { SPA_KEY_API_ALSA_CARD_LONGNAME,
          "RME Fireface 802 (24047128) at usb-0000:00:14.0-2.4.1.1.3, high speed" },
        { SPA_KEY_DEVICE_BUS_PATH, "pci-0000:00:14.0-usb-0:2.4.1.1.3:1.0" },
        { SPA_KEY_DEVICE_SYSFS_PATH,
          "/devices/pci0000:00/0000:00:14.0/usb3/3-2/3-2.4/3-2.4.1/3-2.4.1.1/3-2.4.1.1.3/3-2.4.1.1.3:1.0/sound/card1" },
        { SPA_KEY_DEVICE_BUS_ID, "usb-RME_Fireface_802_24047128-00" },
        { SPA_KEY_DEVICE_BUS, "usb" },
        { SPA_KEY_DEVICE_SUBSYSTEM, "sound" },
        { SPA_KEY_DEVICE_VENDOR_ID, "0x0424" },
        { SPA_KEY_DEVICE_VENDOR_NAME, "RME" },
        { SPA_KEY_DEVICE_PRODUCT_ID, "0x3fdd" },
        { SPA_KEY_DEVICE_PRODUCT_NAME, "Fireface 802 (24047128)" },
        { SPA_KEY_DEVICE_SERIAL, "RME_Fireface_802_24047128" },
    };

    info.type = SPA_TYPE_INTERFACE_Device;
    info.factory_name = SPA_NAME_API_ALSA_ACP_DEVICE;
    info.change_mask = SPA_DEVICE_OBJECT_CHANGE_MASK_FLAGS |
                       SPA_DEVICE_OBJECT_CHANGE_MASK_PROPS;
    info.flags = 0;
    info.props = &SPA_DICT_INIT_ARRAY(items);

    spa_log_info(this->log, "ff802 fake enum: card hw:1 added");
    spa_device_emit_object_info(&this->hooks, 1, &info);
    this->emitted = 1;
}

static void update(struct impl *this)
{
    int present = access(this->present_path, F_OK) == 0;

    if (present && !this->emitted) {
        emit_card(this);
    } else if (!present && this->emitted) {
        spa_log_info(this->log, "ff802 fake enum: card hw:1 removed");
        spa_device_emit_object_info(&this->hooks, 1, NULL);
        this->emitted = 0;
    }
}

static void on_notify(struct spa_source *source)
{
    struct impl *this = source->data;
    char buf[4096];

    while (read(source->fd, buf, sizeof(buf)) > 0) {
        /* drain; the state is taken from the file system */
    }
    update(this);
}

static int start_notify(struct impl *this)
{
    int fd;

    if (this->notify.fd >= 0)
        return 0;
    fd = inotify_init1(IN_CLOEXEC | IN_NONBLOCK);
    if (fd < 0)
        return -errno;
    if (inotify_add_watch(fd, this->dir, IN_CREATE | IN_DELETE | IN_MOVED_TO |
                                         IN_MOVED_FROM) < 0) {
        int res = -errno;
        close(fd);
        return res;
    }
    this->notify.func = on_notify;
    this->notify.data = this;
    this->notify.fd = fd;
    this->notify.mask = SPA_IO_IN | SPA_IO_ERR;
    spa_loop_add_source(this->main_loop, &this->notify);
    return 0;
}

static void stop_notify(struct impl *this)
{
    if (this->notify.fd < 0)
        return;
    spa_loop_remove_source(this->main_loop, &this->notify);
    close(this->notify.fd);
    this->notify.fd = -1;
}

static void emit_device_info(struct impl *this)
{
    struct spa_device_info info = SPA_DEVICE_INFO_INIT();

    info.change_mask = SPA_DEVICE_CHANGE_MASK_FLAGS | SPA_DEVICE_CHANGE_MASK_PROPS;
    info.flags = 0;
    info.props = &SPA_DICT_INIT_ARRAY(device_info_items);
    spa_device_emit_info(&this->hooks, &info);
}

static void impl_hook_removed(struct spa_hook *hook)
{
    struct impl *this = hook->priv;

    if (spa_hook_list_is_empty(&this->hooks)) {
        stop_notify(this);
        this->emitted = 0;
    }
}

static int impl_device_add_listener(void *object, struct spa_hook *listener,
                                    const struct spa_device_events *events, void *data)
{
    struct impl *this = object;
    struct spa_hook_list save;
    int res;

    spa_return_val_if_fail(this != NULL, -EINVAL);
    spa_return_val_if_fail(events != NULL, -EINVAL);

    spa_hook_list_isolate(&this->hooks, &save, listener, events, data);
    emit_device_info(this);
    if ((res = start_notify(this)) < 0) {
        spa_log_error(this->log, "ff802 fake enum: cannot watch %s: %s",
                      this->dir, spa_strerror(res));
        spa_hook_list_join(&this->hooks, &save);
        return res;
    }
    this->emitted = 0;
    update(this);
    spa_hook_list_join(&this->hooks, &save);

    listener->removed = impl_hook_removed;
    listener->priv = this;
    return 0;
}

static const struct spa_device_methods impl_device = {
    SPA_VERSION_DEVICE_METHODS,
    .add_listener = impl_device_add_listener,
};

static int impl_get_interface(struct spa_handle *handle, const char *type, void **interface)
{
    struct impl *this = (struct impl *)handle;

    spa_return_val_if_fail(handle != NULL, -EINVAL);
    spa_return_val_if_fail(interface != NULL, -EINVAL);

    if (!spa_streq(type, SPA_TYPE_INTERFACE_Device))
        return -ENOENT;
    *interface = &this->device;
    return 0;
}

static int impl_clear(struct spa_handle *handle)
{
    stop_notify((struct impl *)handle);
    return 0;
}

static size_t impl_get_size(const struct spa_handle_factory *factory,
                            const struct spa_dict *params)
{
    (void)factory;
    (void)params;
    return sizeof(struct impl);
}

static int impl_init(const struct spa_handle_factory *factory, struct spa_handle *handle,
                     const struct spa_dict *info, const struct spa_support *support,
                     uint32_t n_support)
{
    struct impl *this = (struct impl *)handle;
    const char *dir = getenv("FF802_FAKE_ENUM_DIR");

    (void)info;
    spa_return_val_if_fail(factory != NULL, -EINVAL);
    spa_return_val_if_fail(handle != NULL, -EINVAL);

    handle->get_interface = impl_get_interface;
    handle->clear = impl_clear;

    this->log = spa_support_find(support, n_support, SPA_TYPE_INTERFACE_Log);
    this->main_loop = spa_support_find(support, n_support, SPA_TYPE_INTERFACE_Loop);
    this->notify.fd = -1;
    if (this->main_loop == NULL) {
        spa_log_error(this->log, "ff802 fake enum: a main loop is needed");
        return -EINVAL;
    }
    if (dir == NULL || dir[0] != '/' ||
        snprintf(this->dir, sizeof(this->dir), "%s", dir) >= (int)sizeof(this->dir)) {
        spa_log_error(this->log, "ff802 fake enum: FF802_FAKE_ENUM_DIR must be an absolute path");
        return -EINVAL;
    }
    snprintf(this->present_path, sizeof(this->present_path), "%s/present", this->dir);

    spa_hook_list_init(&this->hooks);
    this->device.iface = SPA_INTERFACE_INIT(SPA_TYPE_INTERFACE_Device, SPA_VERSION_DEVICE,
                                            &impl_device, this);
    return 0;
}

static const struct spa_interface_info impl_interfaces[] = {
    { SPA_TYPE_INTERFACE_Device, },
};

static int impl_enum_interface_info(const struct spa_handle_factory *factory,
                                    const struct spa_interface_info **info, uint32_t *index)
{
    spa_return_val_if_fail(factory != NULL, -EINVAL);
    spa_return_val_if_fail(info != NULL, -EINVAL);
    spa_return_val_if_fail(index != NULL, -EINVAL);

    if (*index >= SPA_N_ELEMENTS(impl_interfaces))
        return 0;
    *info = &impl_interfaces[(*index)++];
    return 1;
}

static const struct spa_handle_factory fake_enum_factory = {
    SPA_VERSION_HANDLE_FACTORY,
    SPA_NAME_API_ALSA_ENUM_UDEV,
    NULL,
    impl_get_size,
    impl_init,
    impl_enum_interface_info,
};

SPA_EXPORT
int spa_handle_factory_enum(const struct spa_handle_factory **factory, uint32_t *index)
{
    spa_return_val_if_fail(factory != NULL, -EINVAL);
    spa_return_val_if_fail(index != NULL, -EINVAL);

    if (*index > 0)
        return 0;
    *factory = &fake_enum_factory;
    (*index)++;
    return 1;
}
