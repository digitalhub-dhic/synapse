========================================================================
                      SYNAPSE DEPLOYMENT REPOSITORY
========================================================================

📁 1-Windows\
   ├── BYOD\           : Student Laptops (Install-BYOD-Student.bat, Synapse-Setup-BYOD.exe)
   │                     Instructor console controls release/revoke; student-side toggles are retired.
   │                     Install prompts for a per-device teacher override password and installs
   │                     the updater locally with a protected SYSTEM task.
   └── Lab\            : Institutional Lab PCs (Install-Lab-Silent.bat, Synapse-Setup-Lab.exe)

📁 2-Linux\
   ├── BYOD\           : Linux Student Laptops (run-byod-installer.sh, install-synapse-byod.sh)
   │                     Requires college Tailscale enrollment; administrator release/revoke scripts are provided.
   ├── Lab\            : Linux Lab Workstations (run-lab-installer.sh, install-synapse-lab.sh)
   └── Tools\          : Shared Linux readiness checker used by both deployment paths.

📁 3-Security-Keys\
   ├── Master-Private-Key\  : RESTRICTED - Teacher Master Private Key.
   └── Public-Key\          : Safe for All Client Computers (Digihub.pem).

📁 4-Auto-Updater\
   ├── Check-And-AutoUpdate.ps1 : Windows auto-updater. Configure its local publisher thumbprint before release.
   ├── client_version.json      : Release metadata and fleet versions.
   └── Payload\                 : Core binaries, DLLs, and plugins for fleet auto-updates.

WINDOWS UPDATE TRUST REQUIREMENT
--------------------------------
The protected local updater rejects payloads unless DigiAgentCore.dll and every
downloaded plugin are Authenticode-signed by the thumbprint pinned in
4-Auto-Updater\Check-And-AutoUpdate.ps1. Replace the placeholder with the actual
publisher certificate thumbprint before packaging/deploying the updater. Do not
load the trust thumbprint from client_version.json or another network-share file.
The BYOD bootstrap validates this pin before installing or recording consent;
until configured, BYOD setup stops before enrollment.

For each BYOD, Install-BYOD-Student.bat prompts for the teacher override password.
Use a unique strong password per device, or use the same password only if the
college deliberately manages that shared credential. The verifier is stored in
ProgramData as a salted PBKDF2 verifier that interactive users can read but
cannot replace; the password itself is not stored. Use at least 16 characters.
Missing credential setup is a deployment failure for BYOD.

📁 5-Teacher-Tools\
   ├── Teacher-Release-BYOD.bat     : Releases BYOD student laptops at end of class.
   ├── Teacher-Enforce-BYOD.bat     : Re-enforces BYOD classroom policy during class.
   └── Teacher-Uninstall-Synapse.bat: Uses the per-device teacher credential; the verifier PowerShell file must be beside it.

========================================================================
