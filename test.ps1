# Uji coba Midi Roll tanpa membuka Resolume.
#
#   .\test.ps1              jalankan semua tes
#   .\test.ps1 -Seconds 5   durasi tes harness
#
# Tes 1: metadata   - DLL dimuat, semua parameter terdaftar
# Tes 2: end-to-end - konteks OpenGL nyata, plugin instantiate, render,
#                     envelope bergerak, note + CC tertangkap di loopMIDI

param(
	[int]$Seconds = 3,
	[string]$Port = "loopmidi"
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$dll = Join-Path ([Environment]::GetFolderPath("MyDocuments")) "Resolume Arena\Extra Effects\MidiRoll.dll"
$probe = Join-Path $root "build\Release\MidiRollProbe.exe"
$harness = Join-Path $root "build\Release\MidiRollHarness.exe"

Push-Location $root
try {
	if (-not (Test-Path $dll)) { throw "DLL belum terpasang: $dll - jalankan .\build.ps1 dulu" }
	if (-not (Test-Path $probe)) { throw "Probe belum dibuild: $probe" }
	if (-not (Test-Path $harness)) { throw "Harness belum dibuild: $harness" }

	Write-Host "`n=== TES 1: metadata ===" -ForegroundColor Cyan
	& $probe $dll
	$metadataOk = $LASTEXITCODE -eq 0

	Write-Host "`n=== TES 2: end-to-end ===" -ForegroundColor Cyan
	& $harness $dll --seconds $Seconds --port $Port
	$endToEndOk = $LASTEXITCODE -eq 0

	Write-Host "`n=== RINGKASAN ===" -ForegroundColor Cyan
	Write-Host ("metadata   : {0}" -f $(if ($metadataOk) { "LULUS" } else { "GAGAL" })) -ForegroundColor $(if ($metadataOk) { "Green" } else { "Red" })
	Write-Host ("end-to-end : {0}" -f $(if ($endToEndOk) { "LULUS" } else { "GAGAL" })) -ForegroundColor $(if ($endToEndOk) { "Green" } else { "Red" })

	if (-not ($metadataOk -and $endToEndOk)) { exit 1 }
}
finally {
	Pop-Location
}
