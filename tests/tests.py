#!/usr/bin/env python3
import pexpect 
import sys
import os
import time
import operator

logging = False

# runs all the unit tests of the debugger
# this script should be run first because it
# sets clock source and CKDIV8
unit_script = ("unit tests", "",
    ("set style enabled off", ""),
    ("set trace-commands on", ""),
    ("set logging file unit.log", ""),
    ("set logging overwrite off", ""),
    ("set logging on",  ""),
    ("target remote $PORT", "0x00000000 in ?? ()"),
    ("monitor dwconnect", "debugWIRE is now enabled"),
    ("monitor reset", ""),
    ("monitor $CKFUSE", "fuse is now "),
    ("monitor $CKSOURCE", "Clock source is now"),
    ("monitor testall", "All tests succeeded"),
    ("detach",  "[Inferior 1 (Remote target) detached]"),
    ("quit", ""))


# tests breaks in ISRs
# tests asynchronous stop
# tests display
blink_script =("blink test", "blink", 
    ("set style enabled off", ""),
    ("set trace-commands on", ""),
    ("set logging file blink.log", ""),
    ("set logging overwrite off", ""),
    ("set logging on",  ""),
    ("target remote $PORT", "0x00000000 in __vectors ()"),
    ("monitor dwconnect", "debugWIRE is now enabled"),
    ("monitor reset", ""),
    ("load", "Start address 0x00000000,"),
    ("list blink.ino:10", "ISR(TIMER0_COMPA_vect)"),
    ("break blink.ino:13",  "line 13"),
    ("continue",  "Breakpoint 1, __vector_"),
    ("display privmillis", "privmillis = 0"),
    ("continue", "privmillis ="),
    ("print cycle", "$1 = "),
    ("break loop", "Breakpoint 2 at"),
    ("delete 1", ""),
    ("continue", "Breakpoint 2, loop"),
    ("next", "mydelay(1000)"),
    ("break 14", "Breakpoint 3 at"),
    ("continue", "if (cycle < 5)" ),
    ("continue", "if (cycle < 5)" ),
    ("print cycle", "$2 = " ),
    ("delete 3", ""),
    ("break 64", "Breakpoint 4 at"),
    ("cond 4 cycle == 4", ""),
    ("disable 2", ""),
    ("info b",  "stop only if cycle == 4"),
    ("continue", "Breakpoint 4, loop"),
    ("next", ""),
    ("print cycle", "$3 = 5 "),
    ("break 13", ""),
    ("continue", "13"),
    ("delete 1-10", "No breakpoint number 10"),
    ("continue&", ""),
    ("$SLEEP", 5),
    ("interrupt", ""),
    ("$SLEEP", 0.1),   
    ("", "Program received signal SIGINT, Interrupt"), # this message comes asynchronously
    ("i b","No breakpoints or watchpoints"),
    ("detach", "[Inferior 1 (Remote target) detached]"),
    ("quit", ""))

# test whether flash memory is loaded up without any error
flash_script = ("flash test", "flashtest",
    ("set style enabled off", ""),
    ("set trace-commands on", ""),
    ("set logging file flash.log", ""),
    ("set logging overwrite off", ""),
    ("set logging on",  ""),
    ("target remote $PORT", "0x00000000 in __vectors ()"),
    ("monitor dwconnect", "debugWIRE is now enabled"),
    ("monitor reset", ""),
    ("load", "Start address 0x00000000,"),
    ("break flashOK", "Breakpoint 1 at"),
    ("break flasherror", "Breakpoint 2 at"),
    ("continue", "Breakpoint 1, flashOK ()"),
    ("detach", "[Inferior 1 (Remote target) detached]"),
    ("quit", ""))

