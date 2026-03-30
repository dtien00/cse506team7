# PowerShell build script for testing libc linkage
# This compiles test programs against your libc to identify missing symbols

$ErrorActionPreference = "Continue"

$CROSS = "riscv64-unknown-elf-"
$CC = "${CROSS}gcc"
$AR = "${CROSS}ar"

# Get compiler include path
$INCLUDE_PATH = & $CC -print-file-name=include
$CFLAGS = "-gdwarf-4 -Wall -Werror -Os -ffreestanding -fno-builtin -nostdlib -nostdinc -isystem $INCLUDE_PATH -mcmodel=medany -march=rv64imac_zicsr_zifencei -mabi=lp64 -ffunction-sections -fdata-sections -fno-tree-switch-conversion"

$BUILD_DIR = "build_test"
$LIBC_DIR = "..\..\libc"
$LIBC_INCLUDE = "$LIBC_DIR\include"

Write-Host "=== Building libc test suite ===" -ForegroundColor Cyan
Write-Host ""

# Create build directory
New-Item -ItemType Directory -Force -Path $BUILD_DIR | Out-Null

# Step 1: Build libc objects
Write-Host "Step 1: Compiling libc sources..." -ForegroundColor Yellow
$LIBC_OBJS = @()

Get-ChildItem "$LIBC_DIR\*.c" | ForEach-Object {
    $basename = $_.BaseName
    $obj = "$BUILD_DIR\libc_${basename}.o"
    Write-Host "  Compiling $($_.Name)..."
    & $CC $CFLAGS.Split() -I$LIBC_INCLUDE -c $_.FullName -o $obj
    $LIBC_OBJS += $obj
}

# Compile assembly files (except crt.S)
Get-ChildItem "$LIBC_DIR\*.S" | Where-Object { $_.BaseName -ne "crt" } | ForEach-Object {
    $basename = $_.BaseName
    $obj = "$BUILD_DIR\libc_${basename}.o"
    Write-Host "  Assembling $($_.Name)..."
    & $CC $CFLAGS.Split() -I$LIBC_INCLUDE -c $_.FullName -o $obj
    $LIBC_OBJS += $obj
}

# Build crt.o separately
Write-Host "  Assembling crt.S..."
& $CC $CFLAGS.Split() -I$LIBC_INCLUDE -c "$LIBC_DIR\crt.S" -o "$BUILD_DIR\crt.o"

# Step 2: Create libc archive
Write-Host ""
Write-Host "Step 2: Creating libc.a archive..." -ForegroundColor Yellow
& $AR rcs "$BUILD_DIR\libc.a" $LIBC_OBJS
Write-Host "  Created $BUILD_DIR\libc.a" -ForegroundColor Green

# Step 3: Try to link minimal test
Write-Host ""
Write-Host "Step 3: Linking minimal test (getpid, write, fork)..." -ForegroundColor Yellow
& $CC $CFLAGS.Split() -I$LIBC_INCLUDE "$BUILD_DIR\crt.o" "libc_test_minimal.c" "$BUILD_DIR\libc.a" -Wl,--gc-sections -o "$BUILD_DIR\libc_test_minimal" 2>&1 | Tee-Object -FilePath "$BUILD_DIR\minimal_link.log"
if ($LASTEXITCODE -eq 0) {
    Write-Host "  ✓ Minimal test linked successfully!" -ForegroundColor Green
} else {
    Write-Host "  ✗ Minimal test failed to link - check $BUILD_DIR\minimal_link.log for missing symbols" -ForegroundColor Red
}

# Step 4: Try to link full test
Write-Host ""
Write-Host "Step 4: Linking comprehensive test..." -ForegroundColor Yellow
& $CC $CFLAGS.Split() -I$LIBC_INCLUDE "$BUILD_DIR\crt.o" "libc_test.c" "$BUILD_DIR\libc.a" -Wl,--gc-sections -o "$BUILD_DIR\libc_test" 2>&1 | Tee-Object -FilePath "$BUILD_DIR\full_link.log"
if ($LASTEXITCODE -eq 0) {
    Write-Host "  ✓ Comprehensive test linked successfully!" -ForegroundColor Green
} else {
    Write-Host "  ✗ Comprehensive test failed to link - check $BUILD_DIR\full_link.log for missing symbols" -ForegroundColor Red
}

# Step 5: Show summary
Write-Host ""
Write-Host "=== Build Summary ===" -ForegroundColor Cyan
Write-Host "Build artifacts in: $BUILD_DIR\"
Write-Host "  - libc.a: Your libc library"
Write-Host "  - crt.o: C runtime startup"
Write-Host "  - libc_test_minimal: Minimal test binary (if successful)"
Write-Host "  - libc_test: Comprehensive test binary (if successful)"
Write-Host ""
Write-Host "To identify missing symbols, check the .log files for 'undefined reference' errors"
Write-Host ""

# Step 6: List missing symbols if any
Write-Host "=== Checking for missing symbols ===" -ForegroundColor Cyan

if (Test-Path "$BUILD_DIR\minimal_link.log") {
    $MISSING = Select-String -Path "$BUILD_DIR\minimal_link.log" -Pattern "undefined reference" | 
        ForEach-Object { $_ -replace ".*undefined reference to \`([^']*)'.*", '$1' } | 
        Sort-Object -Unique
    
    if ($MISSING) {
        Write-Host "Missing symbols in minimal test:" -ForegroundColor Red
        $MISSING | ForEach-Object { Write-Host "  - $_" }
    } else {
        Write-Host "No missing symbols in minimal test!" -ForegroundColor Green
    }
}

if (Test-Path "$BUILD_DIR\full_link.log") {
    $MISSING = Select-String -Path "$BUILD_DIR\full_link.log" -Pattern "undefined reference" | 
        ForEach-Object { $_ -replace ".*undefined reference to \`([^']*)'.*", '$1' } | 
        Sort-Object -Unique
    
    if ($MISSING) {
        Write-Host ""
        Write-Host "Missing symbols in comprehensive test:" -ForegroundColor Red
        $MISSING | ForEach-Object { Write-Host "  - $_" }
    } else {
        Write-Host "No missing symbols in comprehensive test!" -ForegroundColor Green
    }
}

Write-Host ""
Write-Host "Done!" -ForegroundColor Cyan
