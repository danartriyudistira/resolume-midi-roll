# Build Midi Roll and install it into Resolume's user plugin folder.
#
#   .\build.ps1              Release build + install
#   .\build.ps1 -Config Debug
#   .\build.ps1 -NoInstall   build only, leave the Resolume folder alone

param(
	[ValidateSet("Debug", "Release")]
	[string]$Config = "Release",
	[switch]$NoInstall,
	[switch]$Web
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $MyInvocation.MyCommand.Path

Push-Location $root
try {
	if (-not (Test-Path "vendor/ffgl/CMakeLists.txt")) {
		throw "FFGL SDK missing at vendor/ffgl. Run: git clone https://github.com/resolume/ffgl.git vendor/ffgl"
	}

	if ($Web) {
		Write-Host "==> web build" -ForegroundColor Cyan
		Push-Location "web"
		try {
			& npm run build
			if ($LASTEXITCODE -ne 0) { throw "Web build failed" }
		}
		finally {
			Pop-Location
		}

		# Deploy the built editor into gui/ so the plugin serves it at 127.0.0.1:47823.
		Remove-Item "gui/index.html" -Force -ErrorAction SilentlyContinue
		Remove-Item "gui/assets" -Recurse -Force -ErrorAction SilentlyContinue
		Copy-Item "web/dist/index.html" "gui/index.html" -Force
		Copy-Item "web/dist/assets" "gui/assets" -Recurse -Force
	}

	$cmakeArgs = @("-S", ".", "-B", "build", "-G", "Visual Studio 17 2022", "-A", "x64")
	if ($NoInstall) {
		$cmakeArgs += "-DMIDIROLL_SKIP_INSTALL=ON"
	}
	else {
		# Resolume reads plugins from the shell's Documents folder, which is often
		# redirected to OneDrive. Ask Windows rather than assuming a path.
		$documents = [Environment]::GetFolderPath("MyDocuments")
		$installDir = Join-Path $documents "Resolume Arena\Extra Effects"
		$cmakeArgs += "-DMIDIROLL_INSTALL_DIR=$installDir"
	}

	Write-Host "==> configure" -ForegroundColor Cyan
	& cmake @cmakeArgs
	if ($LASTEXITCODE -ne 0) { throw "CMake configure failed" }

	Write-Host "==> build ($Config)" -ForegroundColor Cyan
	& cmake --build build --config $Config
	if ($LASTEXITCODE -ne 0) { throw "Build failed" }

	$dll = Join-Path $root "build/$Config/MidiRoll.dll"
	if (-not (Test-Path $dll)) { throw "Expected output missing: $dll" }

	Write-Host "==> ok: $dll" -ForegroundColor Green
	if (-not $NoInstall) {
		Write-Host "==> installed to $installDir" -ForegroundColor Green
	}
}
finally {
	Pop-Location
}
