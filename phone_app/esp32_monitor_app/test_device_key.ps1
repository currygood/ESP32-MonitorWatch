# Test with Device Key instead of Product Key
$productId = "1nF1D22kt0"
$deviceName = "MyTest"

# Try with Device Key
$deviceKey = "cFJaTlc4UkNzbnhBdG5QajVuVko0U3JTMlFUZm5Sb2E="

Write-Host "=========================================="
Write-Host "Test with DEVICE KEY (not product key)"
Write-Host "=========================================="
Write-Host ""

$version = "2018-10-31"
$method = "sha256"
$resource = "products/$productId/devices/$deviceName"
$expiry = [Math]::Floor((Get-Date).AddMinutes(10).ToUniversalTime().Subtract([DateTime]::new(1970,1,1)).TotalSeconds)

$sigSource = "$expiry`n$method`n$resource`n$version"

try {
    $keyBytes = [System.Convert]::FromBase64String($deviceKey)
    $contentBytes = [System.Text.Encoding]::UTF8.GetBytes($sigSource)
    
    $hmac = New-Object System.Security.Cryptography.HMACSHA256
    $hmac.Key = $keyBytes
    $hashBytes = $hmac.ComputeHash($contentBytes)
    $signatureBase64 = [System.Convert]::ToBase64String($hashBytes)
    $signatureEncoded = $signatureBase64.Replace('+', '%2B').Replace('/', '%2F').Replace('=', '%3D')
    
    $encodedResource = [System.Uri]::EscapeDataString($resource)
    $token = "version=$version&res=$encodedResource&et=$expiry&method=$method&sign=$signatureEncoded"
    
    Write-Host "Using Device Key: $deviceKey"
    Write-Host "Token: $token"
    Write-Host ""
    
    $url = "https://iot-api.heclouds.com/thingmodel/query-device-property?product_id=$productId&device_name=$deviceName"
    $headers = @{
        'Authorization' = $token
        'Accept' = 'application/json'
        'Content-Type' = 'application/json'
    }
    
    $response = Invoke-RestMethod -Uri $url -Method Get -Headers $headers
    
    Write-Host "Response:"
    Write-Host "  Code: $($response.code)"
    Write-Host "  Msg: $($response.msg)"
    
    if ($response.code -eq 0) {
        Write-Host ""
        Write-Host "SUCCESS! Device Key works!"
        Write-Host "Data: $(ConvertTo-Json $response.data -Depth 5)"
    } else {
        Write-Host ""
        Write-Host "FAILED with code $($response.code): $($response.msg)"
    }
} catch {
    Write-Host "Error: $_"
}