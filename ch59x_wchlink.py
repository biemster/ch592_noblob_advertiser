#!/usr/bin/env python
import sys
from time import sleep
import argparse
from array import array

import usb.core
import usb.util

CHIPTYPE_CH59x = 0x0b

CH_USB_VENDOR_ID   = 0x1a86    # VID
CH_USB_PRODUCT_ID  = 0x8010    # PID
CH_USB_EP_OUT      = 0x01      # endpoint for command transfer out
CH_USB_EP_OUT_DATA = 0x02      # endpoint for data transfer out
CH_USB_EP_IN       = 0x81      # endpoint for reply transfer in
CH_USB_EP_IN_DATA  = 0x82      # endpoint for data reply transfer in
CH_USB_PACKET_SIZE = 256       # packet size
CH_USB_TIMEOUT     = 5000      # timeout for USB operations

CH_STR_PROG_DETECT = (0x81, 0x0d, 0x01, 0x01)
CH_STR_PROG_SPEED  = (0x81, 0x0c, 0x02, 0x01, 0x02) # (0x01: 6000kHz, 0x02: 4000kHz, 0x03: 400kHz)
CH_STR_CHIP_DETECT = (0x81, 0x0d, 0x01, 0x02)
CH_STR_CHIP_SPEED  = (0x81, 0x0c, 0x02, CHIPTYPE_CH59x, 0x02)
CH_STR_FLASH_PREP  = (0x81, 0x01, 0x08, 0x00, 0x00, 0x00, 0x00) # send to addr 0x00000000
CH_STR_POLL_DEBUG  = (0x81, 0x08, 0x06, 0x04, 0x00, 0x00, 0x00, 0x00, 0x01)
CH_STR_ACK1_DEBUG  = (0x81, 0x08, 0x06, 0x05, 0x00, 0x00, 0x00, 0x00, 0x02)
CH_STR_ACK2_DEBUG  = (0x81, 0x08, 0x06, 0x04, 0x00, 0x00, 0x00, 0x04, 0x02)

device = usb.core.find(idVendor=CH_USB_VENDOR_ID, idProduct=CH_USB_PRODUCT_ID)

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--flash', help='Flash .bin file. If no file is provided a blinky will be flashed')
    parser.add_argument('--dump', help='Dump memory region, use with --length to batch n bytes')
    parser.add_argument('--length', help='Number of bytes to dump')
    parser.add_argument('--terminal', help='Open debug interface terminal', action='store_true')
    parser.add_argument('--toggle-3v', help='Toggle the 3v3 line, to turn the chip off and on again', action='store_true')
    parser.add_argument('--toggle-5v', help='Toggle the 5v line, to turn the chip off and on again', action='store_true')
    parser.add_argument('--reset', help='Reset', action='store_true')
    args = parser.parse_args()

    if device is None:
        print("no programmer found")
        exit(0)

    # Get an endpoint instance
    cfg = device.get_active_configuration()
    intf = cfg[(0, 0)]

    # Claim the interface
    usb.util.claim_interface(device, intf)

    prog_init()

    if args.toggle_3v or args.toggle_5v:
        toggle_power(args.toggle_3v, args.toggle_5v)

    chip_init()

    if args.flash:
        flash(args.flash)
        reset()
        if args.terminal:
            open_terminal()
    elif args.reset:
        reset()
        if args.terminal:
            open_terminal()
    elif args.terminal:
        open_terminal()
    elif args.dump:
        dump(args.dump, args.length)
    elif not (args.toggle_3v or args.toggle_5v):
        flash()
        reset()

    # Release the interface when done
    usb.util.release_interface(device, intf)
    print('done')

