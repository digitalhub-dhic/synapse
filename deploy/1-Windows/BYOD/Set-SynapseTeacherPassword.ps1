# Run from an elevated PowerShell prompt on each BYOD device after installation.
$ErrorActionPreference = 'Stop'
$credentialPath = 'C:\ProgramData\DigiAgent\teacher_credential.json'
$iterations = 310000

$secure = Read-Host 'Set this device''s teacher override password' -AsSecureString
$ptr = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($secure)
try {
    $password = [Runtime.InteropServices.Marshal]::PtrToStringBSTR($ptr)
    if ([string]::IsNullOrWhiteSpace($password) -or $password.Length -lt 16) {
        throw 'Password must contain at least 16 characters.'
    }

    $salt = New-Object byte[] 16
    [Security.Cryptography.RandomNumberGenerator]::Create().GetBytes($salt)
    $derive = [Security.Cryptography.Rfc2898DeriveBytes]::new(
        $password, $salt, $iterations, [Security.Cryptography.HashAlgorithmName]::SHA256)
    try { $hash = $derive.GetBytes(32) } finally { $derive.Dispose() }

    New-Item -ItemType Directory -Path (Split-Path $credentialPath) -Force | Out-Null
    $document = [ordered]@{
        version = 1
        algorithm = 'PBKDF2-HMAC-SHA256'
        iterations = $iterations
        salt = [Convert]::ToBase64String($salt)
        hash = [Convert]::ToBase64String($hash)
    } | ConvertTo-Json
    Set-Content -LiteralPath $credentialPath -Value $document -Encoding ASCII
    # The interactive guard needs to read the verifier as the signed-in user.
    # Users can read the salted verifier, but only SYSTEM/admins can replace it.
    & icacls.exe $credentialPath /inheritance:r /grant:r 'SYSTEM:(F)' 'Administrators:(F)' 'Users:(R)' | Out-Null
    if ($LASTEXITCODE -ne 0) { throw 'Could not secure the credential file ACL.' }
    Write-Host 'Teacher override password saved for this device.'
}
finally {
    [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($ptr)
    $password = $null
}
