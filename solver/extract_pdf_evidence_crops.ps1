param(
    [string]$PdfPath = (Join-Path (Split-Path $PSScriptRoot -Parent) "Siemens_U273_Limiter.pdf"),
    [string]$OutDir = (Join-Path (Split-Path $PSScriptRoot -Parent) "results\pdf_evidence")
)

$ErrorActionPreference = "Stop"

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

node (Join-Path $PSScriptRoot "extract_pdf_ccitt_images.js") $PdfPath $OutDir

Add-Type -AssemblyName System.Drawing

$sourceTiff = Join-Path $OutDir "pdf_image_obj56_3289x2304_photometric0.tif"
$sourcePng = Join-Path $OutDir "pdf_image_obj56_3289x2304_photometric0.png"

$page = [System.Drawing.Image]::FromFile($sourceTiff)
try {
    $page.Save($sourcePng, [System.Drawing.Imaging.ImageFormat]::Png)
}
finally {
    $page.Dispose()
}

function Save-Crop {
    param(
        [System.Drawing.Image]$Image,
        [string]$Name,
        [int]$X,
        [int]$Y,
        [int]$Width,
        [int]$Height
    )

    $bitmap = New-Object System.Drawing.Bitmap $Width, $Height
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    try {
        $graphics.DrawImage(
            $Image,
            [System.Drawing.Rectangle]::new(0, 0, $Width, $Height),
            [System.Drawing.Rectangle]::new($X, $Y, $Width, $Height),
            [System.Drawing.GraphicsUnit]::Pixel)
        $bitmap.Save((Join-Path $OutDir $Name), [System.Drawing.Imaging.ImageFormat]::Png)
    }
    finally {
        $graphics.Dispose()
        $bitmap.Dispose()
    }
}

$image = [System.Drawing.Image]::FromFile($sourcePng)
try {
    Save-Crop $image "crop_b11_global.png" 1350 1050 1600 900
    Save-Crop $image "crop_b11_ts1_ts2_r13_r16_c8_c9_wide.png" 2050 1300 650 650
    Save-Crop $image "crop_b11_c5_c11_cmd_wide.png" 1350 1000 1800 950
    Save-Crop $image "crop_b11_ts2_r15_r16_c8_c9_close.png" 1850 1210 720 620
    Save-Crop $image "crop_b11_c5_s6_close.png" 2280 1220 720 620
    Save-Crop $image "crop_b11_c11_detector_cmd_area.png" 900 1050 900 1050
    Save-Crop $image "crop_b11_s7_c11_bridge_boundary.png" 900 650 780 760
    Save-Crop $image "crop_b11_s6_to_b6_boundary_cmd_trace.png" 2360 720 780 940
    Save-Crop $image "crop_b11_s6_right_output_r1_r2_trace.png" 2470 1180 620 720
}
finally {
    $image.Dispose()
}

Write-Host "Wrote B11 PDF evidence crops to $OutDir"
