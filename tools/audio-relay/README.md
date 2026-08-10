# Bandit Launcher audio relay

The audio relay forwards the Xbox default render output to a laptop over UDP so the game can be heard through the laptop's headphones. The companion initiates everything: the laptop receiver discovers itself by sending subscriptions, so no laptop address is ever stored on the Xbox. The launcher starts `audio_relay.exe` automatically at game launch.

## Requirements

### Xbox helper

- Windows Dev Mode environment where the launcher can start the packaged helper
- Windows SDK WASAPI and Winsock libraries
- `audio_relay.exe` present beside the launcher executable or in the package root
- UDP port 7341 allowed through the relevant Xbox and network firewall rules

### Laptop receiver

- Python 3
- `sounddevice`
- A working PortAudio output backend

Install the Python dependency:

```powershell
python -m pip install sounddevice
```

On Linux, the system PortAudio package may also be required.

## Usage

List available output devices:

```powershell
python relay_audio.py --list-devices
```

Start receiving from the Xbox:

```powershell
python relay_audio.py --host 192.168.1.50
```

Choose an output device by index:

```powershell
python relay_audio.py --host 192.168.1.50 --device 7
```

Choose an output device by name:

```powershell
python relay_audio.py --host 192.168.1.50 --device "Headphones"
```

Adjust the jitter buffer:

```powershell
python relay_audio.py --host 192.168.1.50 --latency-ms 120
```

Use a non-default control port:

```powershell
python relay_audio.py --host 192.168.1.50 --port 7441
```

The helper accepts the same port as its only optional command-line argument:

```powershell
audio_relay.exe 7441
```

Lower latency reduces delay but increases the chance of underruns on unstable Wi-Fi. The default is 80 ms.

## Subscriber protocol

The helper binds an IPv4 UDP socket to `0.0.0.0:7341`.

The receiver sends exactly four ASCII bytes:

```text
BMAS
```

The source IP address and source UDP port of that datagram become the audio delivery endpoint. The receiver repeats the message every 1.5 seconds. The newest subscriber replaces the previous subscriber, so restarting the receiver (which picks a new ephemeral port) redirects the stream immediately.

If no keepalive arrives for six seconds, the helper:

1. Stops sending audio.
2. Releases the WASAPI audio client.
3. Returns to an inexpensive control-socket wait.
4. Reacquires the default render endpoint after a new subscription.

There is no authentication or encryption. Use the relay only on a trusted local network.

## Audio packets

All integer fields are little-endian. Every UDP audio datagram starts with this 16-byte header:

| Offset | Size | Field | Description |
|---:|---:|---|---|
| 0 | 4 | magic | ASCII `BMA2` |
| 4 | 4 | sequence | Unsigned packet sequence, wrapping at 32 bits |
| 8 | 4 | sample rate | Actual WASAPI mix rate in Hz |
| 12 | 1 | channels | `1` for mono or `2` for stereo |
| 13 | 1 | bits per sample | Always `16` |
| 14 | 2 | frame count | Interleaved audio frames in the payload |

The payload immediately follows the header and contains:

```text
frameCount × channels × 2
```

bytes of interleaved signed 16-bit little-endian PCM.

The helper emits at most 240 frames per datagram. At 48 kHz stereo, this is 960 payload bytes and approximately 5 ms of audio. The Xbox mix format is typically 48000 Hz stereo float, so the wire format is usually 48000 Hz stereo s16le at roughly 1.5 Mbit/s.

The helper does not resample; the header carries the actual mix rate. Float32 input is clamped and converted to signed 16-bit PCM. PCM16 is passed through. PCM24 and PCM32 use their most significant 16 bits. Mono remains mono. Formats with more than two channels use their first two channels.

While the console renders silence, the helper keeps sending zero-filled packets. A flat level bar with packets still flowing therefore means the console is rendering silence; no packets at all means the helper is not running or not reachable.

## Receiver behavior

The Python receiver opens its `sounddevice.RawOutputStream` from the sample rate and channel count in the first valid `BMA2` packet. Packets whose format differs from that first packet are dropped, so restart the receiver after changing the console's output device or mix format.

It maintains a byte-oriented jitter buffer sized from `--latency-ms`. Playback starts once the buffer has filled to the latency target. When the buffer overflows, the oldest complete audio frames are discarded. On underrun, silence is written and the receiver refills the jitter target before resuming.

The live status line reports:

- Peak level of the most recently received packet
- Recent UDP bitrate
- Valid packet count
- Invalid, out-of-order, or local overflow drops
- Missing sequence count
- Playback underruns
- Current buffered audio duration

If no valid packet arrives for three seconds, verify that:

- The Xbox and laptop are on mutually reachable networks
- The launcher started `audio_relay.exe`
- The game is producing audio
- The configured UDP port matches
- Firewall rules allow inbound UDP on the Xbox helper
- Client isolation is disabled on the Wi-Fi access point

## Building

`audio_relay\build_audio_relay.ps1` compiles the helper from `audio_relay\audio_relay.cpp` with `cl.exe /std:c++17` (linking `ws2_32.lib` and `ole32.lib`, static CRT); the main `build.ps1` runs it automatically and packages `audio_relay.exe` into the package root:

```powershell
.\audio_relay\build_audio_relay.ps1
```

The script resolves the Visual Studio toolchain itself, the same way the other native helpers build.

## Logging

The helper appends sparse lifecycle and error records to:

```text
audio_relay.log
```

The file is created in the helper's working directory, normally the launcher's game directory. Packet-level events are not logged.
