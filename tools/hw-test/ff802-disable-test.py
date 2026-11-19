#!/usr/bin/env python3
"""Fireface 802: CC -> proprietary (reconstructed one-shot USB test).

Default: preview only.  Actual transmission requires --send and typing SEND.
Targets exactly the previously tested Fireface at its known USB sysfs path.
No settings, drivers, or repositories are modified by this script.

HISTORICAL REFERENCE ONLY (kept in the repository, not part of any Debian
package). This is the script of the successful hardware test of
2026-11-19 from which "ff802-cc disable" was derived; use "sudo ff802-cc
disable" instead (dynamic device detection, interface/driver checks,
refusal while ff802-cc-auto is on or audio is running).

It is specific to ONE unit: the sysfs path 3-2.4.1.1.3, the product string
with serial 24047128 and the USB serial number below are hard-coded, so on
any other port or unit it refuses before opening USB. It performs a real
USB write: libusb auto-detach of snd-usb-audio from interface 3 (the ALSA
card disappears) and one 16-byte bulk transfer to endpoint 0x07 (timeout
3000 ms, no retry); the unit then leaves class-compliant mode.
Preconditions if it is ever run again: root (sudo), ff802-cc-auto off,
no audio playing, volume safety measures in place.
"""

import argparse
import ctypes
import ctypes.util
from pathlib import Path
import sys

USB_DEVICES = Path('/sys/bus/usb/devices')
DEVICE = USB_DEVICES / '3-2.4.1.1.3'
EXPECTED = {
    'idVendor': '0424',
    'idProduct': '3fdd',
    'manufacturer': 'RME',
    'product': 'Fireface 802 (24047128)',
    'serial': 'B0CD044C8E751C8',
}
INTERFACE = 3
ENDPOINT = 0x07  # USB bulk OUT
PAYLOAD = bytes.fromhex('13 F0 00 20 13 0D 10 00 13 7E 7F 13 13 6A 0B F7')


def fail(message):
    raise RuntimeError(message)


def read_attr(path, name):
    try:
        return (path / name).read_text(encoding='ascii').strip()
    except OSError as exc:
        fail(f'Kann {path / name} nicht lesen: {exc}')


def verify_target():
    if not DEVICE.is_dir():
        fail(f'Erwarteter USB-Gerätepfad fehlt: {DEVICE}\n'
             'Der USB-Pfad kann sich geändert haben. Bitte vor dem Senden prüfen.')

    for name, expected in EXPECTED.items():
        actual = read_attr(DEVICE, name)
        if actual != expected:
            fail(f'Geräteprüfung fehlgeschlagen: {name}={actual!r}, '
                 f'erwartet {expected!r}. Kein Befehl gesendet.')

    # Ensure this is the only CC Fireface 802 currently visible.
    matches = []
    for candidate in USB_DEVICES.iterdir():
        if not (candidate / 'idVendor').exists():
            continue
        try:
            vid = (candidate / 'idVendor').read_text().strip().lower()
            pid = (candidate / 'idProduct').read_text().strip().lower()
        except OSError:
            continue
        if (vid, pid) == ('0424', '3fdd'):
            matches.append(candidate.resolve())
    if len(matches) != 1 or matches[0] != DEVICE.resolve():
        fail(f'Erwartet genau ein CC-Fireface am bekannten Port; '
             f'gefunden: {len(matches)}. Kein Befehl gesendet.')

    interface_path = USB_DEVICES / f'{DEVICE.name}:1.{INTERFACE}'
    if not interface_path.is_dir():
        fail(f'USB-Interface {INTERFACE} fehlt: {interface_path}')
    if int(read_attr(interface_path, 'bInterfaceNumber'), 16) != INTERFACE:
        fail('USB-Interface-Nummer stimmt nicht überein.')

    # The endpoint descriptor must be present and must describe bulk OUT.
    endpoint_path = interface_path / f'ep_{ENDPOINT:02x}'
    if not endpoint_path.is_dir():
        fail(f'Erwarteter USB-Endpunkt fehlt: {endpoint_path}')
    attrs = int(read_attr(endpoint_path, 'bmAttributes'), 16)
    address = int(read_attr(endpoint_path, 'bEndpointAddress'), 16)
    if (attrs & 0x03) != 2 or address != ENDPOINT:
        fail('USB-Endpunkt ist nicht der erwartete Bulk-OUT-Endpunkt.')

    bus = int(read_attr(DEVICE, 'busnum'))
    address = int(read_attr(DEVICE, 'devnum'))
    return bus, address


