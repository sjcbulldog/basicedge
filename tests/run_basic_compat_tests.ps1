param([string]$Compiler = "gcc")
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
$executable = Join-Path ([System.IO.Path]::GetTempPath()) ("basic-compat-" + [guid]::NewGuid() + ".exe")
$catalogExecutable = Join-Path ([System.IO.Path]::GetTempPath()) ("basic-catalog-" + [guid]::NewGuid() + ".exe")
Push-Location $root
try {
    & $Compiler -std=c11 -w -I proj_cm55\include -I proj_cm55\third_party\my_basic `
        tests\basic_compat_test.c proj_cm55\source\basic_compat.c `
        proj_cm55\third_party\my_basic\my_basic.c -lm -o $executable
    if ($LASTEXITCODE -ne 0) { throw "Host compilation failed." }
    & $executable basic\trek.bas | Tee-Object -Variable output
    if ($LASTEXITCODE -ne 0) { throw "Classic BASIC regression tests failed." }
    $text = $output -join "`n"
    if ($text.Contains("(EMPTY)") -or !$text.Contains("DISTANCE =1.41421") -or
        !$text.Contains("WARP ENGINES") -or !$text.Contains("COMMAND?")) {
        throw "Trek output regression."
    }
    & $Compiler -std=c11 -Wall -Wextra -Werror -I proj_cm55\include `
        tests\basic_catalog_test.c proj_cm55\source\basic_catalog.c -o $catalogExecutable
    if ($LASTEXITCODE -ne 0) { throw "Catalog host compilation failed." }
    & $catalogExecutable basic\progs.json
    if ($LASTEXITCODE -ne 0) { throw "BASIC catalog tests failed." }
}
finally {
    Pop-Location
    if (Test-Path $executable) { Remove-Item -LiteralPath $executable }
    if (Test-Path $catalogExecutable) { Remove-Item -LiteralPath $catalogExecutable }
}
