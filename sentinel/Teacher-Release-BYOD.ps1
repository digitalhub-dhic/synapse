# Compatibility wrapper: keep the teacher fleet console as the single source
# of truth for BYOD release state and target enrollment.
$ErrorActionPreference = 'Stop'
$manager = 'C:\SynapseAISentinel\byod_fleet_manager.py'
$python = 'C:\Program Files\Python313\python.exe'
if (-not (Test-Path -LiteralPath $manager)) { throw "Fleet manager not found: $manager" }
if (-not (Test-Path -LiteralPath $python)) { $python = 'python.exe' }
& $python $manager 'release-all'
if ($LASTEXITCODE -ne 0) { throw 'BYOD release request failed.' }
