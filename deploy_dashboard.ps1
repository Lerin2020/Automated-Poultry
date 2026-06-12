#!/usr/bin/env pwsh
# Deploy dashboard to ESP32 SPIFFS
# Run from project root: .\deploy_dashboard.ps1

$ErrorActionPreference = "Stop"

Write-Host "`n=== Building Dashboard ===" -ForegroundColor Cyan
Set-Location dashboard
npm run build
Set-Location ..

Write-Host "`n=== Preparing SPIFFS data ===" -ForegroundColor Cyan
$dataDir = "firmware\data"

# Clean old data (keep config.json and queue.json)
if (Test-Path $dataDir) {
    Get-ChildItem $dataDir -Exclude "config.json","queue.json" | Remove-Item -Recurse -Force
} else {
    New-Item -ItemType Directory -Path $dataDir -Force | Out-Null
}

# Copy built files
Copy-Item "dashboard\dist\index.html" "$dataDir\index.html"

# Copy and gzip the large JS/CSS assets
$assetsDir = "$dataDir\assets"
New-Item -ItemType Directory -Path $assetsDir -Force | Out-Null

Get-ChildItem "dashboard\dist\assets" -File | ForEach-Object {
    $src = $_.FullName
    $dest = "$assetsDir\$($_.Name)"
    
    # For JS and CSS, create gzipped versions (much smaller)
    if ($_.Extension -match '\.(js|css)$') {
        $gzDest = "$dest.gz"
        Write-Host "  Compressing: $($_.Name) -> $($_.Name).gz"
        
        $bytes = [System.IO.File]::ReadAllBytes($src)
        $ms = New-Object System.IO.MemoryStream
        $gz = New-Object System.IO.Compression.GZipStream($ms, [System.IO.Compression.CompressionMode]::Compress)
        $gz.Write($bytes, 0, $bytes.Length)
        $gz.Close()
        [System.IO.File]::WriteAllBytes($gzDest, $ms.ToArray())
        $ms.Close()
        
        $origSize = [math]::Round($bytes.Length / 1024, 1)
        $gzSize = [math]::Round((Get-Item $gzDest).Length / 1024, 1)
        Write-Host "    $origSize KB -> $gzSize KB" -ForegroundColor Green
    } else {
        Copy-Item $src $dest
    }
}

Write-Host "`n=== SPIFFS Contents ===" -ForegroundColor Cyan
Get-ChildItem $dataDir -Recurse -File | ForEach-Object {
    $rel = $_.FullName.Replace((Resolve-Path $dataDir).Path, "")
    $size = [math]::Round($_.Length / 1024, 1)
    Write-Host "  $rel ($size KB)"
}

$totalKB = [math]::Round((Get-ChildItem $dataDir -Recurse -File | Measure-Object -Property Length -Sum).Sum / 1024, 1)
Write-Host "`n  Total: $totalKB KB" -ForegroundColor Yellow

Write-Host "`n=== Upload to ESP32 ===" -ForegroundColor Cyan
Write-Host "Run this command in PlatformIO terminal:" -ForegroundColor White
Write-Host "  pio run --target uploadfs" -ForegroundColor Green
Write-Host ""
