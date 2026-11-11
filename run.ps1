$ErrorActionPreference = 'Stop'
$config = Join-Path $PSScriptRoot '.env'
if (-not (Test-Path -LiteralPath $config)) {
    throw 'Create .env from .env.example and configure both Supabase projects first.'
}
$names = 'SUPABASE_URL', 'SUPABASE_SECRET_KEY', 'IAM_SUPABASE_URL', 'IAM_SUPABASE_PUBLISHABLE_KEY'
foreach ($line in Get-Content -LiteralPath $config) {
    if ($line -match '^([A-Z_]+)=(.+)$' -and $names -contains $Matches[1]) {
        [Environment]::SetEnvironmentVariable($Matches[1], $Matches[2].Trim(), 'Process')
    }
}
$missing = $names | Where-Object { -not [Environment]::GetEnvironmentVariable($_, 'Process') }
if ($missing) { throw "Missing in .env: $($missing -join ', ')" }
$executable = Join-Path $PSScriptRoot 'out\build\x64-debug-vcpkg\digital-closet-service.exe'
if (-not (Test-Path -LiteralPath $executable)) {
    throw 'Build x64 Debug first. See README.md.'
}
& $executable
exit $LASTEXITCODE
