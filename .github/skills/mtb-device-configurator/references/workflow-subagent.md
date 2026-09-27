# Workflow 2 — Sub-skill Invocation

**Library-tier skills MUST invoke this skill as a sub-agent** — they must not generate Device Configurator instructions inline. When invoked by a library-tier skill, the calling skill provides: peripheral type, required alias, personality name, and settings. Library-tier skills are pre-installed by Infineon ModusToolbox AI Assistant at `.github/skills/<library-name>/` and are already discoverable by Copilot. Unless necessary to provide feedback for available settings, skip personality discovery (step 4 above) and proceed from step 5 using the provided values.

**Calling convention:** Library skills MUST pass invocation data using the following structured block. This ensures consistent parsing regardless of which library skill is invoking mtb-device-configurator.

```
<!-- mtb-device-configurator sub-agent invocation -->
peripheral_type: <e.g., SCB UART>
personality: <e.g., uart-3.0>
alias: <e.g., DEBUG_UART>
settings:
  - section: <Section>
    setting: <Setting>
    value: <Value>
  - section: <Section>
    setting: <Setting>
    value: <Value>
```

Example:

```
<!-- mtb-device-configurator sub-agent invocation -->
peripheral_type: SCB UART
personality: uart-3.0
alias: DEBUG_UART
settings:
  - section: General
    setting: Baud Rate (bps)
    value: 115200
  - section: Connections
    setting: RX
    value: P6[5]
  - section: Connections
    setting: TX
    value: P6[7]
```
