# Windows BYOD installer build

The deployable package name is `Synapse-Setup-BYOD.exe`. Build it with
`Synapse-Setup-BYOD.spec` from this directory so the Python installer, consent
gate, Guard, watchdog, classroom plugins, and Tailscale MSI are bundled
together. `Install-BYOD-Student.bat` requires a one-time consent token produced
by this build; older installer binaries intentionally stop before enrollment.

Stage these signed/vendor payloads in this directory before building:

- `Synapse-Setup.exe`
- `Digihub.key`
- `SynapseBYODGuard.exe`
- `SynapseWatchdog.exe`
- `DigiAgentCore.dll`
- `windows-platform.dll`, `remoteaccess.dll`, `screenlock.dll`
- `tailscale.msi`

The `.nsi` file is a separate packaging implementation and emits
`Synapse-Setup-BYOD-NSIS.exe`; do not copy it over the deployable Python
package. The two build paths must be kept behaviorally aligned if the NSIS
variant is used for development.

## Build environment

Use 64-bit Python 3.11 on Windows. From this directory, create and activate a
virtual environment, install `requirements-build.txt`, then run:

```powershell
python -m pip install -r requirements-build.txt
python -m PyInstaller --noconfirm Synapse-Setup-BYOD.spec
```

The Sentinel dashboard source dependencies are also listed in
`../requirements-console.txt`. These files describe compatible dependency
ranges; they are not a hash-locked software bill of materials. Keep vendor
payload versions and their signatures/hashes in the release record.
