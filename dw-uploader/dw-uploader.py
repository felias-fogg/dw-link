#!/usr/bin/env python
"""
Uploader for dw-link firmware
Based on STK500v1 implementation byMathieu Virbel <mat@meltingrocks.com>
"""

import os
import sys
import serial
import serial.tools.list_ports
import struct
import progressbar
from intelhex import IntelHex
from time import sleep

STK_LOAD_ADDRESS = 0x55
STK_PROG_PAGE = 0x64
STK_READ_PAGE = 0x74
STK_READ_SIGN = 0x75
STK_LEAVE_PROGMODE = 0x51
STK_GET_PARAMETER = 0x41

# get parameter
STK_SW_MAJOR = 0x81
STK_SW_MINOR = 0x82

STK_GET_SYNC = 0x30

Sync_CRC_EOP = 0x20
Resp_STK_INSYNC = 0x14
Resp_STK_OK = 0x10

MEM_PARTS_328P = {
    "flash": {
        "size": 32768,
        "pagesize": 128,
        "pagecount": 256,
    }
}

DEBUG = "DEBUG" in os.environ

if DEBUG:
    def debug_print(x, *largs):
        print(x.format(*largs))
else:
    def debug_print(*largs):
        pass


class ProtocolException(Exception):
    pass


class Uploader(object):
    def __init__(self, device):
        self.device = device
        self._seq = -1
        self.ih = IntelHex()
        super(Uploader, self).__init__()

    def try_connect(self):
        self.con = None
        print("Connecting to bootloader...")
        self.con = serial.Serial(
            self.device,
            baudrate=115200,
            bytesize=serial.EIGHTBITS,
            parity=serial.PARITY_NONE,
            stopbits=serial.STOPBITS_ONE,
            timeout=0.3,
            write_timeout=0.3,
            xonxoff=0,
            dsrdtr=0,
            rtscts=0)
        self.con.timeout = 0.4
        
        for x in range(3):
            try:
                self.get_sync()
                break
            except Exception as e:
                print("Exception %s" % e)
                pass
        print("Major: {}".format(self.get_parameter(STK_SW_MAJOR)))
        print("Minor: {}".format(self.get_parameter(STK_SW_MINOR)))
        sign = self.read_sign()
        print("Signature: 0x{:02x}{:02x}{:02x}".format(*sign))
        if sign not in [(0x1e, 0x95, 0xf), (0x1e, 0x95, 0x14), (0x1e, 0x95, 0x16)]:
            print("Not an ATmega328(P)(B)!")
            exit(1)

        

    def try_close(self):
        if self.con:
            self.con.close()
            self.con = None

    def upload(self, fname):
        self.try_connect()

        self.ih.loadhex(fname)

        memtype = "flash"
        mem = MEM_PARTS_328P[memtype]
        assert(mem["pagesize"] != 0)

        buf = self.ih.tobinarray()
        prog_size = len(buf)

        # flash the device
        assert(mem["pagesize"] * mem["pagecount"] == mem["size"])
        progress = 0
        bar = progressbar.ProgressBar(
            widgets=[
                'Upload: ',
                progressbar.Bar(),
                ' ',
                progressbar.Counter(format='%(value)02d/%(max_value)d'),
                ' ',
                progressbar.FileTransferSpeed(),
                ' ',
                progressbar.ETA()
            ]
        )
        r = range(0, prog_size + mem["pagesize"], mem["pagesize"]) #mem["size"]
        for addr in bar(r, ):
            if addr > prog_size:
                break
            page = buf[addr:addr + mem["pagesize"]]
            self.load_addr(addr)
            self.prog_page(memtype, page)

        print("\nLeaving programming mode")
        self.leave_progmode()

        print("All done!")

    def load_addr(self, addr):
        debug_print("[STK500] Load address {:06x}".format(addr))
        addr = addr >> 1
        pkt = struct.pack(
            "BBBB",
            STK_LOAD_ADDRESS,
            addr & 0xff,
            (addr >> 8) & 0xff,
            Sync_CRC_EOP)
        self.write(pkt)
        if self.readbyte() != Resp_STK_INSYNC:
            raise ProtocolException("load_addr() can't get into sync")
        if self.readbyte() != Resp_STK_OK:
            raise ProtocolException("load_addr() protocol error")

    def prog_page(self, memtype, data):
        debug_print("[STK500] Prog page")
        assert(memtype == "flash")
        block_size = len(data)
        pkt = struct.pack(
            "BBBB",
            STK_PROG_PAGE,
            (block_size >> 8) & 0xff,
            block_size & 0xff,
            ord("F"),  # because flash, othersize E for eeprom
        )
        pkt += bytearray(data)
        pkt += struct.pack("B", Sync_CRC_EOP)
        self.write(pkt)
        if self.readbyte() != Resp_STK_INSYNC:
            raise ProtocolException("prog_page() can't get into sync")
        if self.readbyte() != Resp_STK_OK:
            raise ProtocolException("prog_page() protocol error")

    def read_page(self, memtype, data):
        debug_print("[STK500] Read page")
        assert(memtype == "flash")
        block_size = len(data)
        pkt = struct.pack(
            "BBBBB",
            STK_READ_PAGE,
            (block_size >> 8) & 0xff,
            block_size & 0xff,
            ord("F"),  # because flash, othersize E for eeprom
            Sync_CRC_EOP
        )
        self.write(pkt)
        if self.readbyte() != Resp_STK_INSYNC:
            raise ProtocolException("read_page() can't get into sync")
        data[:] = self.read(block_size)
        if self.readbyte() != Resp_STK_OK:
            raise ProtocolException("read_page() protocol error")

    def get_sync(self):
        print("Try to get sync")
        pkt = struct.pack("BB", STK_GET_SYNC, Sync_CRC_EOP)
        for i in range(5):
            sleep(.2)
            self.con.flush()
            self.write(pkt)
            try:
                if self.readbyte() != Resp_STK_INSYNC:
                    raise ProtocolException("read_page() can't get into sync")
                if self.readbyte() != Resp_STK_OK:
                    raise ProtocolException("read_page() protocol error")
                print("Connected to bootloader")
                return
            except Exception as e:
                pass
        raise ProtocolException("STK500: cannot get sync")

    def get_parameter(self, param):
        debug_print("[STK500] Get parameter {:x}".format(param))
        self.write(struct.pack("BBB", STK_GET_PARAMETER, param, Sync_CRC_EOP))
        if self.readbyte() != Resp_STK_INSYNC:
            raise ProtocolException("get_parameter() can't get into sync")
        val = self.readbyte()
        if self.readbyte() != Resp_STK_OK:
            raise ProtocolException("get_parameter() protocol error")
        return val

    def read_sign(self):
        debug_print("[STK500] Read signature")
        self.write(struct.pack("BB", STK_READ_SIGN, Sync_CRC_EOP))
        if self.readbyte() != Resp_STK_INSYNC:
            raise ProtocolException("read_sign() can't get into sync")
        sign = struct.unpack("BBB", self.read(3))
        if self.readbyte() != Resp_STK_OK:
            raise ProtocolException("read_sign() protocol error")
        return sign

    def leave_progmode(self):
        debug_print("[STK500] Leaving programming mode")
        pkt = struct.pack("BB",
            STK_LEAVE_PROGMODE,
            Sync_CRC_EOP)
        self.write(pkt)
        if self.readbyte() != Resp_STK_INSYNC:
            raise ProtocolException("leave_progmode() can't get into sync")
        if self.readbyte() != Resp_STK_OK:
            raise ProtocolException("leave_progmode() protocol error")

    def readbyte(self):
        while True:
            ret = self.read(1)
            if ret != b'!':
                b = struct.unpack("B", ret)[0]
                return b

            buf = b''
            index = 0
            while True:
                # print("readbyte line")
                ret = self.read(1)
                # print("ret=", ret)
                if ret == b'\n':
                    if buf:
                        buf = buf.replace(b'\r', '')
                        print("[......] {!r}".format(buf))
                    break
                buf += ret

    def read(self, size):
        read = 0
        buf = bytearray(b"\x00" * size)
        while read < size:
            ret = self.con.read(size - read)
            if ret == b"":
                raise Exception("no data read, timeout? (read={} wanted={})".format(
                    read, size))
            buf[read:read + len(ret)] = ret
            read += len(ret)
        return bytes(buf[:read])

    def write(self, pkt):
        pkt = bytearray(pkt)
        debug_print("[STK500] Packet[{}] {}".format(len(pkt), " ".join(
            ["{:02x}".format(x) for x in pkt])))

        # don't write too fast or we loose data
        for i in range(0, len(pkt), 32):
            self.con.write(pkt[i:i + 32])
            sleep(0.05)


def upload(device, fname):
    if device is None:
        return 0
    try:
        Uploader(device).upload(fname)
    except KeyboardInterrupt:
        print("Goodbye")
        exit(0)
    except ProtocolException as e:
        print(e)
    except Exception as e:
        print(e)

def discover():
    ports = []
    for s in serial.tools.list_ports.comports(True):
        if s.device.startswith('/dev/tty.') or s.device in ["/dev/cu.Bluetooth-Incoming-Port",
                                                                "/dev/cu.debug-console"]:
            continue
        ports += [ s.device ]
    debug_print("[STK500] Discovered ports: %s" % ports)
    if ports == []:
        print("No open serial ports discovered")
        return None
    if len(ports) == 1:
        print("Will use %s for upload" % ports[0])
        return ports[0]
    print("Choose from:")
    for p in enumerate(ports):
        print(p)
    try:
        c = int(input("Choice: "))
    except:
        c = -1
    if c < 0 or c >= len(ports):
        print("Impossible choice")
        return None
    return ports[c]


if __name__ == "__main__":
    upload(discover(), os.path.abspath(os.path.join(os.path.dirname(__file__), "dw-link.hex")))