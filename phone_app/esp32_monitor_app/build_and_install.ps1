# OneNet ESP32 Monitor App Build Script
# Usage: .\build_and_install.ps1

Write-Host "======================================" -ForegroundColor Cyan
Write-Host "  ESP32 Monitor App - Build & Install" -ForegroundColor Yellow
Write-Host "======================================" -ForegroundColor Cyan
Write-Host ""

$ProjectPath = "F:\EmbeddedProject\Esp32MonitorWatch\app\esp32_monitor_app"
$AndroidPath = "$ProjectPath\android"
$ApkPath = "$ProjectPath\build\app\outputs\flutter-apk\app-debug.apk"

Write-Host "[1/5] Setting up environment..." -ForegroundColor Green
$env:GRADLE_USER_HOME = 'F:\gradle-home'
$env:PUB_HOSTED_URL = 'https://pub-web.flutter-io.cn'
$env:FLUTTER_STORAGE_BASE_URL = 'https://storage.flutter-io.cn'
Write-Host "       OK: Gradle cache and mirrors configured" -ForegroundColor Gray
Write-Host ""

Write-Host "[2/5] Getting Flutter dependencies..." -ForegroundColor Green
Set-Location $ProjectPath
flutter pub get
if ($LASTEXITCODE -ne 0) {
    Write-Host "FAILED: Flutter pub get failed!" -ForegroundColor Red
    pause
    exit 1
}
Write-Host ""

Write-Host "[3/5] Building APK with Gradle (this takes a few minutes)..." -ForegroundColor Yellow
Set-Location $AndroidPath
.\gradlew.bat --no-daemon assembleDebug
if ($LASTEXITCODE -ne 0) {
    Write-Host "FAILED: Build failed!" -ForegroundColor Red
    pause
    exit 1
}
Write-Host ""

Write-Host "[4/5] Checking USB device connection..." -ForegroundColor Green
adb devices
Write-Host ""
$devices = adb devices | Select-String -Pattern "device$" -NotMatch -SimpleMatch
$deviceCount = ($devices | Measure-Object).Count
if ($deviceCount -eq 0) {
    Write-Host "WARNING: No device detected!" -ForegroundColor Yellow
    Write-Host "   Please check:" -ForegroundColor Gray
    Write-Host "   1. USB connected" -ForegroundColor Gray
    Write-Host "   2. USB debugging enabled on phone" -ForegroundColor Gray
    Write-Host "   3. USB debugging authorized" -ForegroundColor Gray
    Write-Host ""
    Read-Host "Press Enter to try install anyway..."
}
Write-Host ""

Write-Host "[5/5] Installing APK to device..." -ForegroundColor Green
if (Test-Path $ApkPath) {
    Set-Location $ProjectPath
    adb install -r $ApkPath
    if ($LASTEXITCODE -eq 0) {
        Write-Host ""
        Write-Host "======================================" -ForegroundColor Cyan
        Write-Host "  SUCCESS! App installed!" -ForegroundColor Green
        Write-Host "  Open the app on your phone now." -ForegroundColor Green
        Write-Host "======================================" -ForegroundColor Cyan
        Write-Host ""
        Write-Host "TIP: Check console logs for API debug info" -ForegroundColor Yellow
    } else {
        Write-Host "FAILED: Install failed!" -ForegroundColor Red
    }
} else {
    Write-Host "ERROR: APK not found at $ApkPath" -ForegroundColor Red
}

Write-Host ""
pause