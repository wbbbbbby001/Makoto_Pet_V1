# ============================================================
#  makoto-pet 固件一键编译脚本
#
#  为什么不在当前目录直接 idf.py build？
#  ESP-IDF 5.3 的 kconfgen / objdump 在中文路径下会按 GBK 解码，
#  遇到「立创实战派」就会把路径解成乱码，链接阶段找不到 .a 文件。
#  所以这里把源码镜像到纯英文目录 C:\makoto-pet-build 再编译，
#  编完把 bin 拷回 firmware\build_out\。
#
#  用法：
#    powershell -ExecutionPolicy Bypass -File build.ps1
#    powershell -ExecutionPolicy Bypass -File build.ps1 -Flash -Port COM5
#
#   ESP-IDF 路径优先取环境变量 IDF_PATH / IDF_TOOLS_PATH / IDF_PYTHON_ENV_PATH，
#   没有设置时退回作者机器的默认路径；也可以用参数显式指定：
#    -IdfPath  -IdfTools  -PyEnv  -Work
# ============================================================
param(
    [switch]$Flash,
    [string]$Port = "COM3",
    [string]$IdfPath = $env:IDF_PATH,
    [string]$IdfTools = $env:IDF_TOOLS_PATH,
    [string]$PyEnv = $env:IDF_PYTHON_ENV_PATH,
    [string]$Work = "C:\makoto-pet-build"
)

$ErrorActionPreference = "Stop"

# 从 Git Bash / MSYS 环境调用 PowerShell 时会继承 MSYSTEM，
# ESP-IDF 的 export.ps1 会因此报 "MSys/Mingw is not supported"。
# 这里清掉，保证从任何终端都能正常编译。
Remove-Item Env:MSYSTEM -ErrorAction SilentlyContinue
Remove-Item Env:MINGW_PREFIX -ErrorAction SilentlyContinue

$Src  = $PSScriptRoot
$Out  = Join-Path $Src "build_out"

# 没设环境变量时，使用作者机器的默认路径（本机可直接用）
if (-not $IdfTools) { $IdfTools = "D:\Espressif\Espressif" }
if (-not $IdfPath)  { $IdfPath  = "D:\Espressif\Espressif\frameworks\esp-idf-v5.3.1" }
if (-not $PyEnv)    { $PyEnv    = "D:\Espressif\Espressif\python_env\idf5.3_py3.11_env" }

if (-not (Test-Path $IdfPath)) {
    throw "找不到 ESP-IDF：$IdfPath`n请安装 ESP-IDF 5.3.x，并设置 IDF_PATH / IDF_TOOLS_PATH / IDF_PYTHON_ENV_PATH，或用 -IdfPath/-IdfTools/-PyEnv 参数指定。"
}

# ---------- 镜像源码 ----------
Write-Host "[1/3] 同步源码到 $Work ..." -ForegroundColor Cyan
if (-not (Test-Path $Work)) { New-Item -ItemType Directory -Path $Work | Out-Null }
& "C:\Windows\System32\robocopy.exe" $Src $Work /MIR /NFL /NDL /NJH /NJS /NP `
    /XD build managed_components build_out `
    /XF sdkconfig sdkconfig.old | Out-Null
if ($LASTEXITCODE -ge 8) { throw "robocopy 失败：$LASTEXITCODE" }

# ---------- 编译 ----------
Write-Host "[2/3] 准备 ESP-IDF 环境并编译 ..." -ForegroundColor Cyan
$env:IDF_TOOLS_PATH = $IdfTools
$env:IDF_PATH = $IdfPath
$env:IDF_PYTHON_ENV_PATH = $PyEnv
$env:PYTHONUTF8 = "1"
$env:PYTHONIOENCODING = "utf-8"
$oldEap = $ErrorActionPreference
$ErrorActionPreference = "Continue"
. (Join-Path $IdfPath "export.ps1") *> $null
$ErrorActionPreference = $oldEap
if (-not (Get-Command idf.py -ErrorAction SilentlyContinue)) {
    throw "ESP-IDF 环境初始化失败（idf.py 不在 PATH）"
}

Push-Location $Work
try {
    if (-not (Test-Path (Join-Path $Work "build"))) {
        idf.py set-target esp32s3
        if ($LASTEXITCODE -ne 0) { throw "set-target 失败" }
    }
    idf.py build
    if ($LASTEXITCODE -ne 0) { throw "编译失败" }
    idf.py merge-bin | Out-Null
} finally {
    Pop-Location
}

# ---------- 收集产物 ----------
Write-Host "[3/3] 拷贝生成物到 $Out ..." -ForegroundColor Cyan
New-Item -ItemType Directory -Force -Path $Out | Out-Null
$files = @(
    "build\makoto_pet.bin",
    "build\bootloader\bootloader.bin",
    "build\partition_table\partition-table.bin",
    "build\merged-binary.bin"
)
foreach ($f in $files) {
    $p = Join-Path $Work $f
    if (Test-Path $p) { Copy-Item $p $Out -Force }
}
Get-ChildItem $Out | Select-Object Name, Length | Format-Table -AutoSize

if ($Flash) {
    Write-Host "烧录到 $Port ..." -ForegroundColor Yellow
    Push-Location $Work
    try {
        idf.py -p $Port flash
    } finally {
        Pop-Location
    }
    Write-Host "烧录完成。看日志： idf.py -p $Port monitor" -ForegroundColor Green
}