class USBDeviceDescriptor(ctypes.Structure):
    _fields_ = [
        ('bLength', ctypes.c_uint8), ('bDescriptorType', ctypes.c_uint8),
        ('bcdUSB', ctypes.c_uint16), ('bDeviceClass', ctypes.c_uint8),
        ('bDeviceSubClass', ctypes.c_uint8), ('bDeviceProtocol', ctypes.c_uint8),
        ('bMaxPacketSize0', ctypes.c_uint8), ('idVendor', ctypes.c_uint16),
        ('idProduct', ctypes.c_uint16), ('bcdDevice', ctypes.c_uint16),
        ('iManufacturer', ctypes.c_uint8), ('iProduct', ctypes.c_uint8),
        ('iSerialNumber', ctypes.c_uint8), ('bNumConfigurations', ctypes.c_uint8),
    ]


def send_once(bus, address):
    library = ctypes.util.find_library('usb-1.0')
    if not library:
        fail('libusb-1.0 nicht gefunden. Kein Befehl gesendet.')
    usb = ctypes.CDLL(library)
    P = ctypes.POINTER
    V = ctypes.c_void_p

    usb.libusb_init.argtypes = [P(V)]
    usb.libusb_init.restype = ctypes.c_int
    usb.libusb_exit.argtypes = [V]
    usb.libusb_get_device_list.argtypes = [V, P(P(V))]
    usb.libusb_get_device_list.restype = ctypes.c_ssize_t
    usb.libusb_free_device_list.argtypes = [P(V), ctypes.c_int]
    usb.libusb_get_bus_number.argtypes = [V]
    usb.libusb_get_bus_number.restype = ctypes.c_uint8
    usb.libusb_get_device_address.argtypes = [V]
    usb.libusb_get_device_address.restype = ctypes.c_uint8
    usb.libusb_get_device_descriptor.argtypes = [V, P(USBDeviceDescriptor)]
    usb.libusb_get_device_descriptor.restype = ctypes.c_int
    usb.libusb_open.argtypes = [V, P(V)]
    usb.libusb_open.restype = ctypes.c_int
    usb.libusb_close.argtypes = [V]
    usb.libusb_set_auto_detach_kernel_driver.argtypes = [V, ctypes.c_int]
    usb.libusb_set_auto_detach_kernel_driver.restype = ctypes.c_int
    usb.libusb_claim_interface.argtypes = [V, ctypes.c_int]
    usb.libusb_claim_interface.restype = ctypes.c_int
    usb.libusb_release_interface.argtypes = [V, ctypes.c_int]
    usb.libusb_release_interface.restype = ctypes.c_int
    usb.libusb_bulk_transfer.argtypes = [V, ctypes.c_ubyte, P(ctypes.c_ubyte),
                                         ctypes.c_int, P(ctypes.c_int), ctypes.c_uint]
    usb.libusb_bulk_transfer.restype = ctypes.c_int

    context = V()
    rc = usb.libusb_init(ctypes.byref(context))
    if rc != 0:
        fail(f'libusb_init fehlgeschlagen: {rc}')

    handle = V()
    try:
        devices = P(V)()
        count = usb.libusb_get_device_list(context, ctypes.byref(devices))
        if count < 0:
            fail(f'libusb_get_device_list fehlgeschlagen: {count}')
        try:
            selected = None
            for i in range(count):
                device = devices[i]
                if (usb.libusb_get_bus_number(device) != bus or
                        usb.libusb_get_device_address(device) != address):
                    continue
                descriptor = USBDeviceDescriptor()
                rc = usb.libusb_get_device_descriptor(device, ctypes.byref(descriptor))
                if rc != 0 or (descriptor.idVendor, descriptor.idProduct) != (0x0424, 0x3fdd):
                    fail('libusb-Gerätekennung stimmt nicht mit sysfs überein.')
                selected = device
                break
            if selected is None:
                fail('Das zuvor geprüfte USB-Gerät ist nicht mehr vorhanden.')
            rc = usb.libusb_open(selected, ctypes.byref(handle))
            if rc != 0:
                fail(f'libusb_open fehlgeschlagen: {rc} (ggf. sudo verwenden)')
        finally:
            usb.libusb_free_device_list(devices, 1)

        rc = usb.libusb_set_auto_detach_kernel_driver(handle, 1)
        if rc != 0:
            fail(f'Automatisches Kernel-Treiber-Detach fehlgeschlagen: {rc}')
        rc = usb.libusb_claim_interface(handle, INTERFACE)
        if rc != 0:
            fail(f'USB-Interface {INTERFACE} konnte nicht beansprucht werden: {rc}')
        try:
            transferred = ctypes.c_int(0)
            buffer = (ctypes.c_ubyte * len(PAYLOAD)).from_buffer_copy(PAYLOAD)
            rc = usb.libusb_bulk_transfer(handle, ENDPOINT, buffer, len(PAYLOAD),
                                          ctypes.byref(transferred), 3000)
            if rc != 0:
                fail(f'USB-Bulk-Transfer fehlgeschlagen: {rc}; '
                     f'{transferred.value} Bytes übertragen.')
            if transferred.value != len(PAYLOAD):
                fail(f'Unvollständiger Transfer: {transferred.value}/{len(PAYLOAD)} Bytes.')
            print(f'Erfolgreich übertragen: {transferred.value} Bytes.')
        finally:
            # The device may disappear immediately when the mode switches.
            rc = usb.libusb_release_interface(handle, INTERFACE)
            if rc != 0:
                print(f'Hinweis: Interface-Freigabe meldet {rc} '
                      '(nach Moduswechsel möglicherweise normal).', file=sys.stderr)
    finally:
        if handle.value:
            usb.libusb_close(handle)
        usb.libusb_exit(context)


