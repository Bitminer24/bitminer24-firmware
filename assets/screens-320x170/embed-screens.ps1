param(
    [string]$ImageMagick = "magick"
)

$ErrorActionPreference = "Stop"
$projectRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$mediaFile = Join-Path $projectRoot "components\bm24_media\bm24_media.c"
$source = [System.IO.File]::ReadAllText($mediaFile)

$screens = [ordered]@{
    setup   = "bm24_img_setup"
    miner   = "bm24_img_miner"
    init    = "bm24_img_init"
    clock   = "bm24_img_clock"
    network = "bm24_img_network"
    price   = "bm24_img_price"
    solo    = "bm24_img_solo"
}

foreach ($entry in $screens.GetEnumerator()) {
    $png = Join-Path $PSScriptRoot "$($entry.Key).png"
    $raw = [System.IO.Path]::GetTempFileName()
    try {
        & $ImageMagick $png -alpha off -colorspace sRGB -depth 8 "rgb:$raw"
        if ($LASTEXITCODE -ne 0) {
            throw "ImageMagick konnte $png nicht als RGB8 lesen."
        }

        $bytes = [System.IO.File]::ReadAllBytes($raw)
        if ($bytes.Length -ne (320 * 170 * 3)) {
            throw "$png hat nicht genau 320x170 RGB8-Pixel."
        }

        $body = [System.Text.StringBuilder]::new(420000)
        for ($pixel = 0; $pixel -lt (320 * 170); $pixel++) {
            if (($pixel % 16) -eq 0) {
                [void]$body.Append("    ")
            }
            $offset = $pixel * 3
            $r = [int]$bytes[$offset]
            $g = [int]$bytes[$offset + 1]
            $b = [int]$bytes[$offset + 2]
            $rgb565 = (($r -band 0xF8) -shl 8) -bor
                      (($g -band 0xFC) -shl 3) -bor
                      ($b -shr 3)
            [void]$body.AppendFormat("0x{0:X4}", $rgb565)
            if ($pixel -lt (320 * 170 - 1)) {
                [void]$body.Append(",")
            }
            if (($pixel % 16) -eq 15) {
                [void]$body.Append("`n")
            }
        }

        $symbol = [regex]::Escape($entry.Value)
        $pattern = "(?s)(const uint16_t $symbol\[BM24_IMG_PIXELS\] = \{\r?\n).*?(\r?\n\};)"
        $match = [regex]::Match($source, $pattern)
        if (-not $match.Success) {
            throw "Array $($entry.Value) wurde in bm24_media.c nicht gefunden."
        }
        $replacement = $match.Groups[1].Value + $body.ToString().TrimEnd() +
                       $match.Groups[2].Value
        $source = $source.Substring(0, $match.Index) + $replacement +
                  $source.Substring($match.Index + $match.Length)
        Write-Host "$($entry.Key).png -> $($entry.Value)"
    }
    finally {
        if (Test-Path -LiteralPath $raw) {
            Remove-Item -LiteralPath $raw
        }
    }
}

$utf8NoBom = [System.Text.UTF8Encoding]::new($false)
[System.IO.File]::WriteAllText($mediaFile, $source, $utf8NoBom)
Write-Host "Aktualisiert: $mediaFile"
