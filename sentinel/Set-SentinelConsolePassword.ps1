$ErrorActionPreference = 'Stop'
$iterations = 310000
$securityDir = Join-Path $env:LOCALAPPDATA 'SynapseSentinel\security'

try {
    New-Item -ItemType Directory -Path $securityDir -Force | Out-Null

    # Restrict the folder to this teacher account, SYSTEM, and local admins.
    # Other local users must not be able to read the Flask cookie-signing key.
    $currentSid = [Security.Principal.WindowsIdentity]::GetCurrent().User
    $directoryAcl = New-Object System.Security.AccessControl.DirectorySecurity
    $directoryAcl.SetAccessRuleProtection($true, $false)
    $directoryAcl.SetOwner($currentSid)
    foreach ($sidText in @($currentSid.Value, 'S-1-5-18', 'S-1-5-32-544')) {
        $sid = [Security.Principal.SecurityIdentifier]::new($sidText)
        $inheritance = [Security.AccessControl.InheritanceFlags]::ContainerInherit -bor
            [Security.AccessControl.InheritanceFlags]::ObjectInherit
        $rule = [Security.AccessControl.FileSystemAccessRule]::new(
            $sid, [Security.AccessControl.FileSystemRights]::FullControl, $inheritance,
            [Security.AccessControl.PropagationFlags]::None, [Security.AccessControl.AccessControlType]::Allow)
        $directoryAcl.AddAccessRule($rule)
    }
    Set-Acl -LiteralPath $securityDir -AclObject $directoryAcl

    $secure = Read-Host 'Set the Sentinel teacher-console password (minimum 16 characters)' -AsSecureString
    $ptr = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($secure)
    try {
        $password = [Runtime.InteropServices.Marshal]::PtrToStringBSTR($ptr)
        if ([string]::IsNullOrWhiteSpace($password) -or $password.Length -lt 16) {
            throw 'Password must contain at least 16 characters.'
        }
        $salt = New-Object byte[] 16
        $rng = [Security.Cryptography.RandomNumberGenerator]::Create()
        try { $rng.GetBytes($salt) } finally { $rng.Dispose() }
        $derive = [Security.Cryptography.Rfc2898DeriveBytes]::new(
            $password, $salt, $iterations, [Security.Cryptography.HashAlgorithmName]::SHA256)
        try { $hash = $derive.GetBytes(32) } finally { $derive.Dispose() }

        $credential = [ordered]@{
            version = 1
            algorithm = 'PBKDF2-HMAC-SHA256'
            iterations = $iterations
            salt = [Convert]::ToBase64String($salt)
            hash = [Convert]::ToBase64String($hash)
        } | ConvertTo-Json
        $utf8 = New-Object System.Text.UTF8Encoding($false)
        $credentialPath = Join-Path $securityDir 'console_credential.json'
        $credentialTemp = "$credentialPath.tmp"
        [IO.File]::WriteAllText($credentialTemp, $credential, $utf8)
        Move-Item -LiteralPath $credentialTemp -Destination $credentialPath -Force

        $sessionKey = New-Object byte[] 64
        $rng = [Security.Cryptography.RandomNumberGenerator]::Create()
        try { $rng.GetBytes($sessionKey) } finally { $rng.Dispose() }
        $keyPath = Join-Path $securityDir 'web_session.key'
        $keyTemp = "$keyPath.tmp"
        [IO.File]::WriteAllText($keyTemp, [Convert]::ToBase64String($sessionKey), [Text.Encoding]::ASCII)
        Move-Item -LiteralPath $keyTemp -Destination $keyPath -Force
        [Array]::Clear($sessionKey, 0, $sessionKey.Length)
        [Array]::Clear($hash, 0, $hash.Length)
        [Array]::Clear($salt, 0, $salt.Length)
    }
    finally {
        [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($ptr)
        $password = $null
    }
    Write-Host 'Sentinel console credentials saved with account-restricted permissions.'
}
catch {
    Write-Error "Sentinel console password setup failed: $($_.Exception.Message)"
    exit 1
}
