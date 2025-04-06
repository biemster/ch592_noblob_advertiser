#!/usr/bin/env python

import usb.core
import usb.util

DEBUG_ENABLE = True

CH_USB_VENDOR_ID1   = 0x4348    # VID 1
CH_USB_VENDOR_ID2   = 0x1a86    # VID 2
CH_USB_PRODUCT_ID   = 0x55e0    # PID
CH_USB_EP_OUT       = 0x02      # endpoint for command transfer out
CH_USB_EP_IN        = 0x82      # endpoint for reply transfer in
CH_USB_PACKET_SIZE  = 64        # packet size
CH_USB_TIMEOUT      = 5000      # timeout for USB operations

CH_CMD_REBOOT       = 0xa2
CH_CMD_CONFIG_READ  = 0xa7
CH_CMD_CONFIG_WRITE = 0xa8

CH_STR_CHIP_DETECT  = b'\xa1\x12\x00\x52\x11MCU ISP & WCH.CN'
CH_STR_CONFIG_READ  = (0xa7, 0x02, 0x00, 0x1f, 0x00)
CH_STR_CONFIG_WRITE = (0xa8, 0x0e, 0x00, 0x07, 0x00)
CH_STR_REBOOT       = (0xa2, 0x01, 0x00, 0x01)

# Find the device
device = usb.core.find(idVendor=CH_USB_VENDOR_ID1, idProduct=CH_USB_PRODUCT_ID)

if device is None:
    print("ch59x not found")
    exit(0)

# Set the active configuration. With no arguments, the first configuration will be the active one
device.set_configuration()

# Get an endpoint instance
cfg = device.get_active_configuration()
intf = cfg[(0, 0)]

# Claim the interface
usb.util.claim_interface(device, intf)

# chip detect, should be [161, 0, 2, 0, 146, 34]
device.write(CH_USB_EP_OUT, CH_STR_CHIP_DETECT)
print(device.read(CH_USB_EP_IN, CH_USB_PACKET_SIZE, CH_USB_TIMEOUT))

# config read, WeAct default is [167, 0, 26, 0, 31, 0, 255, 255, 255, 255, 255, 255, 255, 255, 77, 15, 255, 79, 0, 2, 3, 0, 21, 55, 79, 16, 83, 92, 183, 163]
device.write(CH_USB_EP_OUT, CH_STR_CONFIG_READ)
print(device.read(CH_USB_EP_IN, CH_USB_PACKET_SIZE, CH_USB_TIMEOUT))

# Write good config, like the WeAct modules came with
device.write(CH_USB_EP_OUT, CH_STR_CONFIG_WRITE + bytes([255, 255, 255, 255, 255, 255, 255, 255, 213 if DEBUG_ENABLE else 77, 15, 255, 79]))
print(device.read(CH_USB_EP_IN, CH_USB_PACKET_SIZE, CH_USB_TIMEOUT))

# Reboot definitely seems necessary after config update
device.write(CH_USB_EP_OUT, CH_STR_REBOOT)
print(device.read(CH_USB_EP_IN, CH_USB_PACKET_SIZE, CH_USB_TIMEOUT))

# Release the interface when done
usb.util.release_interface(device, intf)
