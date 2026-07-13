# Bandit Mic Relay

Bandit Mic Relay streams a microphone from a companion device to the Xbox UWP virtual
microphone over UDP, so Simple Voice Chat can use it inside the sandbox.

- App to Xbox: UDP `7340`.
- Format: 48000 Hz, mono, signed 16-bit little-endian PCM.
- Packet (little-endian): `magic "BMA1"` (4) + `seq` (u32) + `sampleRate` (u32) +
  `channels` (u8) + `bitsPerSample` (u8) + `sampleCount` (u16) + PCM payload
  (`sampleCount * 2` bytes). Send 10 ms chunks (480 samples) to stay under the MTU.

This tool does not launch Minecraft and does not handle account auth or entitlement checks.

## Run

```
pip install sounddevice
python relay_mic.py --host <xbox-ip> --port 7340 --frame-ms 10
```

- `--device` selects an input by name or index (default: system default input).
- On the console, open Simple Voice Chat settings and pick `Bandit Relay Microphone`.
- Fabric targets load the provider from the launcher classpath. Forge and NeoForge targets
  load it from the bundled `bandit_mic_relay` mod that the launcher syncs into the profile
  `mods/` folder; no manual install is needed on either loader.

A browser page can not be used as the sender: `getUserMedia` needs a secure context, and
the console relay is served over plain HTTP on the LAN. Use this native sender instead.
