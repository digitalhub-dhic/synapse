# Local vision API integration

The Sentinel can optionally send a downscaled screen frame to an OpenAI-compatible
vision API running on the school network. Both API switches default to `false`.
The live Sentinel `enabled` switch must also be explicitly enabled before frames
are scanned.

## Secure JARVIS connection

Ollama on JARVIS is bound to `127.0.0.1:11434` with cloud features disabled. To
reach it from the Synapse Command PC without exposing an unauthenticated API on
the LAN, open an SSH tunnel on that PC and leave the terminal running:

```powershell
ssh -N -L 11435:127.0.0.1:11434 jarvis@192.168.1.227
```

The configured API base URL is `http://127.0.0.1:11435/v1`; model name is
`qwen3.5:0.8b`. API keys, if another private provider requires one, must be
provided through the `SYNAPSE_VISION_API_KEY` environment variable, never stored
in the JSON config.

## Safe rollout

1. Keep `enabled`, `ai_api_shadow_enabled`, `ai_api_alerts_enabled`, and
   `auto_freeze` set to `false` until the endpoint and model have been validated
   against approved, labeled sample screens.
2. Shadow mode sends at most one reduced image per computer per configured
   interval. Results are for evaluation and do not create alerts or screenshots.
3. Review alerts require a separate `ai_api_alerts_enabled` opt-in and two
   separate positive API evaluations by default. Rate-limited scan cycles keep a
   candidate pending for up to 35 seconds; an explicit negative result clears it.
   API-generated candidates are always sent to teacher review; they cannot
   trigger an automatic lock.
4. The endpoint must use HTTP on a loopback or private IP. Public IPs, public
   hostnames, and credentials embedded in the URL are rejected.

This integration is not activated by default and has not been deployed to the
Sentinel installation or student computers.
