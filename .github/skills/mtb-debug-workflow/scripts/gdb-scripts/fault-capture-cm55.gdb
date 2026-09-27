# CM55 fault capture script.
# CM55 does NOT have TrustZone so fault registers are directly readable.
# OpenOCD assigns CM55 to a separate GDB port (default 3334).

set pagination off
set confirm off
set remotetimeout 500
set mem inaccessible-by-default off

echo \n=== Halting CM55 core ===\n
monitor halt

echo \n=== Registers ===\n
info registers r0 r1 r2 r3 r4 r5 r6 r7 r8 r9 r10 r11 r12 sp lr pc xpsr

echo \n=== Current Frame ===\n
frame

echo \n=== Backtrace ===\n
backtrace 20

echo \n=== Fault Status Registers (CM55 SCB 0xE000EDxx) ===\n
echo CFSR (MemManage + BusFault + UsageFault):\n
x/1xw 0xE000ED28
echo HFSR (HardFault Status):\n
x/1xw 0xE000ED2C
echo MMFAR (MemManage Fault Address):\n
x/1xw 0xE000ED34
echo BFAR (BusFault Address):\n
x/1xw 0xE000ED38

echo \n=== CM55 Exception Frame (stacked at PSP) ===\n
echo Reading 8 words from PSP (R0,R1,R2,R3,R12,LR,PC,xPSR):\n
x/8xw $psp

echo \n=== Done ===\n
quit
