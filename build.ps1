# NMCL 构建脚本
# 用法: .\build.ps1 [-QtPath "C:\Qt\6.10.1\mingw_64"] [-Clean] [-NoClean]

param(
    [string]$QtPath = $env:CMAKE_PREFIX_PATH,
    [string]$Config = "Release",
    [switch]$Clean,
    [switch]$NoClean
)

$ErrorActionPreference = "Stop"
$projectRoot = $PSScriptRoot
$buildDir = Join-Path $projectRoot "build"

# 查找 Qt 6
$qtSearchPaths = @(
    $QtPath,
    "C:\Qt\6.10.2\mingw_64",
    "C:\Qt\6.10.1\mingw_64",
    "C:\Qt\6.10.1\msvc2019_64",
    "C:\Qt\6.7.0\mingw_64",
    "C:\Qt\6.6.0\mingw_64",
    "D:\Qt\6.10.1\mingw_64"
)

$foundQt = $null
foreach ($p in $qtSearchPaths) {
    if ($p -and (Test-Path (Join-Path $p "lib\cmake\Qt6"))) {
        $foundQt = $p
        break
    }
}

if (-not $foundQt) {
    Write-Host "Error: Qt 6 not found. Install Qt 6 and run:" -ForegroundColor Red
    Write-Host '  .\build.ps1 -QtPath "C:\Qt\6.10.1\mingw_64"' -ForegroundColor Yellow
    exit 1
}

Write-Host "NMCL Build" -ForegroundColor Cyan
Write-Host "Qt: $foundQt" -ForegroundColor Green

# Qt Tools 加入 PATH
$qtRoot = Split-Path (Split-Path $foundQt -Parent) -Parent
$tools = @(
    "Tools\mingw1310_64\bin",
    "Tools\mingw1120_64\bin",
    "Tools\Ninja",
    "Tools\CMake_64\bin"
)
foreach ($t in $tools) {
    $p = Join-Path $qtRoot $t
    if (Test-Path $p) { $env:PATH = "$p;$env:PATH" }
}

# 选择生成器：mingw 用 Ninja，msvc 用 VS
$generator = "Ninja"
$extraArgs = @()
if ($foundQt -match "msvc") {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vswhere) {
        $vs = & $vswhere -latest -property installationPath -requires Microsoft.Component.MSBuild 2>$null
        if ($vs) {
            $generator = "Visual Studio 17 2022"
            $extraArgs = @("-A", "x64")
        }
    }
}

Write-Host "Generator: $generator" -ForegroundColor Gray

# 清理
if ($Clean -or (-not $NoClean -and (Test-Path $buildDir))) {
    Write-Host "Cleaning build..." -ForegroundColor Gray
    Remove-Item -Recurse -Force $buildDir -ErrorAction SilentlyContinue
    Start-Sleep -Milliseconds 500
}

New-Item -ItemType Directory -Path $buildDir -Force | Out-Null

Push-Location $buildDir
try {
    # 配置
    Write-Host "Configuring..." -ForegroundColor Cyan
    $cmakeArgs = @("..", "-G", $generator, "-DCMAKE_PREFIX_PATH=$foundQt", "-DCMAKE_BUILD_TYPE=$Config") + $extraArgs
    & cmake $cmakeArgs
    if ($LASTEXITCODE -ne 0) { throw "CMake configure failed" }

    # 编译
    Write-Host "Building..." -ForegroundColor Cyan
    & cmake --build . --config $Config
    if ($LASTEXITCODE -ne 0) { throw "Build failed" }

    $exe = Get-ChildItem -Path . -Recurse -Filter "NetMinecraftLauncher.exe" -ErrorAction SilentlyContinue | Select-Object -First 1
    Write-Host ""
    Write-Host "Build succeeded!" -ForegroundColor Green
    Write-Host "Output: $($exe.FullName)" -ForegroundColor Green
} catch {
    Write-Host $_.Exception.Message -ForegroundColor Red
    exit 1
} finally {
    Pop-Location
}
