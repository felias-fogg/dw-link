# Changelog for dw-link

## Version 0.1 (27-May-21)

  - initial version with only a minimal amount of coverage
  - can connect via serial interface to host
  - 'monitor init' establishes the connection to the target
  - 'monitor stop' switches the MCU back to the normal state
  - more accurate baud determination than in other programs based on TIMER1!
  - already all high-level functions from avr_debug (?,H,T,g,G,m,M (except flash),
    D, k, c, s (not functional yet), z, Z, v, q) are implemented
  - all relevant low level functions from DebugWireDebuggerProgrammer are ported
  - erase flash page implemented

## Version 0.2 (28-May-21)

  - writing to flash works
  - loading a file works (using the M command and X commands)

## Version 0.3 (28-May-21)

  - fixed problem with not being able to read the PC after a break
  - use LED_BUILTIN to signal system status
  - fixed breakpoint address problem (converting from word to byte addresses)
  - fixed inconsistent PC addresses (byte vs. word)
  - hw breakpoint integrated

## Version 0.4 (29-MAY-21)

  - use of hw breakpoint as one of the ordinary breakpoints
  - new used field in bp struct
  - works now in PlatformIO (if one uses a .gdbinit file!)
  - fixed bp address bug

## Version 0.5 (31-May-21)

  - less register saving and restoring makes single-stepping faster!
  - fixed problem with clobbered PC after offline execution of insturuction by
    incrementing internal PC twice
  - also disallowed branching / jumping / calling / returning instructions in this context
  - dynamic assignment of HW breakpoint:
    if the same address is used twice in a row for a HW breakpoint and the second time
    there is another breakpoint that has not been written to flash memory yet,
    we reassign the HW BP to the new breakpoint. This way, the HW breakpoint
    is more effectively used for single-stepping dynamic breakpoints, e.g. overstepping
    a function.
  - changed the handling of too many breakpoints to using an error message
    when a breakpoint beyond the limit is going to be inserted. This allows us
    to continue when we have been stopped using a temporary breakpoint inserted by GDB
  - new monitor command "flashcount" that reports on how often a flash page write operation
    had occured.
  - new monitor command "ram" that reports on the minimal number of free bytes in RAM,
    this command is usually disabled, though (see compile time constant FREERAM).
  - added '*' as an charachter to be escaped in bin2mem; the documentation says that
    it only needs to be escaped when comming from the stub, but avr-gdb seems to escape it
    anyway.

## Version 0.6 (03-Jun-21)

  - added support for ATtiny828 and ATtiny43
  - issue an error when a byte is escaped although it should not have been instead
    of silently ignoring it
  - added gdbRemoveAllBreakpoints in order to avoid leaving active bps before reset etc.
  - changed the number of entries of bp from MAXBREAKS*2+1 to one less, because we now
    refuse to acknowledge every extra BP above the allowed number
  - detach function now really detaches, i.e., continues execution on the target and leaves it alone.

## Version 0.7 (03-Jun-21)

  - implementation of monitor ckdivX (promised already in the manual)

## Version 0.8 (08-Jun-21)

  - problem with unsuccessful erase operations fixed: was actually an I/O unsynchronization bug.
    Forgot to read the BREAK/'U' after the single-step operation when skipping a SW breakpoint
    in gdbStepOverBP.
  - added message explaining connection error

## Version 0.9 (30-Jun-21)

  - make distinction between wiring error and unsupported MCU type when connecting
  - implement automatic power-cycling
  - waiting longer after trying to power-cycle before requesting it in order to avoid
    the message to "power-cycle" 
  - not confusing the user by saying that dW is "still enabled". You always get that it is
    "now enabled"

## Version 0.9.1 (02-Jul-21)

  - added "defensive code" into the debugger that detects fatal errors
    such as page erase failures and then reports them to the user
    instead of silently ignoring it
  - added code to detect loss of connection to the target
  - added code to detect when lock bits are set
  - excluded all MCUs with bootloader memory

