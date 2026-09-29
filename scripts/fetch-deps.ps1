# ---------------------------------------------------------------------------
# Download the WebView2 SDK into third_party/.
#
# The SDK is a NuGet package containing WebView2.h, the loader library and the
# runtime redistributable descriptors. It is fetched rather than committed:
# it is about 9 MB, it is not ours to license, and a fresh clone should not
# carry it.
#
# Pinned version. Bumping it is a deliberate change with a review, not
# something that happens because a build ran on a different day. The loader is
# linked statically, so a version bump changes the binary and wants a test run
# against the current Evergreen runtime.
#
# Usage:
#   powershell -ExecutionPolicy Bypass -File scripts/fetch-deps.ps1
#   powershell -ExecutionPolicy Bypass -File scripts/fetch-deps.ps1 -Force
# ---------------------------------------------------------------------------

[CmdletBinding()]
param(
    # Re-download even when the SDK is already present.
    [switch] $Force
)

$ErrorActionPreference = 'Stop'

$SdkVersion = '1.0.4129.50'
$PackageId  = 'microsoft.web.webview2'

$repoRoot   = Split-Path -Parent $PSScriptRoot
$ThirdParty = Join-Path $repoRoot 'third_party'
$TargetDir  = Join-Path $ThirdParty "WebView2.$SdkVersion"
$MarkerFile = Join-Path $TargetDir '.fetched'

# The header is the thing every translation unit needs. If it is there, the
# SDK is there; checking for the whole directory would re-download when only a
# file that nothing uses was removed.
$HeaderPath = Join-Path $TargetDir 'build\native\include\WebView2.h'

if ((Test-Path $HeaderPath) -and -not $Force) {
    Write-Host "WebView2 SDK $SdkVersion already present:"
    Write-Host "  $TargetDir"
    Write-Host "Use -Force to re-download."
    exit 0
}

New-Item -ItemType Directory -Force -Path $ThirdParty | Out-Null

$url = "https://api.nuget.org/v3-flatcontainer/$PackageId/$SdkVersion/$PackageId.$SdkVersion.nupkg"
$nupkg = Join-Path $ThirdParty "webview2-$SdkVersion.nupkg"

Write-Host "Downloading WebView2 SDK $SdkVersion"
Write-Host "  $url"

$ProgressPreference = 'SilentlyContinue'
Invoke-WebRequest -Uri $url -OutFile $nupkg -UseBasicParsing

# A nupkg is a zip. Verify before extracting rather than after, so a truncated
# download fails with a clear message instead of a confusing CMake error three
# steps later.
Add-Type -AssemblyName System.IO.Compression.FileSystem

try {
    $zip = [System.IO.Compression.ZipFile]::OpenRead($nupkg)
} catch {
    Remove-Item $nupkg -Force -ErrorAction SilentlyContinue
    throw "Downloaded package is not a readable zip archive. The download was probably truncated; re-run the script."
}

$hasHeader = $false
foreach ($entry in $zip.Entries) {
    if ($entry.FullName -eq 'build/native/include/WebView2.h') { $hasHeader = $true; break }
}
$zip.Dispose()

if (-not $hasHeader) {
    Remove-Item $nupkg -Force -ErrorAction SilentlyContinue
    throw "Package does not contain build/native/include/WebView2.h. The pinned version may have been unlisted."
}

if (Test-Path $TargetDir) { Remove-Item $TargetDir -Recurse -Force }
New-Item -ItemType Directory -Force -Path $TargetDir | Out-Null

[System.IO.Compression.ZipFile]::ExtractToDirectory($nupkg, $TargetDir)
Remove-Item $nupkg -Force

if (-not (Test-Path $HeaderPath)) {
    throw "Extraction finished but $HeaderPath is missing."
}

# Record what was fetched so a later mismatch is diagnosable without guessing.
@(
    "package=$PackageId"
    "version=$SdkVersion"
    "source=$url"
    "fetched=$(Get-Date -Format 'yyyy-MM-ddTHH:mm:ssK')"
) | Set-Content -Path $MarkerFile -Encoding UTF8

Write-Host ""
Write-Host "WebView2 SDK $SdkVersion installed:"
Write-Host "  $TargetDir"
Write-Host ""
Write-Host "The SDK lives in third_party/, which is git-ignored. Every worktree"
Write-Host "needs its own copy; run this script once per worktree, or copy the"
Write-Host "directory across."