# test recursive functions
# test up/down
# test conditional stop
# test run command
# software watchpoint
fib_script = ("fibonacci test", "fibonacci",
    ("set style enabled off", ""),
    ("set trace-commands on", ""),
    ("set logging file fib.log", ""),
    ("set logging overwrite off", ""),
    ("set logging on",  ""),
    ("target extended-remote $PORT", "0x00000000 in __vectors ()"),
    ("monitor dwconnect", "debugWIRE is now enabled"),
    ("monitor reset", ""),
    ("load", "Start address 0x00000000,"),
    ("list loop","int result, arg;"),
    ("b 62", "Breakpoint 1 at"),
    ("b 84", "Breakpoint 2 at"),
    ("cont", "Breakpoint 1, loop ()"),
    ("p result", "$1 = 13"),
    ("p callcnt", "$2 = 25"),
    ("continue", "84	  delay(result);"),
    ("p result", "$3 = 1"),
    ("p callcnt", "$4 = 1"),
    ("b fib", "line 11"),
    ("b mfib", "line 18"),
    ("dis 1", ""),
    ("continue", "Breakpoint 3, fib (n=7)"),
    ("continue", "Breakpoint 3, fib (n=6)"),
    ("continue", "Breakpoint 3, fib (n=5)"),
    ("continue", "Breakpoint 3, fib (n=4)"),
    ("continue", "Breakpoint 3, fib (n=3)"),
    ("up", "in fib (n=4)"),
    ("up", "in fib (n=5)"),
    ("up", "in fib (n=6)"),
    ("p n", "$5 = 6"),
    ("dis 3", ""),
    ("continue", "Breakpoint 4, mfib (n=7)"),
    ("set remote hardware-watchpoint-limit 0", ""),
    ("watch memo[7]", "Watchpoint 5: memo[7]"),
    ("continue", "Watchpoint 5: memo[7]"),
    ("p memo[7]", "$6 = 1"),    
    ("p memo", "$7 = {0, 0, 0, 0, 0, 0, 0}"),
    ("set memo[7] = 0", ""),
    ("p callcnt", "$8 = 0"),
    ("dis 4-5", ""),
    ("continue", "Breakpoint 2"),
    ("p result", "$9 = 13"),
    ("p callcnt", "$10 = 13"),
    ("enable 1", ""),
    ("set interactive-mode off", ""),
    ("run", "Breakpoint 1"),
    ("detach", "[Inferior 1 (Remote target) detached]"),
    ("quit", ""))

# test OOP debugging - make sure, that LTO is disabled!
# test maximal BP setting (set to 4!)
oop_script = ("oop test", "oop",
    ("set style enabled off", ""),
    ("set trace-commands on", ""),
    ("set logging file oop.log", ""),
    ("set logging overwrite off", ""),
    ("set logging on",  ""),
    ("target extended-remote $PORT", "0x00000000 in __vectors ()"),
    ("monitor dwconnect",  "debugWIRE is now enabled"),
    ("monitor reset", ""),
    ("load", "Start address 0x00000000,"),
    ("ptype r", "type = class Rectangle : public TwoDObject {"),
    ("print r", "$1 = {<TwoDObject> = {x ="),
    ("list setup", "setup(void)"),
    ("b 83", "Breakpoint 1"),
    ("b 92", "Breakpoint 2"),
    ("b 111", "Breakpoint 3"),
    ("b area", "(7 locations)"),
    ("b move", "Breakpoint 5"),
    ("monitor 4bp", "Maximum number of breakpoints now: 4"),
    ("continue", "Command aborted"),
    ("delete 4", ""),
    ("continue", "Breakpoint 1"),
    ("print r","$2 = {<TwoDObject> = {x = 10, y = 11}, height = 5, width = 8}"),
    ("continue", "Serial.println(F(\"Move s by +10, +10:\"));"),
    ("continue", "x = x + xchange;"),
    ("print xchange", "$3 = 10"),
    ("dis 1", ""),
    ("next", "y = y + ychange;"),
    ("next", "94"),
    ("continue", "Breakpoint 3"),
    ("print s", "$4 = {<Rectangle> = {<TwoDObject> = {x = 15, y = 15}, height = 5,"),
    ("detach","[Inferior 1 (Remote target) detached]"),
    ("quit", ""))

