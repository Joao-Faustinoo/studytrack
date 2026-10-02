# build.ps1 - compila o studytrack.
#
#   .\build.ps1              compila
#   .\build.ps1 -Test        compila e roda os testes
#   .\build.ps1 -Clean       apaga build/ antes
#   .\build.ps1 -Run         compila e sobe o servidor
#
# Por que o gerador e Ninja e nao "Visual Studio": na maquina onde isto nasceu
# o vswhere.exe estava ausente e o registro do instalador do VS2019 quebrado,
# entao o CMake nao achava o MSVC pelo gerador de IDE. Rodando dentro de um
# shell vcvars64 basta o cl estar no PATH, o que funciona em qualquer caso.
#
# Localizacao das ferramentas, nesta ordem: variavel de ambiente -> PATH ->
# locais conhecidos. Para forcar, defina STUDYTRACK_CMAKE, STUDYTRACK_NINJA ou
# STUDYTRACK_VCVARS.

param(
    [switch]$Test,
    [switch]$Clean,
    [switch]$Run,
    [string]$Config = "RelWithDebInfo",
    [int]$Port = 8080
)

$ErrorActionPreference = "Stop"
$raiz = $PSScriptRoot

function Primeiro-Existente([string[]]$caminhos) {
    foreach ($c in $caminhos) {
        if ($c -and (Test-Path $c)) { return (Resolve-Path $c).Path }
    }
    return $null
}

function Achar-Ferramenta([string]$envVar, [string]$noPath, [string[]]$conhecidos) {
    $v = [Environment]::GetEnvironmentVariable($envVar)
    if ($v -and (Test-Path $v)) { return $v }

    $cmd = Get-Command $noPath -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }

    return (Primeiro-Existente $conhecidos)
}

function Achar-Vcvars {
    if ($env:STUDYTRACK_VCVARS -and (Test-Path $env:STUDYTRACK_VCVARS)) { return $env:STUDYTRACK_VCVARS }

    # vswhere, quando existe, e a forma correta
    $vswhere = Primeiro-Existente @(
        "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe",
        "$env:ProgramFiles\Microsoft Visual Studio\Installer\vswhere.exe")
    if ($vswhere) {
        $inst = & $vswhere -products * -latest `
                   -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
                   -property installationPath 2>$null
        if ($inst) {
            $bat = Join-Path $inst "VC\Auxiliary\Build\vcvars64.bat"
            if (Test-Path $bat) { return $bat }
        }
    }

    # varredura manual: o vswhere pode estar ausente ou o registro corrompido
    $caminhos = @()
    foreach ($base in @($env:ProgramFiles, ${env:ProgramFiles(x86)})) {
        foreach ($ano in @("2022", "2019")) {
            foreach ($ed in @("BuildTools", "Community", "Professional", "Enterprise")) {
                $caminhos += "$base\Microsoft Visual Studio\$ano\$ed\VC\Auxiliary\Build\vcvars64.bat"
            }
        }
    }
    return (Primeiro-Existente $caminhos)
}

$cmake = Achar-Ferramenta "STUDYTRACK_CMAKE" "cmake" @(
    "D:\Tools\CMake\bin\cmake.exe",
    "$env:ProgramFiles\CMake\bin\cmake.exe",
    "${env:ProgramFiles(x86)}\CMake\bin\cmake.exe")

$ninja = Achar-Ferramenta "STUDYTRACK_NINJA" "ninja" @(
    "D:\Tools\Ninja\ninja.exe",
    "$env:ProgramFiles\Ninja\ninja.exe")

$vcvars = Achar-Vcvars

if (-not $cmake) {
    throw "CMake nao encontrado. Instale (https://cmake.org/download/), ponha no PATH, ou defina STUDYTRACK_CMAKE."
}
if (-not $ninja) {
    throw "Ninja nao encontrado. Baixe ninja-win.zip (https://github.com/ninja-build/ninja/releases), ponha no PATH, ou defina STUDYTRACK_NINJA."
}
if (-not $vcvars) {
    throw "Compilador MSVC nao encontrado. Instale o Visual Studio Build Tools com a carga 'Desenvolvimento para desktop com C++', ou defina STUDYTRACK_VCVARS apontando para o vcvars64.bat."
}

Write-Host "cmake  : $cmake"  -ForegroundColor DarkGray
Write-Host "ninja  : $ninja"  -ForegroundColor DarkGray
Write-Host "vcvars : $vcvars" -ForegroundColor DarkGray

if ($Clean -and (Test-Path "$raiz\build")) {
    Write-Host "limpando build/" -ForegroundColor DarkGray
    Remove-Item "$raiz\build" -Recurse -Force
}

$alvos = if ($Test) { "studytrack st_tests st_assets" } else { "studytrack st_assets" }

# vcvars64 pode reclamar de vswhere ausente; a mensagem e inofensiva, as
# variaveis que importam sao definidas pelo proprio .bat.
$script = @"
call "$vcvars" >nul 2>&1
"$cmake" -S "$raiz" -B "$raiz\build" -G Ninja -DCMAKE_MAKE_PROGRAM="$ninja" -DCMAKE_BUILD_TYPE=$Config || exit /b 1
"$cmake" --build "$raiz\build" --target $alvos || exit /b 1
"@

$bat = Join-Path $env:TEMP "st_build_$PID.bat"
$script | Out-File -FilePath $bat -Encoding ascii
try { cmd /c "`"$bat`"" } finally { Remove-Item $bat -Force -ErrorAction SilentlyContinue }
if ($LASTEXITCODE -ne 0) { throw "build falhou (exit $LASTEXITCODE)" }

Write-Host "`nbuild OK -> $raiz\build\studytrack.exe" -ForegroundColor Green

if ($Test) {
    Write-Host "`n--- testes ---" -ForegroundColor Cyan
    & "$raiz\build\st_tests.exe"
    if ($LASTEXITCODE -ne 0) { throw "testes falharam" }
}

if ($Run) {
    Write-Host "`n--- servidor ---" -ForegroundColor Cyan
    & "$raiz\build\studytrack.exe" serve $Port
}
