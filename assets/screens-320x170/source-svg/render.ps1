param(
    [string]$ImageMagick = "magick"
)

$ErrorActionPreference = "Stop"
$destination = Split-Path -Parent $PSScriptRoot
$names = @("setup", "init", "miner", "clock", "network", "price", "solo")

foreach ($name in $names) {
    & $ImageMagick -background none `
        (Join-Path $PSScriptRoot "$name.svg") `
        -strip -colorspace sRGB `
        (Join-Path $destination "$name.png")
    if ($LASTEXITCODE -ne 0) {
        throw "ImageMagick konnte $name.svg nicht rendern."
    }
}

$setup = Join-Path $destination "setup.png"
$setupWithQr = Join-Path $destination "setup.with-qr.png"
& $ImageMagick $setup (Join-Path $PSScriptRoot "qr-wifi.png") `
    -geometry "+199+38" -composite -strip $setupWithQr
if ($LASTEXITCODE -ne 0) {
    throw "Der WLAN-QR-Code konnte nicht eingesetzt werden."
}
Move-Item -LiteralPath $setupWithQr -Destination $setup -Force

& $ImageMagick identify -format "%f %wx%h`n" `
    ($names | ForEach-Object { Join-Path $destination "$_.png" })
