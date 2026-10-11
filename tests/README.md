# Classic BASIC tests

Run from the repository root with a host GCC compiler:

```powershell
.\tests\run_basic_compat_tests.ps1 -Compiler C:\cygwin64\bin\gcc.exe
```

The runner builds the production compatibility parser and vendored MY-BASIC
interpreter, executes targeted regressions and two scripted sessions of the
unmodified [Trek program](../basic/trek.bas), checks game output, and removes its
temporary executable. It also builds the WiFi program-catalog parser with
warnings as errors, checks [progs.json](../basic/progs.json), lookup rules,
and malformed or unsafe catalogs. No board, SD card, or network is needed. See
[BASIC documentation](../docs/basic.md#classic-basic-parser) for dialect limits
and hardware verification.