flashloader = bytes.fromhex(
        '797122d44ad056ca06d626d24ece52cc5ac85ec662c4937715002a84ae8a3289'
        'c1e39377240099cbb7860700014681450545492a9377f50f0945a5ef93774400'
        '91cb85660146d6850545ad2a9377f50f1145a5e393778401014ad9cfb7540020'
        '1309f90f9384041013598900014ab38a9a40937b8400137c0401338b9a006396'
        '0b0463060c06938904f0930600104e86da850d45052a1375f50f21c5414511a8'
        '8146014681452145312a9377f50f0545addbb250225492540259f249624ad24a'
        '424bb24b224c4561029093060010138604f0da850945fd201375f50f5dd12145'
        'c9bf83a7090091093e9ae39c34ff7d1993840410e31309f8418801455dd8b767'
        '00209c4be38747fb51bf2303048095472303f4802302a480828083076480e3ce'
        '07fe23030480828083076480e3ce07fe03454480828083076480e3ce07fe2302'
        'a4808280411126c44ac24ec006c61377f50bad47aa892e8995446306f7001945'
        '6d37653f8d444e854d37fd59fd1463983401b240a24412498249410182801355'
        '09011375f50f453f2209cdb7011126cc06ceb704080051371545853f71376937'
        '2ac6a53f32459377150089eb136515001375f50ff240e24405618280fd14e9fc'
        '0145cdbf397126dc4ada4ed852d656d45ad25ed006de62ce66ccb7e700e07d57'
        '83aa070022c603aa470023a0e71823a2e718b7170040130770052380e7041307'
        '80fa2380e70483c74704930b75ffb684e20793fbfb0f8546aa892e8b32893724'
        '0040e187015763fa76016308d5008946130700026313d5000157d98f93f7f70f'
        'b71c00402382fc0411472303e4801305f00f653d094cd135636d7c11b7050700'
        'da9537870700795563f2e504b3879500636ef702370b0800a94733eb65016399'
        'f90689e48144513d26850da0da850945553d05090345f9fffd14050b693d81c4'
        '9377fb0ffdf7dd3569fd7d55b7170040130770052380e704130780fa2380e704'
        '03c74704418b2382e704f250b7e700e023a0571123a24711e25432445259c259'
        '325aa25a125b825b724ce24c21618280a5476395f90685691309f00fb3069900'
        'b3742b01b6941349f9ffb3749900856b33796901416b9387f9ffb3f7270199e3'
        '63fc340193d94900c147e3e637ff99bf0569c1697d19d9b71305800d63886901'
        '130500026384790113051008ca85dd3b353d21dd4e99b3843441d9b7da852d45'
        'd533ca94e30099f205097d3ba30fa9fed5bf9387f9ff93f7f70f6361fc0c83c7'
        '1c0413073008631ff70437070800636beb04b3079b00b706100063f5d704334b'
        'eb0089476393f9068980d549e38c04ecda850945413b11090327c9ff91472320'
        'e48003076480e34e07fe23033481fd17edfbfd14110b81c49377fb0fe9ff7d33'
        '71f5e1b5b717004003c75704b70708001377070219e3b78707007955e378fbea'
        '33079b00e3ffe7f855b58547e382f9f2da852d45053b9389f4ffe38504e62933'
        '93f7390091eb832604800327090093074900e39ae6e43e89ce84f1bfb5476396'
        'f9001305900bd13135bdb1471305b00ae38bf9fe99476391f904b7050800b365'
        'bb002d45c53181448d4ba149753963967401832704802320f9008504e39834ff'
        '8327048013172b01635507002312f900d5bb2322f900fdb39d476399f9028145'
        '1305b0044531bd442320090023220900fd599d3993f77400ca9703c70700fd14'
        '398d2380a700e39634ff6dbba1476392f9046d398144630d0b008d479304c003'
        '6308fb009304000563048b01930440041375c507e30895d81945013939390545'
        '293126850d3909453d314931e31c05d669bb91476399f90013056006fd36f53e'
        '130590090db7e38f09d4f154a9bb'
        )

blink_bin = bytes.fromhex(
        '6f00200c000000006801000068010000a9bdf9f5680100000000000000000000'
        '6801000068010000000000000000000068010000000000006801000000000000'
        '6801000068010000680100006801000068010000680100006801000000000000'
        '6801000068010000680100006801000068010000680100006801000068010000'
        '6801000068010000680100006801000037f800e0032788008327c800b305e500'
        '33b5a5003e95032688008326c8003307b6403337e600b387a640998fe3c507fe'
        '8280970100209381a1331761002013016173138541c0938541c06377b5002320'
        '05001105e36db5fe1305000997650020938545f1176600201306c6f063fac500'
        '8322050023a0550011059105e3eac5fe1305c025938541c0138641c0638ac500'
        '8322050023a0550011059105e3eac5fe93070000fd42739002bc896293828288'
        '73a00230970200009382c2eb93e2320073905230b7f700e0054798c39307a01f'
        '739017347300203001a0b717004093870704130670052380c700930680fa2380'
        'd70001000100371700408345b7041307b70493f5f50d2300b700238007000100'
        '01002380c7002380d70001000100b705140037150040938585040cc523800700'
        '010001000100010001000100b725004013052005a383a5802380c7002380d700'
        '010001008346070093e606082300d70023800700010001008280411122c426c2'
        '06c6a537b71700409387470b9843b7140040371400401377f7ef98c3b7170040'
        '9387070a98439384c40a1304840a1367071098c39c4037c503001305c56c93e7'
        '07109cc0b1351c40375522001305055193e707101cc02d3df1bf0000'
    )

def wch_link_command(cmd):
    device.write(CH_USB_EP_OUT, cmd)
    return list( device.read(CH_USB_EP_IN, CH_USB_PACKET_SIZE, CH_USB_TIMEOUT) )

def wch_link_send_data(data):
    padding_len = CH_USB_PACKET_SIZE - (len(data) % CH_USB_PACKET_SIZE)
    data += bytes([0xff] * padding_len)
    for b in range(0, len(data), CH_USB_PACKET_SIZE):
        device.write(CH_USB_EP_OUT_DATA, data[b:b +CH_USB_PACKET_SIZE])

