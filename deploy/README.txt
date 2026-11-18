========================================================================
                      SYNAPSE DEPLOYMENT REPOSITORY
========================================================================

📁 1-Windows\
   ├── BYOD\           : Student Laptops (Install-BYOD-Student.bat, Synapse-Setup-BYOD.exe)
   │                     Includes Synapse-Turn-OFF.bat & Synapse-Turn-ON.bat for privacy toggling.
   └── Lab\            : Institutional Lab PCs (Install-Lab-Silent.bat, Synapse-Setup-Lab.exe)

📁 2-Linux\
   ├── BYOD\           : Linux Student Laptops (run-byod-installer.sh, install-synapse-byod.sh)
   │                     Includes synapse-off.sh & synapse-on.sh privacy toggles.
   └── Lab\            : Linux Lab Workstations (run-lab-installer.sh, install-synapse-lab.sh)

📁 3-Security-Keys\
   ├── Master-Private-Key\  : RESTRICTED - Teacher Master Private Key.
   └── Public-Key\          : Safe for All Client Computers (Digihub.pem).

📁 4-Auto-Updater\
   ├── Check-And-AutoUpdate.ps1 : Silent client auto-update engine.
   ├── client_version.json      : Release metadata and fleet versions.
   └── Payload\                 : Core binaries, DLLs, and plugins for fleet auto-updates.

📁 5-Teacher-Tools\
   ├── Teacher-Release-BYOD.bat     : Releases BYOD student laptops at end of class.
   ├── Teacher-Enforce-BYOD.bat     : Re-enforces BYOD classroom policy during class.
   └── Teacher-Uninstall-Synapse.bat: Password-protected teacher uninstaller.

========================================================================
