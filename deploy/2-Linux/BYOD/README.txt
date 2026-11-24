===================================================================
     SYNAPSE COMMAND — LINUX BYOD STUDENT SETUP GUIDE
===================================================================

HOW TO INSTALL ON A LINUX STUDENT LAPTOP (Ubuntu, Mint, Debian, Fedora):

METHOD 1: From the local network file share
-------------------------------------------
1. Connect student laptop to classroom Wi-Fi / network.
2. Open File Manager -> Connect to Server:
   smb://FILESERVER/public/Synapse/2-Linux/BYOD/
3. Copy 'install-synapse-byod.sh' and 'Tools/synapse-linux-check.sh' into the same folder.
4. Open Terminal and run:
   chmod +x install-synapse-byod.sh
   sudo ./install-synapse-byod.sh

METHOD 2: One-line Terminal Command (if mounted / copied)
---------------------------------------------------------
   sudo bash install-synapse-byod.sh

WHAT THE INSTALLER DOES AUTOMATICALLY:
-------------------------------------
1. Shows an explicit monitoring and control consent notice. The user must type `YES` interactively before installation; headless/noninteractive installation is refused. After readiness checks pass, the accepting Linux account and UTC acceptance time are recorded in `/var/lib/synapse/byod-consent.json`.
2. Installs Veyon from distro package repositories and requires version 4.11.5 or newer. Older packages are rejected; install a current official package and rerun if the distro repository is behind.
3. Installs Tailscale from its official Linux installer if needed, then requires tailnet authentication (interactive URL sign-in if no provisioning key is supplied).
4. Embeds and imports the official classroom Digihub Security Public Key.
5. Configures key authentication and background service autostart.
6. Keeps the Veyon service stopped while installing, then restricts TCP ports 11100 (control) and 11400 (demo broadcast) to the configured teacher LAN/Tailscale addresses using active UFW or firewalld. Missing firewall protection, broad/range allow rules, and firewall command failures stop enrollment.
7. Announces the student's hostname and IP using the fleet listener's JSON beacon format on UDP 11105 over LAN and Tailscale.
8. Installs `synapse-linux-check` to report version, service, key, Tailscale, firewall, session type, PipeWire/portal, and teacher reachability status.

HOW TEACHER CONNECTS IN SYNAPSE COMMAND:
----------------------------------------
1. On the Teacher Console (Synapse Command), look at the left sidebar under 'BYOD'.
2. Right-click 'BYOD' -> 'Add computer'.
3. Enter the student's Hostname or IP address shown at the end of the installation.
4. The student's screen will appear live in the monitoring grid.
5. For Wayland, run `synapse-linux-check` from the student's logged-in desktop. The desktop portal may ask the user to allow screen capture.

Set `SYNAPSE_TEACHER_LAN_IP` and `SYNAPSE_TEACHER_TAILSCALE_IP` before
installation if the teacher console does not use the defaults. The installer
stores these IPv4 endpoints for the readiness check and firewall rules. Devices
must be enrolled in the college tailnet; the installer does not embed or create
a reusable Tailscale key.

RELEASE AND REVOKE:
-------------------
Run `synapse-off.sh` with administrator privileges to stop and disable the Veyon service. The release marker is written only after systemd confirms the service stopped. Tailscale remains connected; this release action disables Veyon screen sharing/control, not the device's network connection.
Run `synapse-on.sh` with administrator privileges to start and enable Veyon. The release marker is cleared only after systemd confirms the service is active. Then confirm live capture/control in the teacher console; the script's beacon is not proof of enrollment or successful monitoring.
Re-running the installer preserves a released state: it does not restart or re-enable Veyon while the release marker exists.
===================================================================
