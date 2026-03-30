# Build script for testing libc implementation (PowerShell)
# This script compiles test programs against the SBUnix libc

$ErrorActionPreference = "Stop"

$CROSS = "riscv64-unknown-elf-"
$CC = "${CROSS}gcc"

Write-Host "Building libc..." -ForegroundColor Cyan
make build/libc.a

Write-Host ""
Write-Host "Compiling test programs..." -ForegroundColor Cyan

$tests = Get-ChildItem -Path "dump\test_*.c"

foreach ($test in $tests) {
    $testname = $test.BaseName
    Write-Host "  Compiling $testname..." -ForegroundColor Yellow
    
    $logfile = "dump\${testname}.log"
    
    $output = & $CC -gdwarf-4 -Wall -Werror -Os -ffreestanding -fno-builtin `
        -nostdlib -nostdinc `
        -isystem $(& $CC -print-file-name=include) `
        -mcmodel=medany -march=rv64imac_zicsr_zifencei -mabi=lp64 `
        -ffunction-sections -fdata-sections -Ilibc/include `
        build/libc/crt.S.o $test.FullName build/libc.a `
        -Wl,--gc-sections -o "dump\${testname}" 2>&1
    
    $output | Out-File -FilePath $logfile
    
    if ($LASTEXITCODE -eq 0) {
        Write-Host "    ✓ ${testname} compiled successfully" -ForegroundColor Green
    } else {
        Write-Host "    ✗ ${testname} failed to compile" -ForegroundColor Red
        Write-Host "    Check $logfile for errors"
    }
}

Write-Host ""
Write-Host "Checking for undefined symbols..." -ForegroundColor Cyan

$logs = Get-ChildItem -Path "dump\test_*.log"
foreach ($log in $logs) {
    $content = Get-Content $log.FullName -Raw
    if ($content -match "undefined reference") {
        Write-Host "Found undefined symbols in $($log.Name):" -ForegroundColor Red
        Select-String -Path $log.FullName -Pattern "undefined reference" | ForEach-Object {
            Write-Host "  $($_.Line)"
        }
    }
}

Write-Host ""
Write-Host "Build complete! Test binaries are in dump/" -ForegroundColor Green
Write-Host "To run tests, boot the kernel with: make qemu"
