param([Parameter(Mandatory=$true)][ValidateSet('baremetal','freertos','coroutines')][string]$Variant,
      [switch]$ObserveOnly, [switch]$ExerciseButton, [double]$Seconds = 2, [string]$Probe)
$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path "$PSScriptRoot\..\..").Path
$python = "$projectRoot\build-hardware-probe\venv\Scripts\python.exe"
$env:PYTHONIOENCODING = 'utf-8'
$probeArgs = @("$PSScriptRoot\hardware.py", $Variant, '--seconds', "$Seconds")
if ($ObserveOnly) { $probeArgs += '--observe-only' }
if ($ExerciseButton) { $probeArgs += '--exercise-button' }
if ($Probe) { $probeArgs += @('--probe', $Probe) }
& $python @probeArgs
if ($LASTEXITCODE) { throw "Hardware check failed ($LASTEXITCODE)" }
