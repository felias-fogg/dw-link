import serial.tools.list_ports
import time

def connect():
    for delay in (0, 2):
        for s in serial.tools.list_ports.comports(True):
            try:
                for sp in (115200, 230400):
                    with serial.Serial(s.device, sp, timeout=0.1, write_timeout=0.1, exclusive=True) as ser:
                        time.sleep(delay)
                        ser.write(b'\x05')
                        if ser.read(7) == b'dw-link':
                            ser.close()
                            with open(".gdb-connect-to-dw-link", "w") as cmd:
                                 cmd.write("set serial baud " + str(sp) + "\n")
                                 cmd.write("target extended-remote " + s.device +"\n")
                            return
            except:
                pass
    with open(".gdb-connect-to-dw-link", "w") as cmd:
         cmd.write('print "No dw-link adapter found"\n')
connect()
