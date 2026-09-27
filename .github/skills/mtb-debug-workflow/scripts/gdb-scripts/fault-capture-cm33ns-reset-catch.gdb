# CM33 NS reset-and-catch script.
# For use when TrustZone clears fault registers before they can be read.
# Resets the board, enables Vector Catch on HardFault (VC_HARDERR in DEMCR),
# resumes execution, and captures fault state at the moment the NS
# HardFault_Handler is entered — BEFORE the secure handler clears registers.
#
# Prerequisites:
#   - Target must support reset_halt on CM33 NS core
#   - OpenOCD must be connected with full probe access
#
# Note: The 'reset_halt cat1d.cm33_ns' command is CAT1D-specific (PSE84).
# For other TrustZone targets, replace with the appropriate OpenOCD target name.

set pagination off
set confirm off
set remotetimeout 500
set mem inaccessible-by-default off

echo \n=== Reset-and-Catch via Vector Catch ===\n

echo \n=== Resetting to NS application ===\n
monitor reset_halt cat1d.cm33_ns

echo \n=== Enabling Vector Catch on HardFault (VC_HARDERR) ===\n
monitor mww 0xE000EDFC [expr {[mrw 0xE000EDFC] | 0x400}]

echo \n=== Resuming — processor will halt on HardFault entry ===\n
continue

echo \n=== HardFault caught via Vector Catch! Reading state ===\n

echo \n=== Registers ===\n
info registers r0 r1 r2 r3 r4 r5 r6 r7 r8 r9 r10 r11 r12 sp lr pc xpsr

echo \n=== Fault Status Registers ===\n
echo CFSR (MemManage + BusFault + UsageFault):\n
x/1xw 0xE000ED28
echo HFSR (HardFault Status):\n
x/1xw 0xE000ED2C
echo MMFAR (MemManage Fault Address):\n
x/1xw 0xE000ED34
echo BFAR (BusFault Address):\n
x/1xw 0xE000ED38
echo CFSR_NS:\n
x/1xw 0xE002ED28
echo HFSR_NS:\n
x/1xw 0xE002ED2C

echo \n=== NS Exception Frame (via OpenOCD DAP) ===\n
monitor reg psp_ns

echo \n=== Backtrace ===\n
backtrace 20

echo \n=== Done ===\n
quit
