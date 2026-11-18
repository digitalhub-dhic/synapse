import base64

script = """
$out = @()
$out += '=== COMPUTER & DOMAIN ==='
$out += 'Computer: ' + $env:COMPUTERNAME
$out += 'User: ' + [System.Security.Principal.WindowsIdentity]::GetCurrent().Name
$out += 'PartOfDomain: ' + (Get-CimInstance Win32_ComputerSystem).PartOfDomain
$out += '=== REGISTRY POLICIES ==='
$reg1 = Get-ItemProperty -Path 'HKLM:\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Policies\\System' -ErrorAction SilentlyContinue
$out += 'DontDisplayLastUserName: ' + $reg1.DontDisplayLastUserName
$out += 'HideFastUserSwitching (System): ' + $reg1.HideFastUserSwitching
$reg2 = Get-ItemProperty -Path 'HKLM:\\SOFTWARE\\Policies\\Microsoft\\Windows\\System' -ErrorAction SilentlyContinue
$out += 'HideFastUserSwitching (Policies): ' + $reg2.HideFastUserSwitching
$out += '=== PROCESSES ==='
$procs = Get-Process | Where-Object { $_.ProcessName -match 'Digi|Synapse|python' } | Select-Object Id, ProcessName, Path
$out += ($procs | Out-String)
$out += '=== FILES IN DIGIAGENT ==='
$files = Get-ChildItem 'C:\\Program Files\\DigiAgent' -ErrorAction SilentlyContinue | Select-Object Name, Length, LastWriteTime
$out += ($files | Out-String)
$out += '=== SCHEDULED TASKS ==='
$tasks = Get-ScheduledTask -TaskName *Synapse* -ErrorAction SilentlyContinue | Select-Object TaskName, State
$out += ($tasks | Out-String)
$out += '=== WIFI STATUS ==='
$wifi = netsh wlan show interfaces
$out += ($wifi | Out-String)
$body = $out -join "`n"
Invoke-RestMethod -Uri 'http://192.168.1.150:9999' -Method Post -Body $body
"""

b64 = base64.b64encode(script.encode('utf-16le')).decode('ascii')
with open(r'C:\SynapseAISentinel\encoded_report.txt', 'w') as f:
    f.write(b64)
print('Length:', len(b64))
