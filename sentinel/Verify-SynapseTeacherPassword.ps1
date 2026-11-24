$ErrorActionPreference = 'Stop'
$credentialPath = 'C:\ProgramData\DigiAgent\teacher_credential.json'

try {
    if (-not (Test-Path -LiteralPath $credentialPath -PathType Leaf)) {
        throw 'This device has no per-device teacher credential. Contact the lab administrator.'
    }
    $credential = Get-Content -LiteralPath $credentialPath -Raw | ConvertFrom-Json
    if ($credential.version -ne 1 -or $credential.algorithm -ne 'PBKDF2-HMAC-SHA256') {
        throw 'The stored teacher credential uses an unsupported format.'
    }
    $iterations = [int]$credential.iterations
    if ($iterations -lt 100000 -or $iterations -gt 2000000) {
        throw 'The stored teacher credential has invalid parameters.'
    }
    $salt = [Convert]::FromBase64String([string]$credential.salt)
    $expected = [Convert]::FromBase64String([string]$credential.hash)
    if ($salt.Length -lt 16 -or $expected.Length -ne 32) {
        throw 'The stored teacher credential is invalid.'
    }

    $secure = Read-Host 'Enter this device''s teacher override password' -AsSecureString
    $ptr = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($secure)
    try {
        $password = [Runtime.InteropServices.Marshal]::PtrToStringBSTR($ptr)
        $derive = [Security.Cryptography.Rfc2898DeriveBytes]::new(
            $password, $salt, $iterations, [Security.Cryptography.HashAlgorithmName]::SHA256)
        try { $actual = $derive.GetBytes(32) } finally { $derive.Dispose() }
        $difference = 0
        for ($i = 0; $i -lt $expected.Length; $i++) {
            $difference = $difference -bor ($expected[$i] -bxor $actual[$i])
        }
        if ($difference -ne 0) { throw 'Incorrect teacher password.' }
    }
    finally {
        if ($actual) { [Array]::Clear($actual, 0, $actual.Length) }
        [Array]::Clear($expected, 0, $expected.Length)
        [Array]::Clear($salt, 0, $salt.Length)
        [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($ptr)
        $password = $null
    }
    exit 0
}
catch {
    Write-Host "Access denied: $($_.Exception.Message)" -ForegroundColor Red
    exit 1
}
