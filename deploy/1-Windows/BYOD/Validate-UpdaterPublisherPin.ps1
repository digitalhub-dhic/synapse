param(
    [Parameter(Mandatory = $true)]
    [string]$UpdaterPath
)

$ErrorActionPreference = 'Stop'
if (-not (Test-Path -LiteralPath $UpdaterPath -PathType Leaf)) {
    throw "Updater script is missing: $UpdaterPath"
}

$updaterSource = Get-Content -LiteralPath $UpdaterPath -Raw
$pinPattern = '(?m)^\s*\$trustedSigner\s*=\s*''([0-9A-Fa-f]{40})''\s*(?:#.*)?$'
if (-not [regex]::IsMatch($updaterSource, $pinPattern)) {
    throw 'The updater publisher thumbprint is missing or invalid. Configure the 40-character publisher pin before enrolling BYOD devices.'
}
