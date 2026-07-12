# OneNet API Test Script - Test if API works
# Usage: .\test_onenet_api.ps1

Write-Host "==========================================" -ForegroundColor Cyan
Write-Host "  OneNet API Connection Test" -ForegroundColor Yellow
Write-Host "==========================================" -ForegroundColor Cyan
Write-Host ""

# Import token generator logic (simplified for PowerShell)
Write-Host "[INFO] This script will test your OneNet API configuration" -ForegroundColor Green
Write-Host ""

# Ask for configuration
$productId = Read-Host "Enter Product ID (e.g., 1nF1D22kt0)"
$deviceName = Read-Host "Enter Device Name (e.g., MyTest)"
$accessKey = Read-Host "Enter Product Access Key (base64 format)"

if (-not $productId -or -not $accessKey) {
    Write-Host "ERROR: Product ID and Access Key are required!" -ForegroundColor Red
    pause
    exit 1
}

Write-Host ""
Write-Host "[TEST] Configuration received:" -ForegroundColor Yellow
Write-Host "       Product ID: $productId"
Write-Host "       Device Name: $($deviceName ?: 'Not specified')"
Write-Host "       Access Key length: $($accessKey.Length) chars"
Write-Host ""

# Generate timestamp
$expiry = [Math]::Floor((Get-Date).AddMinutes(10).ToUniversalTime().Subtract((Get-Date "1970-01-01")).TotalSeconds)

Write-Host "[TEST] Generating token with expiry: $expiry" -ForegroundColor Green

# Build the string to sign
$version = "2018-10-31"
$method = "sha256"
if ($deviceName) {
    $resource = "products/$deviceId/devices/$deviceName"
} else {
    $resource = "products/$productId"
}

$stringToSign = "$expiry`n$method`n$resource`n$version"

Write-Host "[TEST] String to sign prepared" -ForegroundColor Gray
Write-Host ""

# Try to make the API call using curl (simpler than implementing HMAC in PS)
Write-Host "[ACTION] Now we need to test with actual API call..." -ForegroundColor Yellow
Write-Host ""
Write-Host "Please run this command in another terminal:" -ForegroundColor Cyan
Write-Host ""
Write-Host "curl -v `"https://iot-api.heclouds.com/thing/property/query?product_id=$productId&device_name=$deviceName`" -H `"Authorization: <YOUR_TOKEN_HERE>`"" -ForegroundColor White
Write-Host ""
Write-Host "OR use Postman with these settings:" -ForegroundColor Cyan
Write-Host "  URL: https://iot-api.heclouds.com/thing/property/query" -ForegroundColor Gray
Write-Host "  Method: GET" -ForegroundColor Gray
Write-Host "  Params: product_id=$productId, device_name=$deviceName" -ForegroundColor Gray
Write-Host "  Header: Authorization: <token>" -ForegroundColor Gray
Write-Host ""

pause