## Version 0.9.2 (04-Jul-21)

  - show speed of connection when calling "monitor init"
  - initialize all vars when gdb reconnects (i.e., when gdb sends a qSupported packet)
  - now we use TIMER1 for measuring the delay instead of counting execution cycles in
    OnePinSerial: much more accurate!
  - since this is still not enough, the millis and the status blinking interrupts have been
    disabled (we now use \_delay\_ms and \_delay\_us); actually, the blink IRQ is now only used
    for error blinking and for power-cycle
    flashing -- all other interrupts are disabled, except, of course, for the
    PCI interrupt for the debugWIRE line and the interrupt for the serial line;
  - in order to get the center of the first bit, we now wait for 1,5 bits after the first edge
    (minus the time used in the program for serving the IRQ etc - which is empirically determined);
  - changed setTimeoutDelay(DWIRE_RATE) to setTimeoutDelay(ctx.baud) in order to minimize the delay
    in the getResponse loop; it used to be 4ms, which was ridiculous and showed badly with the
    new OnePinSerial class;
  - in addition, the targetReset routine needed a seperate delay loop because the delay to the
    response is not depended on the baud rate, but it is constant 75ms;
  - finally, we can now also write flash memory in the ATmegas, so the exclusion of the
    bootloader MCUs has been revoked

## Version 0.9.3 (04-Jul-21)

  - Now it seems to work with targets running at 16 MHz. With SCOPE\_TIMING enabled in OnePinSerial,
    it worked beautifully; without, there were a number of glitches when reading.
    So I put in some NOPs when SCOPE_TIMING is disabled ... and it
    works. The only problem is, I do not know why, and this makes me nervous. Maybe time
    to rework OnePinSerial from the ground up.

## Version 0.9.4 (02-Nov-21)

  - Instead of OnePinSerial, we use now dwSerial, which uses in turn the (new) base class SingleWireSerial.
    One order of magnitude more accurate and robust! Works up to 250 kbps without a hickup
    (if the millis interrupt is disabled).
  - Timeout in getResponse is now done using the number of cycle iterations, which is roughly 2-3us.
    Meaning we should wait roughly 40,000 iterations if we want to wait up to 100 ms (works so far).

## Version 0.9.5 (04-Nov-21)

  - changed the TXOnlySerial bps to 57600
  - decreased timeout for "power-cycle" to 3 seconds in order to supress "timeout" message by gdb 
  - all basic debugWIRE functions have now a DW prefix
  - created "monitor testtg" and "monitor testgdb" for unit testing the target functions
    and the gdb functions, but have to come up with unit tests for them
  - inserted "unit test" code for low level DebugWIRE functions, which can be called using
    "monitor testdw"
  - registers are only saved (right after start or after a break) and restored (before
    execution or single-stepping)
    no more partial saves etc. Is much faster since we use the bulk register transfer.
  - no more noJumpInstr() since jumps can actually be executed offline
  - calls to dw.sendCmd replaced by calls to new function sendCommand, which now waits for
    Serial output to be finished in order to avoid any output interrupt, which can
    take up to 8.75 us and by that confuse SingleWireSerial. It can
    withstand 6.6 us, but 8.75 us is too much at 125 kbps!

## Version 0.9.6 (04-Nov-21)

  - reshuffeld code in gdbConnect without external effects. Added a case for "unknown reason"
    for a connection error.
  - added more DW test cases and streamlined DW unit tests
  - added target unit tests 
  - added a 'monitor testall' command that executes all unit tests

