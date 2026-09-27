# CM33 fault capture for targets with TrustZone (e.g., PSE84).
# TrustZone causes NS HardFaults to escalate to the secure handler, which
# clears fault registers before we can read them. This script reads BOTH
# secure and non-secure SCB registers, plus the NS exception frame from PSP_NS.

set pagination off
set confirm off
set remotetimeout 500
set mem inaccessible-by-default off

echo \n=== Halting core ===\n
monitor halt

echo \n=== Registers ===\n
info registers r0 r1 r2 r3 r4 r5 r6 r7 r8 r9 r10 r11 r12 sp lr pc xpsr
echo \nTrustZone stack pointers:\n
info registers msp_s psp_s msp_ns psp_ns

echo \n=== Current Frame ===\n
frame

echo \n=== Backtrace ===\n
backtrace 20

echo \n=== Fault Status Registers (Secure SCB 0xE000EDxx) ===\n
echo CFSR_S (MemManage + BusFault + UsageFault):\n
x/1xw 0xE000ED28
echo HFSR_S (HardFault Status):\n
x/1xw 0xE000ED2C
echo SFSR (SecureFault Status):\n
x/1xw 0xE000EDE4
echo SFAR (SecureFault Address):\n
x/1xw 0xE000EDE8

echo \n=== Fault Status Registers (Non-Secure SCB 0xE002EDxx) ===\n
echo CFSR_NS:\n
x/1xw 0xE002ED28
echo HFSR_NS:\n
x/1xw 0xE002ED2C
echo MMFAR_NS:\n
x/1xw 0xE002ED34
echo BFAR_NS:\n
x/1xw 0xE002ED38

echo \n=== NS Exception Frame (stacked at PSP_NS) ===\n
echo Reading 8 words from PSP_NS (R0,R1,R2,R3,R12,LR,PC,xPSR):\n
x/8xw $psp_ns

echo \n=== Done ===\n
quit
