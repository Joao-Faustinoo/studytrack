# build.ps1 - compila o studytrack.
#
# Por que este script existe: o vswhere.exe desta maquina esta ausente e o
# registro do instalador do VS 2019 esta quebrado, entao o CMake NAO acha o
# MSVC pelo gerador "Visual Studio 16 2019". A saida e usar o gerador Ninja
# de dentro de um shell vcvars64, onde basta o cl estar no PATH.
#
#   .\build.ps1              compila
#   .\build.ps1 -Test        compila e roda os testes
#   .\build.ps1 -Clean       apaga build/ antes
#   .\build.ps1 -Run         compila e sobe o servidor

param(
    [switch]$Test,
    [switch]$Clean,
    [switch]$Run,
    [string]$Config = "RelWithDebInfo"
)

$ErrorActionPreference = "Stop"
$raiz = $PSScriptRoot

$vcvars = "C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
$cmake  = "D:\Tools\CMake\bin\cmake.exe"
$ninja  = "D:\Tools\Ninja\ninja.exe"

foreach ($p in @($vcvars, $cmake, $ninja)) {
    if (-not (Test-Path $p)) { throw "Nao encontrado: $p" }
}

if ($Clean -and (Test-Path "$raiz\build")) {
    Write-Host "limpando build/" -ForegroundColor DarkGray
    Remove-Item "$raiz\build" -Recurse -Force
}

# vcvars64 chama vswhere internamente e reclama que ele nao existe; a mensagem
# e inofensiva, as variaveis de ambiente que importam sao definidas pelo .bat.
$alvos = "studytrack"
if ($Test) { $alvos = "studytrack st_tests" }

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
    & "$raiz\build\studytrack.exe" serve
}