#tests terminal I/O and heavy IRQ load
tictactoe_script = ("tictactoe test", "tictactoe",
    ("set style enabled off", ""),
    ("set trace-commands on", ""),
    ("set logging file tictactoe.log", ""),
    ("set logging overwrite off", ""),
    ("set logging on",  ""),
    ("target extended-remote $PORT", "0x00000000 in __vectors ()"),
    ("monitor dwconnect",  "debugWIRE is now enabled"),
    ("monitor reset", ""),
    ("load", "Start address 0x00000000,"),
    ("b tictactoe.ino:211","line 211"),
    ("b tictactoe.ino:176","line 176"),
    ("b minimax", "line 48"),
    ("c", "211"),
    ("set key='Y'", ""),
    ("p key", "$1 = 89 'Y'"),
    ("n", "215"),
    ("n", "219"),
    ("n", "210"),
    ("n", "211"),
    ("n", "212"),
    ("n", "return LEFTKEY"),
    ("c", "176"),
    ("set key='9'", ""), # we play 9
    ("c", "Breakpoint 3, minimax (player=1"),
    ("c", "Breakpoint 3, minimax (player=player@entry=-1"),
    ("disable 3", ""),
    ("c", "176"), # player played 5
    ("set key='3'", ""), # we play 3
    ("c", "176"), # player played 6
    ("set key='1'", ""), # we play 1
    ("c", "211"), # player playd 4 -- and won
    ("set key='N'", ""), # we do not want play any longer
    ("c", "211"),
    ("detach","[Inferior 1 (Remote target) detached]"),
    ("quit", ""))

# tests offline single-step execution 
# INT 0 is enabled and switched active by setting the IRQ pin as an output
isr_script = ("single-step test", "isr",
    ("set style enabled off", ""),
    ("set trace-commands on", ""),
    ("set logging file isr.log", ""),
    ("set logging overwrite off", ""),
    ("set logging on",  ""),
    ("target extended-remote $PORT", "0x00000000 in __vectors ()"),
    ("monitor dwconnect",  "debugWIRE is now enabled"),
    ("monitor reset", ""),
    ("load", "Start address 0x00000000,"),
    ("b loop", "line 53"),
    ("c", "Breakpoint 1"),
    ("n", "digitalWrite(IRQPIN,LOW);"),
    ("n", "55"),
    ("display outsidecount", "= 1"),
    ("display irqcount", ""),
    ("p (int)(irqcount == 0)", "$1 = 0"),
    ("set irqcount = 0", ""),
    ("n", "irqcount = 0"),
    ("n", "outsidecount = 2"),
    ("p (int)(irqcount == 1)", "$2 = 0"),
    ("detach", "[Inferior 1 (Remote target) detached]"),
    ("quit", ""))

# switch off debugWIRE mode
# this test script should be run last for each MCU/clock combination
dwoff_script = ("dwoff test", "",
    ("set style enabled off", ""),
    ("set trace-commands on", ""),
    ("set logging file dwoff.log", ""),
    ("set logging overwrite off", ""),
    ("set logging on",  ""),
    ("target extended-remote $PORT", "0x00000000 in ?? ()"),
    ("monitor dwoff",  "debugWIRE is now disabled"),
    ("detach","[Inferior 1 (Remote target) detached]"),
    ("quit", ""))

# the unit_script needs always to run first because it sets the clock frequency,
# the dwoff_script should run last in order to set the MCU back to normal mode
small_arduino = (unit_script, blink_script, flash_script, fib_script, isr_script, dwoff_script)
medium_arduino = (unit_script, blink_script, flash_script, fib_script, oop_script, isr_script, dwoff_script)
large_arduino =  (unit_script, blink_script, flash_script, fib_script, oop_script,
                      tictactoe_script, isr_script, dwoff_script)
exotic_avr = (unit_script, dwoff_script)

