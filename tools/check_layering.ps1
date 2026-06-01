param(
    [switch]$VerboseOutput
)

$ErrorActionPreference = "Stop"

if (-not (Get-Command rg -ErrorAction SilentlyContinue)) {
    Write-Error "ripgrep (rg) is required for layering checks."
}

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoRoot = Resolve-Path (Join-Path $scriptDir "..")
$failed = $false

function Invoke-LayerScan {
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][string[]]$Arguments
    )

    Write-Host "== $Name"
    $output = & rg @Arguments 2>&1
    $code = $LASTEXITCODE

    if ($code -eq 0) {
        Write-Host "FAILED: layering violation found." -ForegroundColor Red
        $output | ForEach-Object { Write-Host $_ }
        return $false
    }

    if ($code -eq 1) {
        if ($VerboseOutput) {
            Write-Host "OK: no matches." -ForegroundColor Green
        }
        return $true
    }

    Write-Host "ERROR: rg exited with code $code." -ForegroundColor Red
    $output | ForEach-Object { Write-Host $_ }
    return $false
}

Push-Location $repoRoot
try {
    $checks = @(
        @{
            Name = "app must not include CGAL or Easy3D algorithm headers"
            Arguments = @(
                "-n",
                "--color", "never",
                '#include\s+[<"](easy3d/algo|easy3d/kdtree|CGAL|algorithms/cgal)',
                "src/app"
            )
        },
        @{
            Name = "app CMake target must not expose or link CGAL runners"
            Arguments = @(
                "-n",
                "--color", "never",
                "claw3d_cgal_algorithms|src/algorithms/cgal/include",
                "src/app/CMakeLists.txt"
            )
        },
        @{
            Name = "app must not include or own concrete runner types"
            Arguments = @(
                "-n",
                "--color", "never",
                '#include\s+".*runner\.h"|#include\s+".*_runner\.h"|std::shared_ptr<.*Runner>|\b[A-Za-z0-9]+Runner\b',
                "src/app"
            )
        },
        @{
            Name = "public service headers must not expose concrete runners"
            Arguments = @(
                "-n",
                "--color", "never",
                "runner\.h|_runner\.h|std::shared_ptr<.*Runner>",
                "src/services",
                "-g", "*.h"
            )
        },
        @{
            Name = "backend layers must not include app or UI headers"
            Arguments = @(
                "-n",
                "--color", "never",
                '#include\s+[<"](imgui|GLFW|app/)',
                "src/services",
                "src/algorithms",
                "src/io",
                "src/common"
            )
        }
    )

    foreach ($check in $checks) {
        if (-not (Invoke-LayerScan -Name $check.Name -Arguments $check.Arguments)) {
            $failed = $true
        }
    }
} finally {
    Pop-Location
}

if ($failed) {
    Write-Host "Layering check failed." -ForegroundColor Red
    exit 1
}

Write-Host "Layering check passed." -ForegroundColor Green
