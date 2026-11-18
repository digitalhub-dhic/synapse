===================================================================
     SYNAPSE COMMAND — LINUX BYOD STUDENT SETUP GUIDE
===================================================================

HOW TO INSTALL ON A LINUX STUDENT LAPTOP (Ubuntu, Mint, Debian, Fedora):

METHOD 1: From the local network file share
-------------------------------------------
1. Connect student laptop to classroom Wi-Fi / network.
2. Open File Manager -> Connect to Server:
   smb://FILESERVER/public/Synapse/2-Linux/BYOD/
3. Copy 'install-synapse-byod.sh' to your home folder or Desktop.
4. Open Terminal and run:
   chmod +x install-synapse-byod.sh
   sudo ./install-synapse-byod.sh

METHOD 2: One-line Terminal Command (if mounted / copied)
---------------------------------------------------------
   sudo bash install-synapse-byod.sh

WHAT THE INSTALLER DOES AUTOMATICALLY:
-------------------------------------
1. Installs 'veyon-service' and plugins from distro package repositories.
2. Embeds and imports the official classroom Digihub Security Public Key.
3. Configures key authentication and background service autostart.
4. Opens local firewall ports (11100 / 11400) for classroom monitoring.
5. Displays the student laptop's Hostname and IP address.

HOW TEACHER CONNECTS IN SYNAPSE COMMAND:
----------------------------------------
1. On the Teacher Console (Synapse Command), look at the left sidebar under 'BYOD'.
2. Right-click 'BYOD' -> 'Add computer'.
3. Enter the student's Hostname or IP address shown at the end of the installation.
4. The student's screen will appear live in the monitoring grid.
===================================================================