def main():
    parser = argparse.ArgumentParser(description='Fireface 802: CC-Modus deaktivieren')
    parser.add_argument('--send', action='store_true',
                        help='USB-Befehl nach zusätzlicher Bestätigung einmal senden')
    args = parser.parse_args()

    bus, address = verify_target()
    print(f'Gerät geprüft: {DEVICE} (Bus {bus:03d}, Device {address:03d})')
    print(f'Fireface 802 (24047128), CC-Modus, USB-ID 0424:3fdd')
    print(f'Interface {INTERFACE}, Bulk OUT 0x{ENDPOINT:02X}')
    print(f'Payload ({len(PAYLOAD)} Bytes): {PAYLOAD.hex(" ").upper()}')

    if not args.send:
        print('VORSCHAU: Kein USB-Befehl gesendet.')
        return

    print('ACHTUNG: Das Fireface wechselt voraussichtlich sofort in den proprietären Modus.')
    if input('Zum einmaligen Senden SEND eingeben: ').strip() != 'SEND':
        print('Abgebrochen. Kein USB-Befehl gesendet.')
        return

    # Re-check identity after user confirmation, before opening USB device.
    bus, address = verify_target()
    send_once(bus, address)


if __name__ == '__main__':
    try:
        main()
    except (RuntimeError, OSError, ValueError) as exc:
        print(f'FEHLER: {exc}', file=sys.stderr)
        sys.exit(1)
