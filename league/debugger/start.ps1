# Starts the api and the ui together. Driver must already be loaded.
$root = Split-Path -Parent $PSScriptRoot

Start-Process python -ArgumentList (Join-Path $PSScriptRoot 'server.py') -WorkingDirectory $root
Start-Sleep -Seconds 2

Push-Location (Join-Path $PSScriptRoot 'ui')
try {
    if (-not (Test-Path 'node_modules')) { npm install --no-fund --no-audit }
    npm run dev
}
finally {
    Pop-Location
}
