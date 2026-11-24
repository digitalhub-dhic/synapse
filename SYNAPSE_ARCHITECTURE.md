# Synapse architecture and readiness notes

**Readiness: not approved for production BYOD deployment.** This document records what the current repository shows; it does not certify the external DigiAgent/Veyon binaries or the deployed network.

## Components present in the repository

| Component | What the source currently does | Limits visible in source |
| --- | --- | --- |
| Sentinel dashboard | Hosts a loopback Flask console, authenticates the teacher, and exposes monitoring and fleet-control routes. | No device acknowledgment for release/re-enforcement. The dashboard is bound to localhost; remote administration depends on the classroom client and network. |
| Windows BYOD Guard | Runs in a student's session, checks classroom connectivity, displays a lock screen, and verifies a per-device teacher password. | It depends on user-session behavior and permissions. It cannot provide tamper-proof enforcement on a personally administered device. |
| Windows SYSTEM watchdog | Repairs installation permissions, attempts network recovery, and records connectivity state. | It does not display UI. It is separate from the student's visible Guard. Runtime behavior must be checked on supported Windows versions. |
| Windows classroom client | Installer packages external DigiAgent/Veyon binaries, keys, and plugins. | Those binaries are build inputs outside this repository; their versions, signatures, settings, and behavior are not proven by Python source checks. |
| Linux BYOD and lab setup | Installs/configures Veyon, network access, consent state, and release/re-enforcement scripts. | Wayland screen capture and input control have not been verified on college hardware. Release state depends on local root-level service scripts. |
| Windows updater | Reads a version manifest, stages signed client files, and attempts rollback if replacement fails. | The publisher thumbprint is a required local trust setting. The checked-in placeholder stops updates until configured. The manifest and SMB share still need release controls and recorded payload hashes. |

## BYOD lifecycle behavior

The intended lifecycle is consent, active classroom management, release for leave periods, then re-enforcement. The current Windows fleet manager submits Veyon `StartApp` requests to edit a protected policy file. Veyon feature processes generally run in the logged-in user's context; the policy directory is protected from standard-user writes. A successful CLI result therefore does not prove that the policy changed. The manager correctly reports the operation as requested or unconfirmed.

Re-enforcement also depends on remote PowerShell remoting to start `DigiAgentService` after release. The installer does not establish or verify the required WinRM configuration. The manager now attempts service startup before asking Veyon to clear the policy, but that is only useful where remoting is already configured and authorized. Until a trusted client-side privileged handler and an acknowledgment protocol exist, Windows release/re-enforcement is incomplete.

The Linux scripts change local release state as root and verify some service and firewall outcomes. That is a different mechanism from the Windows fleet path; end-to-end teacher-to-client acknowledgment still needs device validation.

## Security and deployment constraints

- Consent is required by the BYOD installer, and the installer provisions a per-device teacher-password verifier. This does not make user-session controls resistant to a device owner with administrative or physical access.
- Windows client and updater trust depends on external signing certificates, keys, vendor binaries, and package hashes. Record and validate these for every release.
- Python requirements specify version ranges, not a hash-locked dependency set.
- Tailscale must be authenticated and scoped through the college's own tailnet policy. The repository alone does not establish that enrollment or access policy.
- The local Sentinel dashboard has password authentication and loopback/origin checks. Its security still depends on the Windows account, filesystem ACLs, and the actual installed dependencies.

## Validation still required

1. Test consent, active monitoring, release, and re-enforcement on each supported Windows build, including a standard student account, reboot, sign-out, disconnected network, and failed WinRM cases.
2. Implement a privileged, authenticated Windows command handler that can change release state without granting standard users write access; require a client acknowledgment before displaying a completed state.
3. Test Veyon screen capture, remote input, lock, file transfer, and app controls on supported Windows and Linux desktops. Wayland behavior must be tested per compositor and portal setup; the upstream project describes current Wayland support as experimental.
4. Build the actual installers on Windows, inspect signatures and ACLs, verify rollback and uninstall, and test a release using the exact external binaries and certificates intended for production.
5. Record dependency and vendor-payload versions, hashes, signatures, key ownership, and rotation procedures for each deployable release.
