
# Problems and shortcomings

dw-link is still in ***beta*** state. The most obvious errors have been fixed, but there are most probably others. If something does not go according to plan, please try to isolate the reason for the erroneous behavior, i.e., identify a sequence of operations to replicate the error. The most serious errors are *fatal errors*, which stop the debugger from working. With the command `monitor info` you can get information on the last fatal error (check the [error table at the end of the troubleshooting section](troubleshooting.md#internal-and-fatal-debugger-errors)).

One perfect way to document a debugger error is to switch on logging and command tracing in the debugger:

```text
set trace-commands on
set remote debug 1
set logging on
...
set logging off
```

This can either be done during the interactive debug session or in the `.gdbinit` file in the home directory. The latter is preferable if the problem happens before the session is started using `target remote ...`.

Apart from bugs, there are, of course, shortcomings that one cannot avoid. I will present some of them in the next subsections.

## Flash memory wear

Setting and removing *breakpoints* is one of the main functionality of a debugger. Setting a breakpoint is mainly accomplished by changing an instruction in flash memory to the BREAK instruction. This, however, implies that one has to *reprogram flash memory*. Since flash memory wears out, one should try to minimize the number of flash memory reprogramming operations.

GDB does not pass *breakpoint set* and *breakpoint delete* commands from the user to the hardware debugger, but instead, it sends a list of *breakpoint set* commands before execution starts. After execution stops, it sends *breakpoint delete* commands for all breakpoints. In particular, when thinking about conditional breakpoints, it becomes clear that GDB may send a large number of *breakpoint set* and *breakpoint delete* commands for one breakpoint during one debug session. Although it is guaranteed that flash memory can be reprogrammed at least 10,000 times according to the data sheets, this number can easily be reached even in a few debug sessions, provided there are loops that are often executed and where a conditional breakpoint has been inserted. Fortunately, the situation is not as bad as it looks since there are many ways of getting around the need of reprogramming flash memory.

First, dw-link leaves the breakpoints in memory even when GDB requests to remove them. The breakpoints in flash memory are updated only when GDB requests to continue execution. Assuming that the user does not change breakpoints too often, this will significantly reduce flash reprogramming.  

Second, if there are many breakpoints on the same flash page, the page is reprogrammed only once instead of individually for each breakpoint.

Third, when one restarts from a location where a breakpoint has been set, GDB temporarily removes this breakpoint, single-steps to the next instruction, reinserts the breakpoint, and only then continues execution. This would lead to two reprogramming operations. However, dw-link does not update flash memory before single-stepping. Instead, if the instruction is a single-word instruction, it loads the original instruction into the MCU's instruction register and executes it there. 

For two-word instructions (i.e., LDS, STS, JUMP, and CALL), things are a bit more complicated. The Microchip documents state that one should refrain from inserting breakpoints at double-word instructions, implying that this would create problems. Indeed, RikusW noted in his [reverse engineering notes about debugWIRE](https://web.archive.org/web/20240614191418/http://www.ruemohr.org/docs/debugwire.html):
>Seems that its not possible to execute a 32 bit instruction this way.
The Dragon reflash the page to remove the SW BP, SS and then reflash again with the SW BP!!! 

I noticed that this is still the case, i.e., MPLAB-X in connection with ATMEL-ICE still reprograms the page twice for hitting a breakpoint at a two-word instruction. The more sensible solution is to simulate the execution of these instructions, which is at least as fast and saves two reprogramming operations. And this is what dw-link does.

Fourth, each MCU contains one *hardware breakpoint register*, which stops the MCU when the value in the register equals the program counter. Dw-link uses this for the breakpoint introduced most recently. With this heuristic, temporary breakpoints (as the ones GDB generates for single-stepping) will always get priority and more permanent breakpoints set by the user will end up in flash. 

Fifth, when reprogramming of a flash page is requested, dw-link first checks whether the identical contents should be loaded, in which case it does nothing. Further, it checks whether it is possible to achieve the result by just turning some 1's into 0's. Only if these two things are not possible, the flash page is erased and reprogrammed. This helps in particular when reloading a file with the GDB `load` command after only a few things in the program have been changed.  

With all of that in mind, you do not have to worry too much about flash memory wear when debugging. As a general rule, you should not make massive changes to the breakpoints each time the MCU stops executing. Finally, Microchip recommends that chips that have been used for debugging using debugWIRE should not be shipped to customers. Well, I never ship chips to customers anyway.

<a name="paranoid"></a>For the really paranoid,  there is the option that permits only one breakpoint, i.e., the hardware breakpoint: `monitor breakpoint hardware`. In this case, one either can set one breakpoint or one can single-step, but not both. So, if you want to continue after a break by single-stepping, you first have to delete the breakpoint. By the way, with `monitor breakpoint all`, one switches back to normal mode, in which 20 (including one temporary) breakpoints are allowed.

In addition, the debugger command `monitor info` shows the number of flash page reprogramming commands executed since the debugger was started. This also includes the flash reprogramming commands needed when loading code.

## Slow responses when loading or single-stepping

Sometimes, in particular, when using a clock speed below 1 MHz, responses from the MCU can be quite sluggish. This shows, e.g., when loading code or single-stepping. The reason is that a lot of communication over the RESET line is going on in these cases and the communication speed is set to the MCU clock frequency divided by 8, which is roughly 16000 bps in case of a 128 kHz MCU clock. If the CKDIV8 fuse is programmed, i.e., the MCU clock uses a prescaler of 8, then we are down to 16 kHz MCU clock and 2000 bps. The [Atmel AVR JTAGICE mkII manual ](https://onlinedocs.microchip.com/pr/GUID-73C92233-8EC5-497C-92C3-D52ED257761E-en-US-1/index.html) states under [known issues](https://onlinedocs.microchip.com/oxy/GUID-73C92233-8EC5-497C-92C3-D52ED257761E-en-US-2/GUID-A686427B-0B7C-465A-BCFF-F093FD6B7A8F.html):

>Setting the CLKDIV8 fuse can cause connection problems when using debugWIRE. For best results, leave this fuse un-programmed during debugging. 

"Leaving the fuse un-programmed" means that you probably have to change the fuse to be un-programmed using a fuse-programmer, because the fuse is programmed by default.

With 125 kbps for the debugWIRE line, loading is done with 600 bytes/second. It is 4 KiB/second when the identical file is loaded again (in which case only a comparison with the already loaded file is performed). 

## Program execution is very slow when conditional breakpoints are present

If you use *conditional breakpoints*, the program is slowed down significantly.  The reason is that at such a breakpoint, the program has to be stopped, all registers have to be saved, the current values of the variables have to be inspected, and then the program needs to be started again, whereby registers have to be restored first. For all of these operations, debugWIRE communication takes place. This takes roughly 100 ms per stop, even for simple conditions and an MCU running at 8MHz. So, if you have a loop that iterates 1000 times before the condition is met, it may easily take 2 minutes (instead of a fraction of a second) before execution stops.

<a name="74"></a>

## Single-stepping and interrupt handling clash

In many debuggers, it is impossible to do single-stepping when timer interrupts are active since, after a step, the program may end up in the interrupt routine. This is not the case with avr-gdb and dw-link. Instead, time is frozen and interrupts cannot be raised while the debugger single-steps. Only when the `continue` command is used, interrupts are serviced and the timers are advanced. One can change this behavior by using the command `monitor singlestep interruptible`. In this case, it can happen that control is transferred to the interrupt vector table while single-stepping.

## Limited number of breakpoints

The hardware debugger supports only a limited number of breakpoints. Currently, 20 breakpoints (including one temporary breakpoint for single-stepping) are supported by default. You can reduce this to 1 by issuing the command `monitor breakpoint hardware` ([see above](#paranoid)). If you set more breakpoints than the maximum number, it will not be possible to start execution. Instead one will get the warning `Cannot insert breakpoint ... Command aborted`. You have to delete or disable some breakpoints before program execution can continue. However, you should not use that many breakpoints in any case. One to five breakpoints are usually enough. 

## Power saving is not operational 

When you activate *sleep mode*, the power consumed by the MCU is supposed to go down significantly. If debugWIRE is active, then some timer/counters will never be stopped and for this reason the power reduction is not as high as in normal state.

<a name="section77"></a>

## MCU operations interfering with debugWIRE

There are a few situations where MCU operations interfere with the debugWIRE system. The above-mentioned list of [known issues](https://onlinedocs.microchip.com/oxy/GUID-73C92233-8EC5-497C-92C3-D52ED257761E-en-US-2/GUID-A686427B-0B7C-465A-BCFF-F093FD6B7A8F.html) contains the following:

* The PRSPI bit in the power-saving register should not be set
* Do not single step over a SLEEP instruction
* Breakpoints should not be set at the last address of flash memory
* Do not insert breakpoints immediately after an LPM instruction and do not single-step LPM code. 

Setting the `PRSPI` bit can disable the clock for the debugWIRE line and should be avoided for this reason. 

If a SLEEP instruction is requested to be single-stepped, in dw-link, a NOP will be executed instead. This is apparently what happens in Microchip's debuggers as well. 

Setting a breakpoint to the last address of flash memory hardly ever happens when source-level debugging. 

Single-stepping over LPM instructions can corrupt the flash memory display in Atmel Studio. However, we can safely ignore this.

The list of known issues mentions also the following five potential problems:

* Be aware that the On-chip Debug system is disabled when any lock bits are set
* BOD and WDT resets lead to loss of connection 
* The OSCCAL and CLKPR registers should not be changed during a debug session
* The voltage should not be changed during a debug session
* The CKDIV8 fuse should not be in the programmed state when running off a 128 kHz clock source

The first issue is mitigated by dw-link erasing the chip when lock bits are set. This is not an industrial-strength solution, but it makes life easier because all UNO boards have their lock bits set initially. So, instead of explaining that the bits have to be cleared, it is just done automatically. 

Concerning resets, I did not experience fundamental problems. The only issue was that the target would not stop at the hardware breakpoint after a reset, since the reset will clear this hardware breakpoint. So, if you want to be sure to stop after a reset, use the command `monitor breakpoint software`, which forces all breakpoints to be software breakpoints. If you use the watchdog timer to issue a software reset, make sure that right after restarting the MCU, the watchdog timer will be disabled, as mentioned in the [AVR-LibC FAQ](https://avrdudes.github.io/avr-libc/avr-libc-user-manual-2.2.0/FAQ.html#faq_softreset). Otherwise, you run into a WDT-restart loop.

Changing the clock frequency is also not a problem since, at each stop, the debugger re-synchronizes with the target. Further, changing the supply voltage can be done if you have level-shifting hardware in place. It is still not something that is recommended. 

Finally, debugging at very low clock frequencies (32 kHz/8 = 4 kHz) is not impossible, but communication is extremely slow. I have implemented that mainly because of curiosity.

## BREAK instructions in your program

It is possible to put the BREAK instruction, which is used to implement breakpoints, in ones program by using the inline assembly statement `asm("break")`. This makes no sense since, without the debugger, the MCU will treat this instruction as a NOP. Such a BREAK instruction may also be in the program because a previous debugging session was not terminated in a clean way. 

When running under the debugger, the program will be stopped in the same way as if there is a software breakpoint set by the user. However, one cannot continue execution from this point with the `step`, `next`, or `continue` command. You will always get a SIGILL signal. So, one needs to reload the program code, set the PC to a different value, or restart the debugging session.

<a name="section79"></a>

## Some MCUs have stuck-at-one bits in the program counter

Some debugWIRE MCUs appear to have program counters in which some unused bits are stuck at one. ATmega48s and ATmega88s (without the A-suffix), which I have sitting on my bench,  have their PC bits 11 and 12 or only PC bit 12 always stuck at one. In other words, the PC has at least the value 0x1800 or 0x1000, respectively (note that the AVR program counter addresses words, not bytes!). The hardware debugger can deal with it, but GDB gets confused when trying to perform a stack backtrace. It also gets confused when trying to step over a function call or tries to finalize a function call. For these reasons, debugging these MCUs does not make much sense and dw-link rejects these MCUs with an error message when one tries to connect to one of those (see also [this blog entry](https://hinterm-ziel.de/index.php/2021/12/29/surprise-surprise/)). 

The only reasonable way to deal with this problem is to use a different MCU, one with an A, PA, or PB suffix. 

## The start of the debugger takes two seconds

The reason is that when the host establishes a connection to the debugger, the debugger is reset and the bootloader waits two seconds. You can avoid that by disabling the auto-reset feature putting a capacitor of 10 µF or more between RESET and GND. The dw-link probe shield also does that for you.

## Code optimization reorganizes code and makes it impossible to stop at a particular source line or to inspect or change values of local variables

The standard setting of the Arduino IDE and CLI is to optimize for space, which is accomplished using the compiler option **-Os**. In this case, it may be difficult to stop at some source lines, and single-stepping may give strange results. When you choose `Optimize for Debugging` in the Sketch menu, then the compiler optimizes the code in a debugger-friendly way (using the compiler option **-Og**). 

I have encountered situations [when it was impossible to get the right information about C++ objects](https://arduino-craft-corner.de/index.php/2021/12/15/link-time-optimization-and-debugging-of-object-oriented-programs-on-avr-mcus/). This can be avoided by disabling *link-time optimization* (LTO).  Finally, if there are still discrepancies between what you expect and what the debugger delivers, you can try to set **-O0 -fno-lto**, which you only can do when compiling the sketch with arduino-cli. In PlatformIO, you can set the options for generating the debug binary in the `platform.ini` file.
