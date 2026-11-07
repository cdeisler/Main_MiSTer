[CmdletBinding()]
param(
    [ValidateSet('Verify', 'List', 'Build', 'Upload', 'Monitor')]
    [string]$Action = 'Verify',
    [ValidateSet('Mouse', 'MouseY', 'Dial', 'Diagnostic', 'All')]
    [string]$Mode = 'Mouse',
    [string]$Port
)

$ErrorActionPreference = 'Stop'
$cli = (Get-Command arduino-cli -ErrorAction Stop).Source
$fqbn = 'arduino:avr:micro'
$libraryPath = Join-Path $PSScriptRoot 'libraries'

function Invoke-Arduino {
    param([string[]]$Arguments)
    & $cli @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "Arduino CLI failed (exit $LASTEXITCODE)."
    }
}

function Build-Firmware {
    param([string]$SelectedMode)
    $sketchName = if ($SelectedMode -eq 'Diagnostic') { 'optical-diagnostic' } else { 'optical-spinner' }
    $sketchPath = Join-Path $PSScriptRoot $sketchName
    $outputPath = Join-Path $PSScriptRoot ".build/$SelectedMode"
    $cachePath = Join-Path $PSScriptRoot ".build/cache/$SelectedMode"
    $arguments = @('compile', '--fqbn', $fqbn, '--libraries', $libraryPath,
                   '--build-path', $cachePath, '--output-dir', $outputPath)
    if ($SelectedMode -ne 'Diagnostic') {
        $usbMode = if ($SelectedMode -eq 'Dial') { 0 } else { 1 }
        $mouseAxis = if ($SelectedMode -eq 'MouseY') { 1 } else { 0 }
        $arguments += @('--build-property',
            "compiler.cpp.extra_flags=-DSPINNER_USB_MODE=$usbMode -DSPINNER_MOUSE_AXIS=$mouseAxis")
    }
    $arguments += $sketchPath
    Write-Host "Building $SelectedMode firmware for $fqbn"
    Invoke-Arduino -Arguments $arguments
}

switch ($Action) {
    'Verify' {
        Invoke-Arduino -Arguments @('version')
        Invoke-Arduino -Arguments @('core', 'list')
        Invoke-Arduino -Arguments @('lib', 'list')
        Invoke-Arduino -Arguments @('board', 'list')
    }
    'List' {
        Invoke-Arduino -Arguments @('board', 'list')
    }
    'Build' {
        $modes = if ($Mode -eq 'All') { @('Mouse', 'MouseY', 'Dial', 'Diagnostic') } else { @($Mode) }
        foreach ($selectedMode in $modes) {
            Build-Firmware -SelectedMode $selectedMode
        }
    }
    'Upload' {
        if ([string]::IsNullOrWhiteSpace($Port)) {
            throw 'Specify the current Micro port with -Port; use -Action List first.'
        }
        if ($Mode -eq 'All') {
            throw 'Choose exactly one firmware mode to upload.'
        }
        Build-Firmware -SelectedMode $Mode
        $sketchName = if ($Mode -eq 'Diagnostic') { 'optical-diagnostic' } else { 'optical-spinner' }
        Invoke-Arduino -Arguments @('upload', '--fqbn', $fqbn, '--port', $Port,
            '--input-dir', (Join-Path $PSScriptRoot ".build/$Mode"),
            (Join-Path $PSScriptRoot $sketchName))
    }
    'Monitor' {
        if ([string]::IsNullOrWhiteSpace($Port)) {
            throw 'Specify the runtime serial port with -Port; use -Action List first.'
        }
        Invoke-Arduino -Arguments @('monitor', '--port', $Port, '--config', 'baudrate=115200')
    }
}