## Version 0.9.7 (06-Nov-21)

  - new HWBP policy: most recently inserted BP will become a HWBP,
    i.e., it will steal the property from BPs earlier introduced
  - cleaned up gdbUpdateBreakpoints (no hwbp assignment anymore)
  - streamlined BP removal function, no early freeing of BPs
  - in InsertBreakpoint we may steal the HWBP from another breakpoint
  - new function gdbBreakpointPresent, which returns true if a BREAK instruction
    has been inserted by us and then gives back opcode and 2nd word, if necessary
  - redesign of gbdStepOverBP and renaming it to gdbBreakDetour:
    for 2-byte instructions, we execute offline (using debugWIRE)
    for 4-byte instructions, we simulate the execution
  - unit tests for breakpoint management, single-step, and execution
  - removed gdbRemoveAllBreakpoints and replaced it by a check
    for remaining breakpoints (there shouldn't be any)
  - hide inactive breakpoints in memory to the eyes of gdb when
    gdb asks for flash memory contents
  - introduced bpused (number of used BPs) as a  second BP counter  (in order
    to speed up processing when nothing is there)

## Version 0.9.8  (08-Nov-21)

  - some attiny chips exhibit a somewhat funny behavior
    when they are connected to debugWIRE: you need to send a LONG break to which the
    chip responds with a 'U' with roughly 4000 bps; when you then send
    a reset command (0x07), the chip answers with a break and 'U' using the
    right speed (clk/128); so, I had to redesign the connection
    routine, which hopfully also works for the other chips: turns out, it does;
    moreover, the funny chips seem to work normal after going through
    the motion once, i.e., disabling the DWEN bit again. After that
    they respond after the first break with the right speed
  - while the DWDR register of the ATtiny 2313 is 0x1F, the one for the 4313 
    is 0x27 instead
  - fixed the problem that page writes on the ATmega did not always work (the
    program did not wait long enough, although the code was already there)
  - inserted code to deal with the 4-page erase operations of 1634, 841, 441; new field in the
    mcu record	and code; the pagesize will be treated as 4 times as large (mcu.targepgsz),
    and while programming the page, 4 write operations take place
  - put the SingleWireSerial library into the src directory together
    with dwSerial
  - before any monitor command, the breakpoints will be deleted from
    memory
  - all global vars are initialized when monitor init is executed
  - the name of the connected MCU is shown when connecting
  - enabled blinking interrupt now again when MCU is running;
    interrupt latency is down to 3.4 us, which is tolerable
    for SingleWireSerial
  - noticed that when we use the BUILTIN_LED, then this will blink the
    BUILTIN_LED of the target, since the SCK lines of debugger and
    target are connected; so switched to PB2 (Arudino pin 10) for the system LED
  - when running the ATmega328 target on 16 MHz, sometimes spurious
    0x00/0xFF show up; so I introduced DWflushInput that reads them
    away; what is strange is that this does not happen at lower
    frequencies and not with other chips at 16 MHz; it is also quite determinstic!
  - Unit tests successful on:
    Attiny13 (9.6 MHz, 1.2 MHz)
    ATtiny2313 (8 MHz, 1 MHz)
    ATtiny4313 (8 MHz, 1 MHz)
    ATtiny24 (8 MHz, 1 MHz)
    ATtiny84 (8 MHz, 1 MHz)
    ATtiny85 (8 MHz, 1 MHz)
    ATtiny861 (8 MHz, 1 MHz)
    ATTiny167 (16 MHz, 2 MHz)
    ATtiny1634 (8 MHz, 1 MHz)
    ATmega328P (16 MHz, 8 MHz, 2 MHz, 1 MHz)

Version 0.9.9 (14-Nov-21)
  - new commands: "monitor hwbp" (reducing the number of allowed
    breakpoints to 1) and "monitor swbp" (allowing 32+1 breakpoints) and
    for test purposes: monitor 4bp (3+1 BPs)
  - checking for user inserted BREAK after/before step/continue and return
    with SIGILL
  - checking for ^C in single-step when not progressing (necessary when
    there is a RJMP .-2 instruction and gdb tries to find the
    instruction at the beginning of the next source line).
  - unittest.ino is now the file with all the unit tests
  - more strange observations:
    + all Tinys can execute the two-word JMP and CALL instructions
      despite the claim by Atmel that they cannot
    + one ATmega328, which correctly identifies itself with the
      signature 0x9514 when queried using ISP programming replies with
      0x0950F (the signature of an ATmega328P) when queried using the
      debugWIRE command
    + the simulation of 2-word instructions does not appear to be
      necessary; if the PC is set to the right location and the first word
      is loaded into the instruction register, starting an offline
      execution apparently fetches the second word from the right place
      in flash memory and the execution works flawlessly
  - new commands for testing: "monitor rcosc" and "monitor "xtosc" for
    selecting the internal RC oscillator or an external crystal oscillator. 
  - allow for early interrupts in the blinking interrupt routine
  - added function to recognize illegal opcodes, which is used before
    starting execution and when single-stepping; the function has
    been validated against the avr-objdump disassembler (and JMP and
    CALL on small ATinys are considered illegal)
  - reprogrammed the ISP routines in order to be able to connect to
    ATmegas with less than 32 KiB flash; I had to adopt the same way
    of writing and reading the fuse bytes as avrdude in order to make
    it work - no idea why; and it were only those ATmegas!
  - changed targetStop so that it can be used even when there is a
    fatal error in order to be able to always set a chip back into normal state
  - removed ctx.run and ctx.targetcon and added systate to ctx
  - removed gdbExecProblem; now we set signals and then give a message
    in gdbSendState. 
  - do not record fatal error when disconnected (is checked in
    reportFatalError)
  - in initSession we cleanup the BP table, so a "monitor init" can
    start with an empty BP table
  - gdbContinue does now either return with a signal notifying an
    execution error or with a zero, meaning that execution has been
    started and will be stopped by hitting a breakpoint or by an ^C interrupt
  - had to move the exec on illegal intruction as the last test in the
    unit tests, otherwise the inactive BPs were cleaned up and the hide breaks
    routine would not be effective.

## Version 1.0.0 (16-Nov-21)

  - renamed monitor commands "init" and "stop" to "dwco[nnect]" and "dwof[f]"
  - renamed monitor commands "ckdiv8" and "ckdiv1" to "ck8[prescaler]"
    and "ck1[prescaler]"
  - allowed abbreviations for all other monitor commands as well
  - measure rise time on RESET line in order to reject bad quality
    connections
  - reject monitor command "reset" and "test..." when target is not
    connected
  - included test sketches

## Version 1.0.1 (16-Nov-21)

  - reverted back a change in SingleWireSerial that declared a method
    as an ISR because it let to portability problems

## Version 1.0.2 (24-Nov-21)

  - deleted 10 ms break from doBreak, since the break is already sent
    by calibrate
  - changed calibrate in dwSerial, so that 12 ms break is sent, which should be
    enough even for a 128 kHz system clock, i.e., 1 kbps communication 
    speed (may need to change ISP frequence to accomodate this!)   
  - introduced different pin assignments for 7 different boards: Uno,
    Leonardo, Mega, Nano, Pro Min, Pro Micro, and Micro. The latter
    four all fit into the socket on the same adapter board that
    has level shifting and switching electronic on board,
    for the former three, I will design a shield
  - put type casts into mcu\_attr array in order to get rid of warning
  - adapted the unit tests functions so that they only give an error
    reply in case of a not connected target system when they are called
    in isolation; in addition, they now always return a zero when
    called with an unconnected target
  - since the program will now be designed to drive the RESET line
    through a level shifter, the quality measurement of the line does not
    make sense anymore; will be (conditionally) removed from the code
    and eliminated in the manual; perhaps one can implement this
    functionality by feeding the reset pin back to an ordinary input
    pin and we measure by 'cycle-counting'?
  - reconfiguration based on switch settings occur in the main
    loop
  - switching on programming mode and power-cycling is now done in
    specialized function that take the configuration into accout
  - changed "too many BPs" error: now the gdb function sends an error
    return when one too many BP is inserted; in this case the continue
    or step function is aborted with the warning: "Cannot insert breakpoint 1.
    Cannot access memory at address 0x1b4. Command aborted" - not
    completely accurate, but sort of helpful
  - the above change implies that there may be active BPS (i.e., also
    inserted BREAKS) when the debugger is left or when a load is
    issued; for this reason a removal of all BREAKs is necessary before
    one leaves -> cleanup parameter of gdbUpdateBreakpoints.
  - toomanybps and error message about BPs removed, SIGSYS removed, SIGSYS is
    now SIGABRT.
  - monitor dwconnect integrated into the routine that is started
    after gdb connected to the hardware debugger, i.e., when the
    qsupported command is sent; this means you usually do not have to
    type this command explicitly

## Version 1.0.3 (01-Dec-21)

   - corrected entry bootaddr for ATtiny828 from 0x0F7F to 0x0F80
   - added "hwbreak-" in response string to qSupported
   - added the feature that examining address 0xFFFFFFFF gives you the
     last fatal error code
   - removed fatal error for examing addresses out of band; we simply
     return an error reply to gdb
   - integrated dwconnect error messages into fatal error message list
   - if system state = ERROR_STATE, we need a reset/new connection to
     reset that state
   - error state is cleared when disconnecting with detach or kill
   - further successful unit tests with meanwhile all MCUs, except ATtiny441,
     ATtiny87, ATtiny48 and all ATmegaXU2s
     
## Version 1.0.4 (02-Dec-21)

   - new command "monitor er[aseflash]"
   - brought back command "monitor xt[alosc]"
   - rewrote targetStop by making use of targetSetFuses
   - renamed dw-probe to dw-link; the board retains the name, though
   - also renamed the top-level directory from debugWIRE-probe to dw-link

## Version 1.0.5 (04-Dec-21)

   - designed, tested, and documented the prototype board with
     level-shifters
   - did an exhaustive search in the MPLAB-X database for MCUs
     supporting debugWIRE and came up with a number of obscure chips
     I had never heard about; I documented it in  the manual and
     ordered some of the chips I could get hold of (some will
     only be available by end of next year)
   - restructured the mcu_info data & saved more than 600 bytes
   - check now whether change to xtal is possible and give error
     message if not
   - extended mcu_info so that now all DW chips except the obsolete
     ones are covered; only 128 bytes added for 18 additional chips; 6
     will not be covered because they are obsolete
   - extended mcu_info to include a field that describes the
     architecture, i.e, AVRe or AVRe+; AVRe+ means that multiplication
     can be done in hardware; extended also the handling in targetIllegalOpcode
   - added three unit tests: 1 DW test to check whether
     multiplication is performed/not performed; 2 TG tests for checking
     recognition of mul and jmp instructions
   - moved changelog to docs directory
     
## Version 1.0.6 (07-Dec-21)

   - made the blinking ISR an ISR\_NOBLOCK in order to minimize
     interrupt latency
   - integrated communication speed control so that regardless of
     MCU clock frequency one has a reasonable communication speed;
     this resulted in a number of changes
   - one strange observation in this contect is that communication
     speed is reduced to clk/256 after a program has been stopped by
     CTRL-C, which results in a break condition on the RESET line;
     this does NOT happen when doing the same with the Atmel-ICE,
     and I have no idea why
   - implemented communication speed limit control: "monitor speed \<option>",
     where \<option> can be 'l' (low speed = 62500), 'n' (normal speed
     = 125000), and 'h' (high speed = 250000); the command sets the
     upper limit, but then for 1 MHz clock frequency, one reaches only 
     62500 or 125000 bps in any case; the monitor speed command without an
     option prints the current connection speed

## Version 1.0.7 (10-Dec-21)

   - fixed the bug that a breakpoint signal by the target was not
     detected
   - adaptive communication speed to host: 230400, 115200, 57600,
     38400, 19200, and 9600 (the GDB default) are possible; so
     one only has to set the speed in avr-gdb; dw-link will adapt
     to it; can be disabled by setting ADAPTSPEED = 0
   - inserted configureSupply into setup so that the target could
     be powered up early on; otherwise DW is not active when the
     first doBreak is issued; DW apparently needs to be powered up
     at least 70 ms before you can use it!
   - support extended-remote, i.e., now one can also use the 'run'
     command in order to restart
   - New monitor command: serial - prints speed of communcation line
     to host

## Version 1.0.8 (11-Dec-21)

   - made VARSPEED=1 the default, i.e., dw-link always attempts
     to use the maximal communication speed possible
   - re-introduced simulation of 2-word instructions at breakpoints
     from version 0.9.7, because even in the most recent versions of
     ATMEL-ICE/MPLAB, the debugger reflashes 2-word instruction
     breakpoints; because it is impossible to test extensively
     whether the offline execution works always,
     the simulation solution appears to be safer and does not
     appear to be slower (communication-wise); the simulation is
     activated by SIM2WORD=1 (which is the default value now)
   - changed output of monitor commands that give a direct output
     so that they return a string instead of debug message + OK
   - inserted 100 ms wait in doBreak to allow target to start up
     when controlled by a 32U4 powered debugger; there are a few
     more hickups with it, so no Leonardo etc. yet as a debugger
     (changed documentation as well)
   - only high (250k) and low (125k) speed limit for DW communication

## Version 1.0.9 (20-Dec-21)
   - set sysstate to 'unconnect' when 'kill' command is executed;
     necessary because 'quit' just issues a 'kill'; systate is set to
     connected when 'run' command is used afterwards (which is only
     accepted when target has been connected)
   - prepared test scripts for unit tests, blink, flashtest,
     and fibonacci
   - included TXOnlySerial into the the libraries in 'src'
   - tried out importing dw-link into PlatformIO, seems to work after
     a few changes such as importing TXOnlySerial
   - new monitor command: "monitor version"
   - new default DW speed is now 125k since I had some spurious errors
   - almost all test sketches appear to work wit their debugging
     scripts in test.py (which is now part of the distribution)

## Version 1.1.0 (27-Dec-21)
   - use offline execution for single-stepping in order to avoid
     interrupts while single-stepping through the code - works perfectly
   - new commands: `monitor safestep` and `monitor unsafestep`, the
     former to enable offline execution for single-stepping, the latter for disabling it
   - new test: isr.ino - tests the new feature of safe single-stepping

## Version 1.1.1 (29-Dec-21)
   - deal with MCUs that have "unclean" program counters

## Version 1.1.2 (30-Dec-21)
   - removed DWgetWBp, since it is not used anywhere, but in the unit tests
   - since the unused stuck-at-one bits in the program counter of
     ATmega48 and 88 confuse GDB, the connection to MCUs with such PCs
     is rejected by default; if you really want to use the debugger on
     these MCUs, you have to set STUCKAT1PC to the value 1
   - gdbConnect and targetConnect rewritten
   - all ATtinys pass the tests now, execept for the ATtiny48, which has not arrived yet
   - concerning the ATmegaX8, I still wait for newer versions of
	 ATmega48 and ATmega88; perhaps they are better 
	 behaved than the more than 10 year old exemplars
   - inserted "Reconnecting..." message after changing fuses/erasing
     memory and deleted double "Connected now ..." message

## Version 1.1.3 (30-Dec-21)
  - new "lazy" way of loading flash memory:

    + close a page (i.e., write it to flash) when a byte needs to be
      stored into a new page or some other operation should be performed
	+ open a page, i.e., load it from flash for modification, when a new byte should be stored in a new page
    + store a byte into an open page when it belongs there
  - this is much faster then the old way (50% for MCUs with 128 byte pages, 20% for MCUs with 64 byte pages, and 10% for MCUs with 32 byte pages) and it opens the way to deal with MCUs that have 256 byte pages (Atmega64C1/M1), because it needs less memory than the old way
  - the only drawback is that the last page is only written when another command is sent from GDB to dw-link

## Version 1.1.4 (31-Dec-21)
   - added code in gdbMessage to prevent an output buffer overflow -
     that led to failed unit tests because the title of the test was
     too long (sigh!)
   - unified gdbWriteMem and gdbWriteBinMem and added checks for
     address bounds so that you now get an error message when loading
     a file that is supposed to be for an MCU with more memory

## Version 1.1.5 (31-Dec-21)
   - integrated build actions
   - fixed two typos in dw-link (blank in \_\_AVR_MEGA2560\_\_) and
     tiny85blink (quit) -- thanks to the build actions!

## Version 1.1.6 (03-Jan-22)

   - unified pin names ISP (in docu) and PROG (in sketch) to TISP
   - renamed the constants SCK, MISO, and MOSI to TSCK, TMISO, TMOSI, respectively to avoid name clashes with the predefined constants
   - developed sketch `dptest` to test the different functions of the adapter board
   - added code to enable ISP interface when using dw-probe
   - noticed that one of my (el cheapo) Nanos cannot communicate at 230400 bps, but only at 115200 bps
   - unit tests are noew by default disabled so that dw-link compiles
     for a Nano without a problem
