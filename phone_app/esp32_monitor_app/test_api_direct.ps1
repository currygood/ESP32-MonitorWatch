# Direct API Test Script
$productId = "1nF1D22kt0"
$deviceName = "MyTest"
$accessKey = "3S1H19uDdRWLXVslxembo2+P0RWTXcXB56txJWnhA9c="

# Generate token manually (PowerShell version)
$version = "2018-10-31"
$method = "sha256"
$resource = "products/$productId/devices/$deviceName"
$expiry = [Math]::Floor((Get-Date).AddMinutes(10).ToUniversalTime().Subtract([DateTime]::new(1970,1,1)).TotalSeconds)

Write-Host "=========================================="
Write-Host "OneNet API Direct Test"
Write-Host "=========================================="
Write-Host ""
Write-Host "Configuration:"
Write-Host "  Product ID: $productId"
Write-Host "  Device Name: $deviceName"
Write-Host "  Access Key: $accessKey"
Write-Host "  Method: $method"
Write-Host "  Expiry: $expiry"
Write-Host "  Resource: $resource"
Write-Host ""

# Create signature source string
$sigSource = "$expiry`n$method`n$resource`n$version"
Write-Host "Signature Source:"
Write-Host "  $sigSource"
Write-Host ""

# Decode access key and create HMAC
$keyBytes = [System.Convert]::FromBase64String($accessKey)
$contentBytes = [System.Text.Encoding]::UTF8.GetBytes($sigSource)

$hmac = New-Object System.Security.Cryptography.HMACSHA256
$hmac.Key = $keyBytes
$hashBytes = $hmac.ComputeHash($contentBytes)
$signatureBase64 = [System.Convert]::ToBase64String($hashBytes)
$signatureEncoded = $signatureBase64.Replace('+', '%2B').Replace('/', '%2F').Replace('=', '%3D')

Write-Host "Generated Signature:"
Write-Host "  Base64: $signatureBase64"
Write-Host "  Encoded: $signatureEncoded"
Write-Host ""

# Build token
$encodedResource = [System.Uri]::EscapeDataString($resource)
$token = "version=$version&res=$encodedResource&et=$expiry&method=$method&sign=$signatureEncoded"

Write-Host "Complete Token:"
Write-Host "  $token"
Write-Host ""

# Make API request
$url = "https://iot-api.heclouds.com/thingmodel/query-device-property?product_id=$productId&device_name=$deviceName"

Write-Host "=========================================="
Write-Host "Making API Request..."
Write-Host "  URL: $url"
Write-Host ""

try {
    $headers = @{
        'Authorization' = $token
        'Accept' = 'application/json'
        'Content-Type' = 'application/json'
    }

    $response = Invoke-RestMethod -Uri $url -Method Get -Headers $headers

    Write-Host "Response:"
    Write-Host "  Code: $($response.code)"
    Write-Host "  Msg: $($response.msg)"
    Write-Host "  Data: $(ConvertTo-Json $response.data -Depth 5)"
    Write-Host ""
    
    if ($response.code -eq 0) {
        Write-Host "SUCCESS! Data retrieved!"
    } else {
        Write-Host "FAILED with code $($response.code): $($response.msg)"
    }
} catch {
    Write-Host "Error occurred:"
    Write-Host "  $_"
}