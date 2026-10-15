param([string]$FreeRTOSSource)
$ErrorActionPreference = 'Stop'
$exampleRoot = $PSScriptRoot
$projectRoot = (Resolve-Path "$exampleRoot\..\..").Path
$depsRoot = "$projectRoot\build-nucleo-wb55\deps"
New-Item -ItemType Directory -Force "$depsRoot\cmsis", "$depsRoot\device", "$depsRoot\freertos" | Out-Null
function Fetch-Header([string]$Url, [string]$Destination) {
    if (!(Test-Path -LiteralPath $Destination)) {
        Invoke-WebRequest -Uri $Url -OutFile $Destination -UseBasicParsing
    }
}
$coreSha = '2b7495b8535bdcb306dac29b9ded4cfb679d7e5c'
foreach ($header in @('core_cm4.h','cmsis_version.h','cmsis_compiler.h','cmsis_gcc.h','cmsis_armclang.h','cmsis_armcc.h','mpu_armv7.h')) {
    Fetch-Header "https://raw.githubusercontent.com/ARM-software/CMSIS_5/$coreSha/CMSIS/Core/Include/$header" "$depsRoot\cmsis\$header"
}
Fetch-Header "https://raw.githubusercontent.com/ARM-software/CMSIS_5/$coreSha/LICENSE.txt" "$depsRoot\cmsis\LICENSE.txt"
$deviceSha = 'cda2cb9fc4a5232ab18efece0bb06b0b60910083'
foreach ($header in @('stm32wbxx.h','stm32wb55xx.h','system_stm32wbxx.h')) {
    Fetch-Header "https://raw.githubusercontent.com/STMicroelectronics/cmsis-device-wb/$deviceSha/Include/$header" "$depsRoot\device\$header"
}
Fetch-Header "https://raw.githubusercontent.com/STMicroelectronics/cmsis-device-wb/$deviceSha/LICENSE.md" "$depsRoot\device\LICENSE.md"
if (!(Test-Path "$depsRoot\freertos\tasks.c")) {
    if ($FreeRTOSSource) {
        $archivePath = "$depsRoot\freertos.tar"
        $safeSource = $FreeRTOSSource.Replace('\','/')
        git -c "safe.directory=$safeSource" -C $FreeRTOSSource archive --format=tar --output=$archivePath a8c9d351520d43bd94692361bd67b6d798985c98
        if ($LASTEXITCODE) { throw 'FreeRTOS archive failed' }
        tar -xf $archivePath -C "$depsRoot\freertos"
    } else {
        $archivePath = "$depsRoot\freertos.tar.gz"
        Invoke-WebRequest 'https://codeload.github.com/FreeRTOS/FreeRTOS-Kernel/tar.gz/a8c9d351520d43bd94692361bd67b6d798985c98' -OutFile $archivePath -UseBasicParsing
        tar -xzf $archivePath --strip-components=1 -C "$depsRoot\freertos"
    }
    if ($LASTEXITCODE) { throw 'FreeRTOS extraction failed' }
}
Write-Output "Dependencies prepared: $depsRoot"
