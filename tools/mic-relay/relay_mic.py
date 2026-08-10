#!/usr/bin/env python3
"""Bandit mic relay: stream a microphone from this device to the console over UDP."""
import argparse
import array
import math
import socket
import struct
import sys
import time

try:
    import sounddevice as sd
except ImportError:
    sys.stderr.write("sounddevice is required: pip install sounddevice\n")
    raise

MAGIC = b"BMA1"
SAMPLE_RATE = 48000
CHANNELS = 1
BITS = 16


def list_devices():
    try:
        default_in = sd.default.device[0]
    except Exception:
        default_in = None
    print("input devices (index: name):")
    for idx, dev in enumerate(sd.query_devices()):
        if dev.get("max_input_channels", 0) > 0:
            mark = "  <- default" if idx == default_in else ""
            print("  %2d: %s%s" % (idx, dev["name"], mark))


def resolve_device(spec):
    if spec is None:
        return None
    try:
        return int(spec)
    except (TypeError, ValueError):
        pass
    needle = spec.lower()
    for idx, dev in enumerate(sd.query_devices()):
        if dev.get("max_input_channels", 0) > 0 and needle in dev["name"].lower():
            return idx
    raise SystemExit("no input device matches %r (try --list-devices)" % spec)


def meter_bar(peak):
    width = 30
    filled = 0 if peak <= 0 else min(width, int(width * peak / 32767.0))
    return "#" * filled + "-" * (width - filled)


def main():
    parser = argparse.ArgumentParser(description="Stream a microphone to the Xbox console over UDP.")
    parser.add_argument("--host", help="console IP address")
    parser.add_argument("--port", type=int, default=7340)
    parser.add_argument("--device", default=None, help="input device name or index")
    parser.add_argument("--frame-ms", type=int, default=10, dest="frame_ms")
    parser.add_argument("--list-devices", action="store_true", help="list input devices and exit")
    args = parser.parse_args()

    if args.list_devices:
        list_devices()
        return
    if not args.host:
        raise SystemExit("--host <xbox-ip> is required (or use --list-devices)")

    device = resolve_device(args.device)
    info = sd.query_devices(device if device is not None else sd.default.device[0], "input")
    frames = max(1, SAMPLE_RATE * args.frame_ms // 1000)
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    dst = (args.host, args.port)

    state = {"seq": 0, "packets": 0, "last": b""}

    def callback(indata, frame_count, time_info, status):
        if status:
            sys.stderr.write(str(status) + "\n")
        payload = bytes(indata)
        header = struct.pack("<4sIIBBH", MAGIC, state["seq"] & 0xFFFFFFFF,
                             SAMPLE_RATE, CHANNELS, BITS, frame_count)
        try:
            sock.sendto(header + payload, dst)
        except OSError as exc:
            sys.stderr.write("send failed: %s\n" % exc)
        state["seq"] += 1
        state["packets"] += 1
        state["last"] = payload

    print("device: %s" % info["name"])
    print("streaming %d Hz mono s16le, %d samples/packet -> %s:%d"
          % (SAMPLE_RATE, frames, args.host, args.port))
    print("talk into the mic - the bar should move. Ctrl+C to stop.\n")

    with sd.RawInputStream(samplerate=SAMPLE_RATE, blocksize=frames, dtype="int16",
                           channels=CHANNELS, device=device, callback=callback):
        silent_since = time.time()
        warned = False
        try:
            while True:
                time.sleep(0.15)
                last = state["last"]
                peak = 0
                rms = 0.0
                if last:
                    samples = array.array("h")
                    samples.frombytes(last)
                    if samples:
                        peak = max(abs(s) for s in samples)
                        rms = math.sqrt(sum(s * s for s in samples) / len(samples))
                dbfs = -99.0 if rms < 1.0 else 20.0 * math.log10(rms / 32767.0)
                sys.stdout.write("\rlevel [%s] peak=%5d %6.1f dBFS  pkts=%d   "
                                 % (meter_bar(peak), peak, dbfs, state["packets"]))
                sys.stdout.flush()
                if peak < 30:
                    if not warned and time.time() - silent_since > 4.0:
                        sys.stdout.write("\n(no input detected - wrong device or muted? "
                                         "run with --list-devices and pass --device)\n")
                        warned = True
                else:
                    silent_since = time.time()
                    warned = False
        except KeyboardInterrupt:
            print("\nstopped")


if __name__ == "__main__":
    main()
