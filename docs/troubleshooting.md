# Troubleshooting

## Problems while preparing the setup

### Problem: It is impossible to upload the dw-link firmware to the UNO board

Maybe, the dw-link probe shield or the auto-reset disabling capacitor is still plugged into the UNO board? Remove, and try gain. 

## Startup problems

Problems during startup can often be diagnosed when one selects the `gdb-server`  console in the Arduino IDE 2.

### Problem: In the gdb-server console, the session is terminated right after the message that the gdbserver is waiting for a connection

This is an indication that the avr-gdb program could not be started. One could try to solve the problem by getting hold of an avr-gdb version tailored for one's own operating system and replacing the version in the tools folder. The path to this folder is shown in the green line in the `gdb-server` console. 

### Problem: In the gdb-server console, right after starting the dw-gdbserver (green line), there are a lot of error messages shown

This could indicate that the dw-gdbserver binary is incompatible with one's operating system. One may try to solve the problem by building one's own binary. If you have Python Version >= 3.10 and have pipx installed, you can download, generate, and install a version of dw-gdbserver that should work on your OS. Execute `pipx install dwgdbserver`, and copy the generated binary (try `pipx list` to locate it on your computer) into the tools folder as shown in the green line in the `gdb-server` console.

### Problem: It is impossible to connect to the hardware debugger. In the gdb-server console the message "No hardware debugger discovered" is shown 

One common problem is that the IDE uses the serial line to the debugger for the `Serial Monitor`. Simply close this console. If this does not help, try to choose a different serial port in the `Tools` menu.

Sometimes, one can no longer connect to the hardware debugger.  Try to disconnect and reconnect the USB cable. Next, you may also want to disconnect and reconnect the target. 

If this happens when the hardware debugger powers the target, this is a sign that the capacitive load of the target may be too high. So, one cure is here to power the target externally and do the power-cycling manually.  

### Problem: When connecting to the target using the *target remote* command, it takes a long time and then you get the message *Remote replied unexpectedly to 'vMustReplyEmpty': timeout*

This will probably not happen when dw-link is used in the Arduino IDE.

The serial connection to the hardware debugger could not be established. The most likely reason for that is that there is a mismatch of the bit rates. The Arduino uses by default 115200 baud, but you can recompile dw-link with a changed value of `HOSTBPS`, e.g., using 230400. If GDB is told something differently, either as the argument to the `-b` option when starting avr-gdb or as an argument to the GDB command `set serial baud ...`, you should change that. If you did not specify the bitrate at all, GDB uses its default speed of 9600, which will not work!

My experience is that 230400 bps works only with UNO boards. The Arduino Nano cannot communicate at that speed.

A further (unlikely) reason for a failure in connecting to the host might be that a different communication format was chosen (parity, two stop bits, ...). 

### Problem: In response to the `monitor debugwire enable` command you get the error message *Cannot connect: ...***

Depending on the concrete error message, the problem fix varies.

