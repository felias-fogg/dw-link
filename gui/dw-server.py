#!/usr/bin/env python3
#
# Discover dw-link and then redirect data from a TCP/IP connection to the serial port and vice versa.
# Based on Chris Liechti's tcp_serial_redirct script

import sys
import socket
import serial
import serial.threaded
import time
import serial.tools.list_ports
import shutil, shlex, subprocess

class SerialToNet(serial.threaded.Protocol):
    """serial->socket"""

    def __init__(self):
        self.socket = None

    def __call__(self):
        return self

    def data_received(self, data):
        if self.socket is not None:
            self.socket.sendall(data)

# discovers the dw-link adapter, if present
def discover():
    for delay in (0, 2):
        for s in serial.tools.list_ports.comports(True):
            try:
                for sp in (115200, 230400):
                    with serial.Serial(s.device, sp, timeout=0.1, write_timeout=0.1, exclusive=True) as ser:
                        time.sleep(delay)
                        ser.write(b'\x05') # send ENQ
                        if ser.read(7) == b'dw-link': # if we get this response, it must be an dw-link adapter
                            ser.close()
                            return (sp, s.device)
            except:
                pass
    return (None, None)

if __name__ == '__main__':
    import  argparse

    # parse arguments
    parser = argparse.ArgumentParser(description='TCP/IP server for dw-link adapter')
    parser.add_argument('-p', '--port',  type=int, default=2000, dest='port',
                            help='Local port on machine (default 2000)')
    parser.add_argument('-n', '--nosub', action='store_true', dest='nosubs',
                            help='Do not start any subprocess')
    parser.add_argument('-s', '--sub',  dest='sub', default='gede --no-run',
                            help='Start specified subprocess (default gede)')
    args=parser.parse_args()

    # discover adapter
    speed, device = discover()
    if speed == None or device == None:
        sys.stderr.write('--- No dw-link adapter discovered ---\n')
        exit(2)
    
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
        sys.exit(1)

    ser_to_net = SerialToNet()
    serial_worker = serial.threaded.ReaderThread(ser, ser_to_net)
    serial_worker.start()

    if not args.nosubs:
        cmd = shlex.split(args.sub)
        cmd[0] = shutil.which(cmd[0])

    subprc = subprocess.Popen(cmd)

    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(('', args.port))
    srv.listen(1)
    try:
        intentional_exit = False
        while True:
            sys.stderr.write('Waiting for connection on {}...\n'.format(args.port))
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
                        if subprc.poll() != None:
                            break
                        data = client_socket.recv(1024)
                        if not data:
                            break
                        ser.write(data)                 # get a bunch of bytes and send them
                    except socket.error as msg:
                        sys.stderr.write('ERROR: {}\n'.format(msg))
                        # probably got disconnected
                        break
            except KeyboardInterrupt:
                intentional_exit = True
                raise
            except socket.error as msg:
                sys.stderr.write('ERROR: {}\n'.format(msg))
            finally:
                ser_to_net.socket = None
                sys.stderr.write('Disconnected\n')
                client_socket.close()
                if args.sub:
                    break
    except KeyboardInterrupt:
        pass

    sys.stderr.write('\n--- exit ---\n')
    serial_worker.stop()
