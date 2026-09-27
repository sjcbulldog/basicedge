# Troubleshooting

| Condition | Action |
| --------- | ------ |
| Device Configurator fails to launch | Report the error from running the device-configurator |
| User closed Device Configurator without saving (alias absent from `GeneratedSource/`) | Inform the user; offer to re-open Device Configurator |
| User chose a different alias than instructed | Confirm with the user whether to proceed with their alias or re-open Device Configurator |
| Peripheral already configured with conflicting settings | Report the conflict; ask the user how to proceed |
| Build fails with `cycfg_notices.h` errors | Report the unresolved tasks; offer to re-open Device Configurator |
| Device Configurator DRC errors block save | Instruct the user to resolve all errors shown in the Errors/Warnings panel before closing; if available, include the specific DRC error text |
| Peripheral instance not available for the selected device | Inform the user the requested instance does not exist on this device; suggest an alternative instance from the personality file |
| Clock source unavailable for the chosen peripheral | Direct the user to first enable a compatible clock divider in the Clocks tab, then return to the peripheral settings |