# combination of clock source + CKDIV8 setting + naming for the particular core
MicroRcAndExt = (("ext", "ck1", "16M"), ("rc", "ck1", "9M6"), ("rc", "ck8", "1M2"))
ATTOnlyRc = (("rc", "ck1", "8internal"), ("rc", "ck8",  "1internal"))
ATTRcAndExt = (("ext", "ck1", "16external"), ("rc", "ck1", "8internal"), ("rc", "ck8", "1internal"))
ATTRcAndXtal = (("xtal", "ck1", "16external"), ("rc", "ck1", "8internal"), ("rc", "ck8", "1internal"))
MiniRcAndXtal = (("xtal", "ck1", "16MHz_external"), ("rc", "ck1",  "8MHz_internal"), ("rc", "ck8", "1MHz_internal"))

attiny13 = ("ATtiny13", 2, -1, -1, MicroRcAndExt, small_arduino, "MicroCore:avr:13:clock=",
                "Programmer-ZF")
attiny2313 = ("ATtiny2313", 5, -1, -1, ATTRcAndExt, small_arduino, "ATTinyCore:avr:attinyx313:chip=2313,clock=",
                  "Programmer-ZF")
attiny4313 = ("ATtiny4313", 5, 2, 3, ATTRcAndExt, medium_arduino, "ATTinyCore:avr:attinyx313:chip=4313,clock=",
                 "Programmer-ZF")
attiny43u = ("ATtiny43U", -1, 17, 18, ATTOnlyRc,  medium_arduino, "ATTinyCore:avr:attiny43:clock=",
                  "Breakout Board")
attiny24 = ("ATtiny24", 2, 11, 12, ATTRcAndExt, small_arduino, "ATTinyCore:avr:attinyx4:chip=24,clock=",
                  "Programmer-ZF")
attiny44 = ("ATtiny44", 2, 11, 12, ATTRcAndExt, medium_arduino, "ATTinyCore:avr:attinyx4:chip=44,clock=",
                  "Programmer-ZF")
attiny84 = ("ATtiny84", 2, 11, 12, ATTRcAndExt, large_arduino, "ATTinyCore:avr:attinyx4:chip=84,clock=",
                  "Programmer-ZF")
attiny841 = ("ATtiny841", -1, 11, 12, ATTOnlyRc, large_arduino, "ATTinyCore:avr:attinyx41:chip=841,clock=",
                  "Breakout Board")
attiny441 = ("ATtiny441", -1, 11, 12, ATTOnlyRc, medium_arduino, "ATTinyCore:avr:attinyx41:chip=441,clock=",
                  "Breadboard setup")
attiny25 = ("ATtiny25", 2, 6, 5, ATTRcAndExt, small_arduino, "ATTinyCore:avr:attinyx5:chip=25,clock=",
                  "Programmer-ZF")
attiny45 = ("ATtiny45", 2, 6, 5, ATTRcAndExt, medium_arduino, "ATTinyCore:avr:attinyx5:chip=45,clock=",
                  "Programmer-ZF")
attiny85 = ("ATtiny85", 2, 6, 5, ATTRcAndExt, large_arduino, "ATTinyCore:avr:attinyx5:chip=85,clock=",
                  "Programmer-ZF")
attiny261 = ("ATtiny261", -1, 11, 12, ATTRcAndXtal, small_arduino, "ATTinyCore:avr:attinyx61:chip=261,clock=",
                 "Breakout Board")
attiny461 = ("ATtiny461", -1, 11, 12, ATTRcAndXtal, medium_arduino, "ATTinyCore:avr:attinyx61:chip=461,clock=",
                 "Breakout Board")
attiny861 = ("ATtiny861", -1, 11, 12, ATTRcAndXtal, large_arduino, "ATTinyCore:avr:attinyx61:chip=861,clock=",
                 "Breakout Board")
attiny87 = ("ATtiny87", -1, 1, 2, ATTRcAndXtal,  large_arduino, "ATTinyCore:avr:attinyx7:chip=87,clock=",
                 "Breakout Board")
attiny167 = ("ATtiny167", -1, 1, 2, ATTRcAndXtal,  large_arduino, "ATTinyCore:avr:attinyx7:chip=167,clock=",
                 "Breakout Board")
attiny48 = ("ATtiny48", -1, 13, 12, ATTOnlyRc,  medium_arduino, "ATTinyCore:avr:attinyx8:chip=48,clock=",
                "Programmer-ZF")
