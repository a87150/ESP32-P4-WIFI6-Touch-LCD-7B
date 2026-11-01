# Flash ONLY the app partition and the storage (littlefs) data partition.
# Bootloader / partition table / NVS / factory assets are NEVER touched.
# Full-factory restore is always possible with
# firmware/ESP32-P4-WIFI6-Touch-LCD-7B-FactoryOnly.bin @0x0.
param(
    [string]$Port = "COM3",
    # Python that has esptool installed. Defaults to `python` on PATH, which is
    # what the ESP-IDF environment provides.
    [string]$Python = "python",
    [int]$Baud = 460800
)

$ErrorActionPreference = "Stop"
$proj = $PSScriptRoot

# Partition offsets from partitions.csv: factory app @0x110000, storage @0x1000000.
$APP_OFFSET     = "0x110000"
$STORAGE_OFFSET = "0x1000000"

$app     = Join-Path $proj "build\system_monitor.bin"
$storage = Join-Path $proj "build\storage.bin"

if (-not (Test-Path $app)) { throw "app image not found: $app (run 'idf.py build' first)" }

$common = @("--chip", "esp32p4", "-p", $Port, "-b", "$Baud",
            "--before", "default-reset", "--after", "hard-reset")

if (Test-Path $storage) {
    Write-Host "== flashing app @$APP_OFFSET + littlefs storage @$STORAGE_OFFSET on $Port"
    & $Python -m esptool @common write-flash $APP_OFFSET $app $STORAGE_OFFSET $storage
} else {
    Write-Host "== littlefs image not present, flashing app only @$APP_OFFSET on $Port"
    & $Python -m esptool @common write-flash $APP_OFFSET $app
}

if ($LASTEXITCODE -ne 0) { throw "esptool failed with exit code $LASTEXITCODE" }
Write-Host "== done (reset with --after hard-reset)"