def toggle_power(do_3v, do_5v):
    if do_3v:
        assert wch_link_command((0x81, 0x0d, 0x01, 0x0a)) == [0x82, 0x0d, 0x01, 0x0a]
        sleep(0.1)
        assert wch_link_command((0x81, 0x0d, 0x01, 0x09)) == [0x82, 0x0d, 0x01, 0x09]
    if do_5v:
        assert wch_link_command((0x81, 0x0d, 0x01, 0x0c)) == [0x82, 0x0d, 0x01, 0x0c]
        sleep(0.1)
        assert wch_link_command((0x81, 0x0d, 0x01, 0x0b)) == [0x82, 0x0d, 0x01, 0x0b]

def prog_init():
    prog_info = wch_link_command(CH_STR_PROG_DETECT)
    # print( [hex(x) for x in prog_info] )
    if prog_info[5] == 18:
        print(f'* linkE v{prog_info[3]}.{prog_info[4]} found')
    assert wch_link_command(CH_STR_PROG_SPEED) == [0x82, 0x0c, 0x01, 0x01]

def chip_init():
    assert wch_link_command(CH_STR_CHIP_DETECT)[3] == CHIPTYPE_CH59x
    print('* ch59x found, set speed to 4000kHz')
    assert wch_link_command(CH_STR_CHIP_SPEED) == [0x82, 0x0c, 0x01, 0x01]

def flash(fw = None):
    fw_bin = blink_bin
    if fw:
        fw_bin = open(fw, 'rb').read()
        print(f'* flashing {fw} (len={len(fw_bin)})')
    else:
        print('* flashing blinky example')
    assert wch_link_command(CH_STR_FLASH_PREP + tuple(len(fw_bin).to_bytes(4))) == [0x82, 0x01, 0x01, 0x01]

    assert wch_link_command((0x81, 0x02, 0x01, 0x05)) == [0x82, 0x02, 0x01, 0x05] # what's this?

    wch_link_send_data(flashloader)

    assert wch_link_command((0x81, 0x02, 0x01, 0x07)) == [0x82, 0x02, 0x01, 0x07] # what's this?
    assert wch_link_command((0x81, 0x02, 0x01, 0x02)) == [0x82, 0x02, 0x01, 0x02] # what's this?

    wch_link_send_data(fw_bin)

    assert wch_link_command((0x81, 0x02, 0x01, 0x08)) == [0x82, 0x02, 0x01, 0x08] # what's this?

def dump(address, length):
    address = int(address, 16)
    if not length:
        length = 4
    elif length[:2] == '0x':
        length = int(length, 16)
    else:
        length = int(length)
    length += (4 - (length % 4)) if length % 4 else 0
    address -= address % 4

    cmd = [0x81, 0x03, 0x08] + list(address.to_bytes(4)) + list(length.to_bytes(4))
    wch_link_command(cmd)
    assert wch_link_command((0x81, 0x02, 0x01, 0x0c)) == [0x82, 0x02, 0x01, 0x0c] # what's this?
    res = array('I', bytes( device.read(CH_USB_EP_IN_DATA, CH_USB_PACKET_SIZE, CH_USB_TIMEOUT) ))
    res.byteswap()
    print(f'{address:08x}: {[hex(x) for x in res.tobytes()]}'.replace("'",""))

def open_terminal():
    assert wch_link_command((0x81, 0x08, 0x06, 0x10, 0x80, 0x00, 0x00, 0x01, 0x02)) == [0x82, 0x08, 0x06, 0x10, 0x80, 0x00, 0x00, 0x01, 0x00]
    assert wch_link_command((0x81, 0x08, 0x06, 0x10, 0x80, 0x00, 0x00, 0x03, 0x02)) == [0x82, 0x08, 0x06, 0x10, 0x80, 0x00, 0x00, 0x03, 0x00]

    assert wch_link_command((0x81, 0x08, 0x06, 0x10, 0x80, 0x00, 0x00, 0x01, 0x02)) == [0x82, 0x08, 0x06, 0x10, 0x80, 0x00, 0x00, 0x01, 0x00]
    assert wch_link_command((0x81, 0x08, 0x06, 0x16, 0x00, 0x00, 0x07, 0x00, 0x02)) == [0x82, 0x08, 0x06, 0x16, 0x00, 0x00, 0x07, 0x00, 0x00]

    assert wch_link_command((0x81, 0x08, 0x06, 0x10, 0x40, 0x00, 0x00, 0x01, 0x02)) == [0x82, 0x08, 0x06, 0x10, 0x40, 0x00, 0x00, 0x01, 0x00]

    try:
        while True:
            res = wch_link_command(CH_STR_POLL_DEBUG)
            if res[7] == 0x85:
                print(chr(res[6]), end='')
                assert wch_link_command(CH_STR_ACK1_DEBUG) == [0x82, 0x08, 0x06, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00]
                assert wch_link_command(CH_STR_ACK2_DEBUG) == [0x82, 0x08, 0x06, 0x04, 0x00, 0x00, 0x00, 0x04, 0x00]
    except KeyboardInterrupt:
        print()

def reset():
    assert wch_link_command((0x81, 0x0b, 0x01, 0x01)) == [0x82, 0x0b, 0x01, 0x01]
    assert wch_link_command((0x81, 0x0d, 0x01, 0xff)) == [0x82, 0x0d, 0x01, 0xff]

if __name__ == '__main__':
    main()
