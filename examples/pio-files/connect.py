import serial.tools.list_ports
import time
import gdb

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
                            gdb.execute("set serial baud " + str(sp))
                            gdb.execute("target extended-remote " + s.device)
                            return
            except:
                pass
    print("No dw-link adapter found")
connect()
