# format.ps1 - 代码格式化脚本
# 使用 clang-format 格式化所有源代码文件

param(
    [switch]$Check,  # 只检查不修改
    [switch]$Fix     # 修复格式问题
)

$ErrorActionPreference = "Stop"

# 检查 clang-format 是否安装
$clangFormat = Get-Command clang-format -ErrorAction SilentlyContinue
if (-not $clangFormat) {
    Write-Host "Error: clang-format not found!" -ForegroundColor Red
    Write-Host "Please install clang-format:" -ForegroundColor Yellow
    Write-Host "  - Windows: choco install llvm" -ForegroundColor Yellow
    Write-Host "  - Or download from: https://llvm.org/builds/" -ForegroundColor Yellow
    exit 1
}

Write-Host "Using: $($clangFormat.Source)" -ForegroundColor Cyan

# 要格式化的目录
$directories = @(
    "libco/src",
    "libco/include",
    "libcoxx/src",
    "libcoxx/include",
    "tests",
    "examples"
)

# 文件扩展名
$extensions = @("*.c", "*.h", "*.cpp", "*.hpp", "*.cc", "*.hh")

$totalFiles = 0
$formattedFiles = 0
$badFormatFiles = @()

foreach ($dir in $directories) {
    $dirPath = Join-Path $PSScriptRoot $dir
    
    if (-not (Test-Path $dirPath)) {
        Write-Host "Skip: $dir (not exists)" -ForegroundColor Yellow
        continue
    }
    
    Write-Host "`nProcessing: $dir" -ForegroundColor Green
    
    foreach ($ext in $extensions) {
        $files = Get-ChildItem -Path $dirPath -Filter $ext -Recurse -File
        
        foreach ($file in $files) {
            $totalFiles++
            $relativePath = $file.FullName.Replace("$PSScriptRoot\", "")
            
            if ($Check) {
                # 只检查格式
                $output = & clang-format --dry-run -Werror $file.FullName 2>&1
                if ($LASTEXITCODE -ne 0) {
                    $formattedFiles++
                    $badFormatFiles += $relativePath
                    Write-Host "  ✗ $relativePath" -ForegroundColor Red
                } else {
                    Write-Host "  ✓ $relativePath" -ForegroundColor Green
                }
            } else {
                # 格式化文件
                & clang-format -i $file.FullName
                if ($LASTEXITCODE -eq 0) {
                    $formattedFiles++
                    Write-Host "  ✓ $relativePath" -ForegroundColor Cyan
                } else {
                    Write-Host "  ✗ $relativePath (failed)" -ForegroundColor Red
                }
            }
        }
    }
}

# 输出统计
Write-Host "`n========================================" -ForegroundColor Cyan
if ($Check) {
    Write-Host "Format Check Complete" -ForegroundColor Cyan
    Write-Host "Total files: $totalFiles" -ForegroundColor White
    Write-Host "Bad format: $formattedFiles" -ForegroundColor $(if ($formattedFiles -eq 0) { "Green" } else { "Red" })
    
    if ($badFormatFiles.Count -gt 0) {
        Write-Host "`nFiles need formatting:" -ForegroundColor Yellow
        foreach ($file in $badFormatFiles) {
            Write-Host "  - $file" -ForegroundColor Gray
        }
        Write-Host "`nRun './tools/format.ps1' to fix" -ForegroundColor Yellow
        exit 1
    } else {
        Write-Host "All files are properly formatted! ✓" -ForegroundColor Green
    }
} else {
    Write-Host "Format Complete" -ForegroundColor Cyan
    Write-Host "Total files: $totalFiles" -ForegroundColor White
    Write-Host "Formatted: $formattedFiles" -ForegroundColor Green
}

exit 0
