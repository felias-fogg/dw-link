#!/usr/bin/env python3
#
# Discover dw-link and then redirect data from a TCP/IP connection to the serial port and vice versa.
# Based on Chris Liechti's tcp_serial_redirct script
#
# Version 1.2.4 (22-Dec-2024)
# - New option -V gives version number
# - Now uses the gdb_port value when cortex-debug calls the script
#
# Version 1.2.3 (21-Dec-2024)
# - Fatal error messages from dw-link will now be shown by dw-server (they have the "***" prefix in an O-record)
# - The serial port is now shown when dw-server discovers dw-link
#
# Version 1.2.2 (20-Dec-2024)
#
# - dw-server will now "emulate" an openocd server if "gdb_port 50000" is mentioned as one of the arguments
# - it will ignore the port "/dev/cu.Bluetooth-Incoming-Port" under macOS
# - it has a new option: -v or --verbose, where everything transmitted is shown
#
# Version 1.2.1 (17-Dec-2024)
# - under Linux, we have to send the ENQ twice before we get an answer, I have no idea why
#
# Version 1.2.0 (21-sep-2023)
# - does not call gede with the (now obsolete) --no-run option; thze -g option still works, though
#
# Version 1.1.0
# - special option for calling gede with the --no-run option
#
# Version 1.0.0
# - first working version
#

VERSION = "1.2.4"

import sys
import socket
import serial
import serial.threaded
import time
import serial.tools.list_ports
import shutil, shlex, subprocess
import  argparse


class SerialToNet(serial.threaded.Protocol):
    """serial->socket"""

    def __init__(self):
        self.socket = None
        self.last = b""

    def __call__(self):
        return self

    def data_received(self, data):
        if self.socket is not None:
            self.socket.sendall(data)
            self.last += data
            if self.last:
                    if self.last[-1] == ord('+') or (len(self.last) > 2 and self.last[-3] == ord('#')):
                        if len(self.last) > 2 and self.last[1] == ord('O') and self.last[2] != ord('K'):
                            message = self.convert_gdb_message()
                        else:
                            message = ""
                        if verbose:
                            sys.stdout.write("repl: {}\n".format(self.last))
                            if message:
                                sys.stdout.write("dw-link message: {}\n".format(message))
                            sys.stdout.flush()
                        elif len(message) > 2 and message[:3] == '***':
                            sys.stderr.write("dw-link message: {}\n".format(message))
                            sys.stderr.flush()
                        self.last = b""

    def convert_gdb_message(self):
        bs = self.last[2:self.last.find(b'#')]
        hv = bs.decode('utf-8')
        bv = bytes.fromhex(hv)
        return bv.decode('utf-8')
    
# discovers the dw-link adapter, if present
def discover(verbose):
    for delay in (0.2, 2):
        for s in serial.tools.list_ports.comports(True):
            if verbose:
                sys.stdout.write("Device: {}\n".format(s.device))
                sys.stdout.flush()
            if s.device == "/dev/cu.Bluetooth-Incoming-Port":
                continue
            if verbose:
                sys.stdout.write("Check:{}\n".format(s.device))
                sys.stdout.flush()
            try:
                for sp in (115200, ):
                    with serial.Serial(s.device, sp, timeout=0.1, write_timeout=0.1, exclusive=True) as ser:
                        time.sleep(delay)
                        ser.write(b'\x05') # send ENQ
                        resp = ser.read(7) # under Linux, the first response might be empty
                        if resp != b'dw-link':
                            time.sleep(0.2)
                            ser.write(b'\x05') # try again sending ENQ                        
                            resp = ser.read(7) # now it should be the right response!
                        if resp == b'dw-link': # if we get this response, it must be an dw-link adapter
                            return (sp, s.device)
            except:
                pass
    return (None, None)