attiny88 = ("ATtiny88", -1, 13, 12, ATTOnlyRc,  large_arduino, "ATTinyCore:avr:attinyx8:chip=88,clock=",
                "Programmer-ZF")
attiny828 = ("ATtiny828", -1, 1, 2, ATTOnlyRc,  large_arduino, "ATTinyCore:avr:attiny828:clock=",
                 "Breakout Board")
attiny1634 = ("ATtiny1634", -1, 2, 1, ATTOnlyRc, large_arduino, "ATTinyCore:avr:attiny1634:clock=",
                "Breakout Board")


atmega48a = ("ATmega48A", -1, 2, 3, MiniRcAndXtal, medium_arduino, "MiniCore:avr:48:variant=modelNonP,bootloader=no_bootloader,clock=",
                 "ATmega-ZF")
atmega48pa = ("ATmega48PA", -1, 2, 3, MiniRcAndXtal, medium_arduino, "MiniCore:avr:48:variant=modelP,bootloader=no_bootloader,clock=",
                 "ATmega-ZF")
atmega88a = ("ATmega88A", -1, 2, 3, MiniRcAndXtal, large_arduino, "MiniCore:avr:88:variant=modelNonP,bootloader=no_bootloader,clock=",
                 "ATmega-ZF")
atmega88pa = ("ATmega88PA", -1, 2, 3, MiniRcAndXtal, large_arduino, "MiniCore:avr:88:variant=modelP,bootloader=no_bootloader,clock=",
                 "ATmega-ZF")
atmega168a = ("ATmega168A", -1, 2, 3, MiniRcAndXtal, large_arduino, "MiniCore:avr:168:variant=modelNonP,bootloader=no_bootloader,clock=",
                 "ATmega-ZF")
atmega168pa = ("ATmega168PA", -1, 2, 3, MiniRcAndXtal, large_arduino, "MiniCore:avr:168:variant=modelP,bootloader=no_bootloader,clock=",
                 "ATmega-ZF")
atmega328 = ("ATmega328", -1, 2, 3, MiniRcAndXtal, large_arduino, "MiniCore:avr:328:variant=modelNonP,bootloader=no_bootloader,clock=",
                 "ATmega-ZF")
atmega328p = ("ATmega328P", -1, 2, 3, MiniRcAndXtal, large_arduino, "MiniCore:avr:328:variant=modelP,bootloader=no_bootloader,clock=",
                 "ATmega-ZF")
atmega328pb = ("ATmega328PB", -1, 2, 3, MiniRcAndXtal, large_arduino, "MiniCore:avr:328:variant=modelPB,bootloader=no_bootloader,clock=",
                 "Breakout Board")

tinylist = (attiny13, attiny2313, attiny4313, attiny43u, attiny24, attiny44, attiny84, attiny841,
                 attiny25, attiny45, attiny85, attiny261, attiny461, attiny861, attiny87, attiny167,
                 attiny48, attiny88, attiny828, attiny1634)
megalist = (atmega48a, atmega48pa, atmega88a, atmega88pa, atmega168a, atmega168pa,  atmega328,
                atmega328p, atmega328pb)
exoticlist = ( )