- *Cannot connect: Could not communicate by ISP; check wiring*: The debugger cannot establish an ISP connection. Check wiring. Maybe you forgot to power the target board? I did that more than once. If this is not the reason, disconnect everything and put it together again. This helps sometimes. Finally, this error could be caused by having too much capacitive load or a pull-up resistor that is too strong on the RESET line.  
- *Cannot connect: Could not activate debugWIRE*: An ISP connection was established, but it was not possible to activate debugWIRE. Most probably the MCU is now in a limbo state and can only be resurrected by a HV programmer. The reason is most probably too much capacitive load on the RESET line or a strong pullup resistor on this line.
- *Cannot connect: Unsupported MCU*: This MCU is not supported by dw-link.
- *Cannot connect: Lock bits could not be cleared:* This should not happen at all because it is always possible to clear the lock bits by erasing the entire chip.
- *Cannot connect: PC with stuck-at-one bits*: dw-link tried to connect to an [MCU with stuck-at-one bits in the program counter](requirements.md#mcus-with-debugwire-interface). These MCUs cannot be debugged with GDB. 
- *Cannot connect: Reset line has a capacitive load*: The message says it all.
- *MCU type does not match*: The chip connected to the hardware debugger is different from what you announced when starting the debugger through an IDE or by calling dw-gdbserver. 
- *Cannot connect: Target not powered or RESET shortened to GND*: The RESET line is low. Either you forgot to power the target, or there is another (maybe temporary?) reason for it.
- *Cannot connect for unknown reasons:* This error message should not be shown at all. If it does, please tell me!

## Problems while debugging

<a name="lost"></a>

### Problem: You get the message *Connection to target lost*, the program receives a `SIGHUP` signal when you try to start execution, and/or the system LED is off

The target is not responsive any longer. Possible reasons for such a loss of connectivity could be that the RESET line of the target system does not satisfy the [necessary electrical requirements](requirements.md#requirements-concerning-the-target-system). Other reasons might be that the program disturbed the communication by changing, e.g., the [MCU clock frequency](problems.md#mcu-operations-interfering-with-debugwire). Try to identify the reason, eliminate it, and then restart the debug session.  Most probably, there are still BREAK instructions in flash memory, so the `load` command should be used to reload the program.

### Problem: When stopping the program with Ctrl-C (or with the stop button), you get the message *Cannot remove breakpoints because program is no longer writable.*

The reason is most probably that the communication connection to the target system has been lost ([see above](#lost)).

### Problem: When trying to start execution with the `run` command, GDB stops with an internal error

This happens with older avr-gdb versions. You can instead use `monitor reset` and `continue`. 

### Problem: The debugger does not start execution when you request *single-stepping* or *execution* and you get the warning *Cannot insert breakpoint ... Command aborted*

You use more than the allowed number of breakpoints, i.e., usually 20 (including one for a temporary breakpoint for single-stepping). If you have executed the `monitor breakpoint h` command, this number is reduced to 1. In this case, you can either set a breakpoint or you can single-step, but not both! In any case, you need to reduce the number of breakpoints before you can continue.

### Problem: When single stepping with `next` or `step` , you receive the message *Warning: Cannot insert breakpoint 0* and the program is stopped at a strange location

The problem is similar to the one above: You used too many breakpoints and there is no temporary breakpoint left for GDB. The program is probably stopped somewhere you have not anticipated. You may be able to recover by deleting one or more breakpoints, setting a breakpoint close to where you wanted to step, and then using the `continue` command. If this is not possible, restart and use fewer breakpoints.

### Problem: The debugger does not start execution when you request *single-stepping* or *execution*, you get the message *illegal instruction*, and the program receives a `SIGILL` signal

It could be that you did not issue a load command. In this case, one should do that.

A second reason for such a signal could be that at the position we want to continue from, there is a BREAK instruction that either was explicitly inserted by the programmer or is a leftover from a previous debugging session that was not cleaned up. In the former case, you may want to change the sketch and restart debugging. In the latter case, a simple `load` command will do.

## Strange behavior of the debugger

### Problem: After changing optimization options, the binary is still too large/very small

You switched the optimization option from **-Og -fno-lto** back to normal and you recompiled, but your program still looks very big. The reason for that can be that the Arduino IDE/CLI does not always recompile the core, but reuses the compiled and linked archive. In the Arduino IDE 1, you can force a recompile of the core by exiting the IDE. In IDE 2, this option is no longer available. You need to locate the compiled files and delete them manually.

### Problem: The debugger responses are very sluggish

One reason for that could be that the target is run with a clock less than 1 MHz, e.g. at 128 kHz. Since the debugWIRE communication speed is usually clock/8, the debugWIRE communication speed could be 16kbps. If the CKDIV8 fuse is programmed, it could even be only 2kbps. Unprogram CKDIV8 and if possible [choose a higher clock frequency](problems.md#slow-responses-when-loading-or-single-stepping).

### Problem: While single-stepping, time seems to be frozen, i.e., the timers do not advance and no timer interrupt is raised

This is a feature, not a bug.  It allows you to single-step through the code without being distracted by interrupts that transfer the control to the interrupt service routine. Time passes and interrupts are raised only when you use the `continue` command (or when the `next` command skips over a function call). You can change this behavior by using the command `monitor singlestep i`, which enables the timers and interrupts while single-stepping. In this case, however, it may happen that during single-stepping control is transferred into an interrupt routine.

### Problem: PWM (analogWrite) does not seem to work when the program is stopped

The reason is that all timers are usually stopped when the program is in a stopped state. However, you can change this behavior using the GDB command `monitor timers r`. In this case, the timers are run even when the program is stopped, which means that PWM (aka `analogWrite`) is also still active.

### Problem: When single stepping with `next` or `step` , the program ends up at the start of flash memory, e.g., 0x0030

This should only happen when you have used the command `monitor singlestep interruptible` before, which enables interrupts while single-stepping. In this case, an interrupt might have been raised which has transferred control to the interrupt vector table at the beginning of flash memory. If you want to continue debugging, set a breakpoint at the line you planned to stop with the single-step command and use the `continue` command. If you want to avoid this behavior in the future, issue the debugger command `monitor singlestep safe`. 

### Problem: After requesting to stop at a function, the debugger displays a completely different file, where the execution will stop

This is a GDB problem. It can happen when a function call is inlined at the beginning of the function one intends to stop at. While the place where execution will stop looks crazy (e.g., HardwareSerial.h at line 121), the execution stops indeed at the beginning of the specified function (in this case at the beginning of setup).

### Problem: The debugger does not stop at the line a breakpoint was set

Not all source lines generate machine code so that it is sometimes impossible to stop at a given line. The debugger will then try to stop at the next possible line. This effect can get worse with different compiler optimization levels. For debugging, **-Og3** is the recommended optimization option, which applies optimizations in a debug-friendly way. This is also the default for PlatformIO. In the Arduino IDE, you have to activate the `Optimize for Debugging` switch in the  `Sketch` menu.

### Problem: The debugger does things that appear to be strange

The debugger starts execution, but it never stops at a breakpoint it should stop, single-stepping does not lead to the expected results, etc. I have seen three possible reasons for that (apart from a programming error that you are hunting).

Often, I had forgotten to load the binary code into flash. Remember to use the `load` command ***every time*** after you have started a debugging session. Otherwise it may be the case that the MCU flash memory contains old code! Note that after the `load` command the program counter is set to zero. However, the MCU and its registers have not been reset. You should probably force a hardware reset by using the command `monitor reset`. Alternatively, when you initiated your session with `target extended-remote ...`, you can use the `run` command that resets the MCU and starts at address zero. In the Arduino IDE 2, all that cannot happen.

Second, you may have specified a board/MCU different from your actual target. This happens quite easily with PlatformIO when you work with different targets. In this case, some things appear to work, but others do not work at all. Again, in the Arduino IDE 2, this cannot happen.

Another possible reason for strange behavior is the chosen compiler optimization level. If you have not chosen **-Og3**, then single-stepping may not work as expected and/or you may not be able to assign values to local variables. If objects are not printed the right way, then you may consider disabling LTO (by using the compiler option **-fno-lto**). 

So, before blaming the debugger, check for the three possible causes.



## Problems with with GUI/IDE

### Problem: When starting the debug session in PlatformIO, you get the message *pioinit:XX: Error in sourced command file

Something in the `platformio.ini` file is not quite right. Sometimes an additional line of information is given that identifies the problem. If you see also see the message `"monitor" command not supported by this target` then the dw-link adapter could not be found.

One other common problem is that the debug environment is not the first environment or the default environment. In this case, the wrong environment is used to configure the debug session and probably some environment variables are not set at all or set to the wrong values. So, you need to edit the `platformio.ini` file accordingly.



## Problems after debugging

### Problem: After debugging, the chip is unresponsive, i.e., does not respond anymore to ISP programming or bootloader upload**

There are many possible causes:

* The DWEN fuse is still programmed, i.e., the MCU is still in debugWIRE mode. In this case, it may help to enter and leave the debugger again, provided that there are not any [problems with the RESET line](requirements.md#worst-case-scenario). It may also be helpful to issue the command `monitor debugwire disable`. 
* Another fuse has been programmed by accident. In particular, there are the `monitor` commands that change the clock source. If an external clock or an XTAL has been chosen, then you can recover the chip only by providing such an external clock or XTAL and then use either ISP programming or connect again to dw-link. 
* Apparently it happens that the MCU is stuck halfway when transitioning to debugWIRE state. 

If nothing helps, then [high-voltage programming](requirements.md#worst-case-scenario) might still be a last resort.



## Internal and fatal debugger errors

<a name="fatalerror"></a>

### Problem: The system LED blinks furiously and/or the program receives an `ABORT` signal when trying to start execution

In this case some serious internal error had happened. You have to stop the current debug session and restart. 

The reason for such an error could be that the connection to the target could not be established or that there was an internal debugger error. It may be that the corresponding error message has already been displayed. You can find out what kind of error happened by typing the following command:

```
monitor info
```

{!internal_errors.md!}