if __name__ == '__main__':

    # before parsing the argument, check whether we should "emulate" an openocd server
    if any("cortex-debug" in arg for arg in sys.argv): # called by cortex-debug -> pretend to be an openocd server
        prg = ""
        gede = False
        cortex = True
        verbose = False
        portargs = [arg for arg in sys.argv if "gdb_port" in arg]
        if len(portargs) == 0:
            sys.stderr.write('No port argument given\n')
            sys.exit(1)
        else:
            port = int(portargs[0][9:])
    else:
        # parse arguments
        cortex = False
        parser = argparse.ArgumentParser(description='TCP/IP server for dw-link adapter (Version ' + VERSION + ')')
        parser.add_argument('-p', '--port',  type=int, default=2000, dest='port',
                                help='local port on machine (default 2000)')
        parser.add_argument('-s', '--start',  dest='prg', 
                                help='start specified program')
        parser.add_argument('-g', '--gede',  action="store_true",
                                help='start gede')
        parser.add_argument('-v', '--verbose', action="store_true",
                                help="log all communcation")
        parser.add_argument('-V', '--version', action="store_true",
                                help="print version number and exit immediately")
        args=parser.parse_args()
        port = args.port
        prg = args.prg
        gede = args.gede
        verbose = args.verbose

        if args.version:
            sys.stdout.write("{}\n".format(VERSION))
            sys.stdout.flush()
            sys.exit(0)
        
    # discover adapter
    speed, device = discover(verbose)
    if speed == None or device == None:
        sys.stderr.write('--- No dw-link adapter discovered ---\n')
        sys.exit(1)
    
    # connect to serial port
    ser = serial.serial_for_url(device, do_not_open=True)
    ser.baudrate = speed
    ser.bytesize = 8
    ser.parity = 'N'
    ser.stopbits = 1
    ser.rtscts = False
    ser.xonxoff = False

    try:
        ser.open()
    except serial.SerialException as e:
        sys.stderr.write('Could not open serial port {}: {}\n'.format(device, e))
        sys.exit(2)

    ser_to_net = SerialToNet()
    serial_worker = serial.threaded.ReaderThread(ser, ser_to_net)
    serial_worker.start()

    if gede:
        prg = "gede"

    if prg:
        cmd = shlex.split(prg)
        cmd[0] = shutil.which(cmd[0])
        subprc = subprocess.Popen(cmd)

    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(('', port))
    srv.listen(1)
    try:
        while True:
            sys.stdout.write("\n\rDw-link ready on {}, waiting for GDB connection.\r\n".format(device))
            sys.stdout.write("Info : Listening on port {} for gdb connection\n\r".format(port))
            sys.stdout.flush()
            
            client_socket, addr = srv.accept()
            sys.stderr.write('Connected by {}\n'.format(addr))
            # More quickly detect bad clients who quit without closing the
            # connection: After 1 second of idle, start sending TCP keep-alive
            # packets every 1 second. If 3 consecutive keep-alive packets
            # fail, assume the client is gone and close the connection.
            try:
                client_socket.setsockopt(socket.IPPROTO_TCP, socket.TCP_KEEPIDLE, 1)
                client_socket.setsockopt(socket.IPPROTO_TCP, socket.TCP_KEEPINTVL, 1)
                client_socket.setsockopt(socket.IPPROTO_TCP, socket.TCP_KEEPCNT, 3)
                client_socket.setsockopt(socket.SOL_SOCKET, socket.SO_KEEPALIVE, 1)
            except AttributeError:
                pass # XXX not available on windows
            client_socket.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
            try:
                ser_to_net.socket = client_socket
                # enter network <-> serial loop
                while True:
                    try:
                        data = client_socket.recv(1024)
                        if not data:
                            break
                        ser.write(data)                 # get a bunch of bytes and send them
                        if verbose:
                            sys.stdout.write("send: {}\n".format(data))
                            sys.stdout.flush()
                    except socket.error as msg:
                        sys.stderr.write('ERROR: {}\n'.format(msg))
                        # probably got disconnected
                        break
            except socket.error as msg:
                sys.stderr.write('ERROR: {}\n'.format(msg))
            finally:
                ser_to_net.socket = None
                sys.stderr.write('Disconnected\n')
                ser.write(b'$D#44') # send detach command to dw-link debugger
                client_socket.close()
                break
    except KeyboardInterrupt:
        pass

    sys.stderr.write('\r\n--- exit ---\r\n')
    serial_worker.stop()
