$ErrorActionPreference = 'Stop'
$config = Join-Path $PSScriptRoot '..\.env'
if (-not (Test-Path -LiteralPath $config)) { throw 'Create ..\.env from ..\.env.example first.' }
$names = 'IAM_SUPABASE_URL', 'IAM_SUPABASE_PUBLISHABLE_KEY', 'CF_API_TOKEN', 'CF_ACCOUNT_ID', 'DC_API_URL'
foreach ($line in Get-Content -LiteralPath $config) {
    if ($line -match '^([A-Z_]+)=(.+)$' -and $names -contains $Matches[1]) {
        [Environment]::SetEnvironmentVariable($Matches[1], $Matches[2].Trim(), 'Process')
    }
}
$python = Join-Path $PSScriptRoot '.venv\Scripts\python.exe'
if (-not (Test-Path -LiteralPath $python)) {
    throw 'Create the venv first: python -m venv .venv; .venv\Scripts\pip install -r requirements.txt'
}
Set-Location $PSScriptRoot
& $python -m uvicorn app:app --host 0.0.0.0 --port 8082
exit $LASTEXITCODE
