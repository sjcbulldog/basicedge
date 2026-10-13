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
    & $executable basic\trek.bas basic\amazing.bas ..\desktop\assets\grtrek.bas | Tee-Object -Variable output
    if ($LASTEXITCODE -ne 0) { throw "Classic BASIC regression tests failed." }
    $text = $output -join "`n"
    if ($text.Contains("(EMPTY)") -or !$text.Contains("DISTANCE =1.41421") -or
        !$text.Contains("WARP ENGINES") -or !$text.Contains("COMMAND?")) {
        throw "Trek output regression."
    }
    $begin = [array]::IndexOf([string[]]$output, "#TRACE-BEGIN")
    $end = [array]::IndexOf([string[]]$output, "#TRACE-END")
    $trace = if ($begin -ge 0 -and $end -gt $begin) { ($output[($begin + 1)..($end - 1)] -join "`n") -replace "`r", "" } else { "" }
    $expectedTrace = @(
        "TRACE 10: REM HELLO: WORLD", "TRACE 20: X=1", "TRACE 20: Y=2",
        "TRACE 30: IF X=1 THEN 50", "TRACE 50: FOR I=1 TO 2", "TRACE 50: NEXT I",
        "TRACE 50: NEXT I", "TRACE 60: DEF FNA(Q)=Q+1", "TRACE 70: PRINT `"A`";",
        "A", "TRACE 70: Z=FNA(1)", "TRACE 80: IF X=2 THEN",
        "TRACE 90: IF X=1 THEN", "TRACE 90: Y=5", "TRACE 90: GOTO 110",
        "TRACE 110: END") -join "`n"
    if ($trace -ne $expectedTrace) {
        throw "TRACE output regression:`n$trace"
    }
    $mazeRows = @($output | Where-Object { $_ -match '^I[ I]{36}$' })
    $mazeFloors = @($output | Where-Object { $_ -match '^(:--|:  ){12}\.$' })
    if (!$text.Contains("MEANINGLESS DIMENSIONS") -or $mazeRows.Count -ne 9 -or
        $mazeFloors.Count -ne 9) {
        throw "Amazing output regression."
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
