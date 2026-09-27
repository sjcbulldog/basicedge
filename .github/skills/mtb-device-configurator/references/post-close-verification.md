# Post-Close Verification Procedure

After the user confirms they have completed the changes and closed Device Configurator:

## 1. Routing Verification Triad

**Read ALL THREE files together** to understand effective pin routing — any one file alone can be misleading:

| File | What It Shows | Common Pitfall |
|------|--------------|----------------|
| `cycfg_peripherals.h` | Peripheral aliases and config structs | Shows what's configured, not how it's routed |
| `cycfg_pins.h` | Pin macros and HSIOM assignments | **Fallback macros may show GPIO even when alternate function is routed** |
| `cycfg_routing.h` | Actual signal-to-pin connections | The authoritative source for effective routing |

⚠️ **Do NOT infer effective routing from `cycfg_pins.h` alone.** The HSIOM macro defaults can appear as GPIO while the actual routing (in `cycfg_routing.h`) connects an alternate function.

## 2. Verification Checklist

1. Re-inspect `GeneratedSource/cycfg_peripherals.h` for the expected alias macro (e.g., `#define DEBUG_UART_HW SCB2`)
2. Read the relevant `GeneratedSource/` files and confirm:
   - The expected alias defines exist (e.g., `ALIAS_HW`, `ALIAS_NUM`, `ALIAS_ENABLED`)
   - Config struct values match what was requested (period, compare, mode, divider)
   - Pin HSIOM routing is correct (check `cycfg_pins.c/h` AND `cycfg_routing.h`)
   - Clock divider value matches the plan (check `cycfg_peripheral_clocks.c/h`)
   - No `cycfg_notices.h` errors
   Report any discrepancies to the user before proceeding to code integration.
3. Run `make build` from the multi-project root and check for errors from `cycfg_notices.h` — these indicate unresolved configuration tasks
4. If verification fails: inform the user and offer to re-open Device Configurator; do not proceed with code integration
5. Verify `#include "cycfg.h"` exists in `main.c` — add if absent
6. Verify `init_cycfg_all()` is called before any peripheral use in `main.c` — add if absent
