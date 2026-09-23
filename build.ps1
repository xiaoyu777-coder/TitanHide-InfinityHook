[CmdletBinding()]
param(
    [ValidateSet('Release')]
    [string] $Configuration = 'Release',

    [string] $WindowsTargetPlatformVersion = '10.0.28000.0',

    # WDK 10.0.28000.0 dropped KMDF 1.9 validation for Windows 7. Windows10
    # selects the supported KMDF toolchain while leaving project files intact.
    [string] $DriverTargetVersion = 'Windows10',

    [switch] $Clean
)

$ErrorActionPreference = 'Stop'

$root = (Resolve-Path -LiteralPath $PSScriptRoot).Path
$solution = Join-Path $root 'TitanHide.sln'
$x64Output = Join-Path $root 'x64\Release'
$temporaryRoot = Join-Path $root ('.build\TitanHide-build-' + [Guid]::NewGuid().ToString('N'))
$x32Staging = Join-Path $temporaryRoot 'Release'

if (-not (Test-Path -LiteralPath $solution -PathType Leaf)) {
    throw "Solution not found: $solution"
}

function Resolve-MSBuild {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path -LiteralPath $vswhere -PathType Leaf) {
        $installationPath = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -property installationPath
        if ($LASTEXITCODE -eq 0 -and $installationPath) {
            $candidates = @(
                (Join-Path $installationPath 'MSBuild\Current\Bin\amd64\MSBuild.exe'),
                (Join-Path $installationPath 'MSBuild\Current\Bin\MSBuild.exe')
            )
            foreach ($candidate in $candidates) {
                if (Test-Path -LiteralPath $candidate -PathType Leaf) {
                    return $candidate
                }
            }
        }
    }

    $command = Get-Command msbuild.exe -ErrorAction SilentlyContinue
    if ($command) {
        return $command.Source
    }

    throw 'MSBuild.exe was not found. Install Visual Studio with the C++ build tools or add MSBuild to PATH.'
}

function Invoke-MSBuild {
    param(
        [Parameter(Mandatory = $true)] [string] $Project,
        [Parameter(Mandatory = $true)] [string] $Platform,
        [string] $OutputDirectory,
        [string] $IntermediateDirectory
    )

    $arguments = @(
        $Project,
        '/t:Build',
        '/m',
        '/nologo',
        '/v:minimal',
        "/p:Configuration=$Configuration",
        "/p:Platform=$Platform",
        "/p:WindowsTargetPlatformVersion=$WindowsTargetPlatformVersion",
        "/p:TargetVersion=$DriverTargetVersion"
    )

    if ($OutputDirectory) {
        $arguments += "/p:OutDir=$OutputDirectory\"
    }
    if ($IntermediateDirectory) {
        $arguments += "/p:IntDir=$IntermediateDirectory\"
    }

    Write-Host "Building $Platform ${Configuration}: $Project"
    & $msbuild @arguments
    if ($LASTEXITCODE -ne 0) {
        throw "MSBuild failed for $Project ($Platform). Exit code: $LASTEXITCODE"
    }
}

$msbuild = Resolve-MSBuild
Write-Host "Using MSBuild: $msbuild"
Write-Host "Using Windows SDK: $WindowsTargetPlatformVersion"

if ($Clean) {
    foreach ($directory in @($x64Output)) {
        if (Test-Path -LiteralPath $directory) {
            Remove-Item -LiteralPath $directory -Recurse -Force
        }
    }
}

New-Item -ItemType Directory -Path $x64Output -Force | Out-Null

try {
    New-Item -ItemType Directory -Path $x32Staging -Force | Out-Null

    # Build every Release|x64 project. Project files retain control of the
    # x64\Release and x64\Release\plugins layout.
    Invoke-MSBuild -Project $solution -Platform 'x64'

    # The solution's Win32 configuration contains other projects. Build only
    # the x32dbg plugin and stage it outside the normal x32 Release directory.
    $x32PluginProject = Join-Path $root 'TitanHide_x64dbg\TitanHide_x64dbg.vcxproj'
    $x32Intermediate = Join-Path $temporaryRoot 'obj'
    Invoke-MSBuild -Project $x32PluginProject -Platform 'Win32' -OutputDirectory (Join-Path $x32Staging 'plugins') -IntermediateDirectory $x32Intermediate

    $x32Plugin = Join-Path $x32Staging 'plugins\TitanHide.dp32'
    if (-not (Test-Path -LiteralPath $x32Plugin -PathType Leaf)) {
        throw "The x32dbg plugin was not produced: $x32Plugin"
    }

    $pluginOutput = Join-Path $x64Output 'plugins'
    New-Item -ItemType Directory -Path $pluginOutput -Force | Out-Null
    Copy-Item -LiteralPath $x32Plugin -Destination (Join-Path $pluginOutput 'TitanHide.dp32') -Force

    $requiredOutputs = @(
        (Join-Path $x64Output 'TitanHide.sys'),
        (Join-Path $x64Output 'TitanHideGUI.exe'),
        (Join-Path $x64Output 'TitanHideCLI.exe'),
        (Join-Path $x64Output 'TitanHideTest.exe'),
        (Join-Path $pluginOutput 'TitanHide.dp64'),
        (Join-Path $pluginOutput 'TitanHideTE.dll'),
        (Join-Path $pluginOutput 'TitanHide.dp32')
    )
    foreach ($output in $requiredOutputs) {
        if (-not (Test-Path -LiteralPath $output -PathType Leaf)) {
            throw "Expected build output was not found: $output"
        }
    }
}
finally {
    if (Test-Path -LiteralPath $temporaryRoot) {
        Remove-Item -LiteralPath $temporaryRoot -Recurse -Force -ErrorAction SilentlyContinue
    }
}

Write-Host "Build completed. Output: $x64Output"