# should be called in the 'tests' directory    
def test_mcu(port, description):
    mcu_name = description[0]
    external_clock_pin =  description[1]
    rx_pin = description[2]
    tx_pin = description[3]
    ck_list = description[4]
    script_list = description[5]
    board_name = description[6]
    setup = description[7]

    failed_comp = 0
    failed_scripts = 0
    tests_done = 0

    print("*** Setup " + mcu_name + " using " + setup + " ***")
    if external_clock_pin > 0: print("Connect external clock to pin", external_clock_pin)
    if rx_pin > 0: print("Connect serial RX at pin", rx_pin)
    if tx_pin > 0: print("Connect serial TX at pin", tx_pin)
    while (True):
        print("Type 'G' to start, 'S' to skip, 'X' to exit. ", end="")  
        response = input("Response: ")
        if response.lower().find("g") != -1: break;
        if response.lower().find("s") != -1: return (0,0,len(ck_list)*len(script_list))
        if response.lower().find("x") != -1: return (-1,-1,-1)
    for ck in ck_list:
        for script in script_list:
            exit_status = 0
            if script[1] != "":
                print("compile " + script[1] + " " + ck[2])
                cmd = "arduino-cli compile -b " + board_name + ck[2] + \
                  ' -e --build-property="build.extra_flags=-Og -fno-lto" --output-dir ' + \
                  script[1] + " " + script[1]
                if logging: print("COMMAND=",cmd)
                (cmd_out, exit_status) = pexpect.run(cmd, withexitstatus=1)
            tests_done += 1
            if exit_status != 0:
                print("FAILED compilation: ", cmd_out)
                failed_comp += 1
                continue
            if not run_script(script, port, ck[0], ck[1]):
                failed_scripts += 1
    print(mcu_name + " test: ", end='')
    if failed_comp + failed_scripts == 0:
        print("OK")
    else:
        print("*** SOME FAILURES ****")
    return(failed_comp, failed_scripts, tests_done)
                            
# should be called in the 'tests' directory
def run_script(script, port, cksource, ckfuse):
    testname = script[0]
    print("Running " + testname + ": " + port + " / " + cksource + " / " + ckfuse ) 
    testbinary = script[1] + "/" + script[1] + ".ino.elf"
    child = pexpect.spawn("avr-gdb " + testbinary + " -b 230400 -n")
    ix = 2
    resp = child.expect(["\(gdb\)",pexpect.TIMEOUT,pexpect.EOF],timeout=2)
    if (resp >= 1):
        print("Failed " + testname + ", calling avr-gdb")
        return False
    while ix < len(script):
        if not logging:
            sys.stdout.write(str(ix-1))
            sys.stdout.write(" ")
            sys.stdout.flush()
        interact = script[ix]
        cmd = interact[0].replace("$PORT",port).replace("$CKFUSE",ckfuse).replace("$CKSOURCE",cksource)
        if cmd == "$SLEEP":
            if logging: print("\nCOMMAND: Sleep(" + str(interact[1]) + ")")
            time.sleep(interact[1])
            ix += 1
            continue
        if logging:
            print("\nCOMMAND:  " + cmd)
        child.sendline(cmd)
        resp = child.expect([ "\(gdb\)", pexpect.TIMEOUT, pexpect.EOF ],timeout=(240 if cmd == 'load' else 30))
        if resp == 1:
            if (interact[1] == pexpect.TIMEOUT):
                if logging: print("RESPONSE: TIMEOUT")
                continue
            print("Failed " + testname + " in line",ix-1, " with TIMEOUT")
            print("Expected: '" + interact[1] + "' in response to: '" + cmd + "'")
            print("FAILED: " + script[0])
            return False
        if resp == 2:
            if ix == len(script) - 1:
                print("\nSUCCEEDED: " + script[0])
                return True
            else:
                print("\nFAILED: " + testname + " in line",ix-1, " with EOF from child")
                return False
        if (logging):
            print("RESPONSE: " + child.before.decode())
        if child.before.decode().find(interact[1]) == -1:
            print("Failed " + testname + " in line",ix-1, " with unexpected response:")
            print("      '" + child.before.decode() + "'")
            print("Expected: '" + interact[1] + "' in response to: '" + cmd + "'")
            print("FAILED: " + script[0])
            return False
        ix += 1
    print("\nSUCCEEDED: " + script[0])
    return True

# should be called in the 'tests' directory
def run_all_tests(port):
    result = (0,0,0)

    for test in tinylist + megalist + exoticlist:
        result = tuple(map(operator.add,result,test_mcu(port,test)))

    print();
    print("Tests run:           ", result[2])
    print("Compilations failed: ", result[0])
    print("Scripts failed:      ", result[1])
    

#print(run_script(tictactoe_script, "/dev/cu.usbmodem1442101", "rc", "ck1"))

print(test_mcu("/dev/cu.usbmodem1442101", attiny48))

#run_all_tests("/dev/cu.usbmodem1442101")
