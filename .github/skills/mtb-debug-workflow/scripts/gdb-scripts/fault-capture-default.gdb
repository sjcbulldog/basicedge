# Default Cortex-M fault capture script.
# Works for any single-core Cortex-M target without TrustZone.
# Reads: general registers, backtrace, and standard ARM fault status registers.

set pagination off
set confirm off

monitor halt

echo \n=== Registers ===\n
info registers

echo \n=== Current Frame ===\n
frame

echo \n=== Backtrace ===\n
backtrace 20

echo \n=== Fault Status Registers (ARM Cortex-M) ===\n
echo CFSR (MemManage + BusFault + UsageFault):\n
x/1xw 0xE000ED28
echo HFSR (HardFault Status):\n
x/1xw 0xE000ED2C
echo MMFAR (MemManage Fault Address):\n
x/1xw 0xE000ED34
echo BFAR (BusFault Address):\n
x/1xw 0xE000ED38

echo \n=== Done ===\n
quit
