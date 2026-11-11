$ErrorActionPreference = 'Stop'

$qaniRoot = 'C:\Qorvo\QANI_3_2_1\QANI_Source\QANI-All-FreeRTOS_QNI_3_0_0'
$projectDir = Join-Path $qaniRoot 'Projects\Projects\QANI\FreeRTOS\DWM3001CDK'
$commonDir = Join-Path $qaniRoot 'Projects\Projects\QANI\FreeRTOS\QANI-FreeRTOS-Common'
$patchDir = Split-Path -Parent $MyInvocation.MyCommand.Path

$targets = @(
    @{
        Source = Join-Path $patchDir 'ProjectDefinition\ProjectName.c'
        Target = Join-Path $projectDir 'ProjectDefinition\ProjectName.c'
    },
    @{
        Source = Join-Path $patchDir 'QANI-FreeRTOS-Common\main.c'
        Target = Join-Path $commonDir 'main.c'
    }
)

foreach ($item in $targets) {
    if (!(Test-Path -LiteralPath $item.Source)) {
        throw "Patch source missing: $($item.Source)"
    }
    if (!(Test-Path -LiteralPath $item.Target)) {
        throw "QANI source target missing: $($item.Target)`nCheck qaniRoot at the top of this script. No files have been copied yet if this is the first failure."
    }
}

$stamp = Get-Date -Format 'yyyyMMdd_HHmmss'
foreach ($item in $targets) {
    $backup = "$($item.Target).before_tracker_$stamp.bak"
    Copy-Item -LiteralPath $item.Target -Destination $backup
    Copy-Item -LiteralPath $item.Source -Destination $item.Target -Force
    Write-Host "Updated: $($item.Target)"
    Write-Host "Backup:  $backup"
}

Write-Host ''
Write-Host 'Tracker identity patch applied.'
Write-Host 'Open the existing DWM3001CDK-QANI-FreeRTOS.emProject in SEGGER Embedded Studio and build with F7.'
