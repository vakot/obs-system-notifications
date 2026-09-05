[CmdletBinding()]
param(
    [Parameter()]
    [ValidateRange(1, 64)]
    [int] $Parallel = 8,

    [Parameter()]
    [ValidateNotNullOrEmpty()]
    [string] $Version
)

$ErrorActionPreference = 'Stop'
$repositoryRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$buildspecPath = Join-Path $repositoryRoot 'buildspec.json'
$buildspec = Get-Content -LiteralPath $buildspecPath -Raw | ConvertFrom-Json
$projectName = [string] $buildspec.name
$projectVersion = [string] $buildspec.version

if ($projectVersion -notmatch '^\d+\.\d+\.\d+$') {
    throw "buildspec.json version is not a supported release version: $projectVersion"
}

if ($Version -and $Version -ne $projectVersion) {
    throw "Requested version '$Version' does not match buildspec.json version '$projectVersion'."
}

$tag = "v$projectVersion"
$vsDevCmd = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat'
$cmake = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
$buildDirectory = Join-Path $repositoryRoot 'build_x64'
$artifactsDirectory = Join-Path $repositoryRoot 'artifacts'
$stagingDirectory = Join-Path $artifactsDirectory "stage-$tag"
$artifactBaseName = "$projectName-$tag-windows-x64"
$artifactPath = Join-Path $artifactsDirectory "$artifactBaseName.zip"
$checksumPath = Join-Path $artifactsDirectory "$artifactBaseName.zip.sha256"

function Invoke-CheckedNative {
    param(
        [Parameter(Mandatory)] [string] $FilePath,
        [Parameter(Mandatory)] [string[]] $Arguments,
        [Parameter(Mandatory)] [string] $Description
    )

    Write-Host "$Description..."
    & $FilePath @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$Description failed with exit code $LASTEXITCODE."
    }
}

function Invoke-CMakeThroughVsDev {
    param(
        [Parameter(Mandatory)] [string] $Arguments,
        [Parameter(Mandatory)] [string] $Description
    )

    if (-not (Test-Path -LiteralPath $vsDevCmd -PathType Leaf)) {
        throw "Visual Studio developer environment was not found: $vsDevCmd"
    }

    if (-not (Test-Path -LiteralPath $cmake -PathType Leaf)) {
        throw "CMake executable was not found: $cmake"
    }

    Write-Host "$Description..."
    $command = "call `"$vsDevCmd`" -arch=x64 -host_arch=x64 >NUL && `"$cmake`" $Arguments"
    & cmd /d /c $command
    if ($LASTEXITCODE -ne 0) {
        throw "$Description failed with exit code $LASTEXITCODE."
    }
}

if ((git branch --show-current) -ne 'master') {
    throw 'Release must be run from the local master branch.'
}

$workingTree = @(git status --porcelain)
if ($LASTEXITCODE -ne 0) { throw 'Unable to inspect the Git working tree.' }
if ($workingTree.Count -gt 0) {
    throw 'Release requires a clean working tree.'
}

Invoke-CheckedNative git @('fetch', 'origin', 'master', '--tags') 'Fetching latest master and tags'

$head = (git rev-parse HEAD).Trim()
$originMaster = (git rev-parse origin/master).Trim()
if ($head -ne $originMaster) {
    throw 'Local master is not aligned with origin/master. Run git pull --ff-only and retry.'
}

$localTag = @(git tag --list $tag)
if ($localTag.Count -gt 0) {
    throw "Tag already exists locally: $tag"
}

$remoteTag = @(git ls-remote --exit-code --tags origin "refs/tags/$tag" 2>$null)
if ($LASTEXITCODE -notin @(0, 2)) {
    throw "Unable to check whether tag exists on origin: $tag"
}
if ($LASTEXITCODE -eq 0 -and $remoteTag.Count -gt 0) {
    throw "Tag already exists on origin: $tag"
}

New-Item -ItemType Directory -Path $artifactsDirectory -Force | Out-Null
if (Test-Path -LiteralPath $stagingDirectory) {
    Remove-Item -LiteralPath $stagingDirectory -Recurse -Force
}
New-Item -ItemType Directory -Path $stagingDirectory -Force | Out-Null

$releaseCreated = $false
try {
    Invoke-CheckedNative git @('tag', '-a', $tag, '-m', "Release $tag") "Creating annotated tag $tag"
    Invoke-CheckedNative git @('push', 'origin', $tag) "Pushing tag $tag"

    Invoke-CheckedNative gh @('release', 'create', $tag, '--verify-tag', '--generate-notes', '--draft') "Creating draft GitHub release $tag"
    $releaseCreated = $true

    if (-not (Test-Path -LiteralPath (Join-Path $buildDirectory 'CMakeCache.txt') -PathType Leaf)) {
        Invoke-CMakeThroughVsDev '--preset windows-x64' 'Configuring the release build'
    }

    Invoke-CMakeThroughVsDev "--build `"$buildDirectory`" --config Release --parallel $Parallel" 'Building the Release configuration'
    Invoke-CMakeThroughVsDev "--install `"$buildDirectory`" --config Release --prefix `"$stagingDirectory`"" 'Installing the release artifact'

    $pluginPath = Join-Path $stagingDirectory 'obs-plugins\64bit\obs-system-notifications.dll'
    if (-not (Test-Path -LiteralPath $pluginPath -PathType Leaf)) {
        throw "Release artifact is missing the plugin DLL: $pluginPath"
    }

    Get-ChildItem -LiteralPath $stagingDirectory -Filter '*.pdb' -File -Recurse |
        Remove-Item -Force

    if (Test-Path -LiteralPath $artifactPath) { Remove-Item -LiteralPath $artifactPath -Force }
    if (Test-Path -LiteralPath $checksumPath) { Remove-Item -LiteralPath $checksumPath -Force }
    Compress-Archive -Path (Join-Path $stagingDirectory '*') -DestinationPath $artifactPath -CompressionLevel Optimal

    $hash = (Get-FileHash -LiteralPath $artifactPath -Algorithm SHA256).Hash.ToLowerInvariant()
    "$hash  $([System.IO.Path]::GetFileName($artifactPath))" |
        Set-Content -LiteralPath $checksumPath -Encoding ascii -NoNewline

    Invoke-CheckedNative gh @('release', 'upload', $tag, $artifactPath, $checksumPath, '--clobber') "Uploading $tag artifacts"
    Invoke-CheckedNative gh @('release', 'edit', $tag, '--draft=false') "Publishing GitHub release $tag"

    Write-Host "Release published: $tag"
    Write-Host "Artifact: $artifactPath"
    Write-Host "Checksum: $checksumPath"
}
finally {
    if (Test-Path -LiteralPath $stagingDirectory) {
        Remove-Item -LiteralPath $stagingDirectory -Recurse -Force
    }

    if (-not $releaseCreated) {
        Write-Warning "The GitHub release was not created. The tag may still exist if tag creation succeeded."
    }
}
