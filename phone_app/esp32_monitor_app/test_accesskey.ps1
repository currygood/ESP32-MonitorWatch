# Test AccessKey decoding
$accessKey = "3S1H19uDdRWLXVslxembo2+P0RWTXcXB56txJWnhA9c="

Write-Host "Original AccessKey: $accessKey"
Write-Host "Length: $($accessKey.Length)"
Write-Host ""

try {
    $bytes = [System.Convert]::FromBase64String($accessKey)
    Write-Host "SUCCESS: Base64 decoded to $($bytes.Length) bytes"
    Write-Host "Bytes (hex): $($bytes -join ',')"
} catch {
    Write-Host "ERROR: Failed to decode base64!"
    Write-Host "Error: $_"
}