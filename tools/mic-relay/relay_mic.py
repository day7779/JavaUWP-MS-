#!/usr/bin/env python3
"""Bandit mic relay: stream this device's default microphone to the console over UDP."""
import argparse
import socket
import struct
import sys

try:
    import sounddevice as sd
except ImportError:
    sys.stderr.write("sounddevice is required: pip install sounddevice\n")
    raise

MAGIC = b"BMA1"
SAMPLE_RATE = 48000
CHANNELS = 1
BITS = 16


def main():
    parser = argparse.ArgumentParser(description="Stream the default microphone to the Xbox console over UDP.")
    parser.add_argument("--host", required=True, help="console IP address")
    parser.add_argument("--port", type=int, default=7340)
    parser.add_argument("--device", default=None, help="input device name or index")
    parser.add_argument("--frame-ms", type=int, default=10, dest="frame_ms")
    args = parser.parse_args()

    frames = max(1, SAMPLE_RATE * args.frame_ms // 1000)
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    dst = (args.host, args.port)
    seq = 0

    device = args.device
    if device is not None:
        try:
            device = int(device)
        except ValueError:
            pass

    def callback(indata, frame_count, time_info, status):
        nonlocal seq
        if status:
            sys.stderr.write(str(status) + "\n")
        header = struct.pack("<4sIIBBH", MAGIC, seq & 0xFFFFFFFF, SAMPLE_RATE, CHANNELS, BITS, frame_count)
        try:
            sock.sendto(header + bytes(indata), dst)
        except OSError as exc:
            sys.stderr.write("send failed: %s\n" % exc)
        seq += 1

    print("streaming %d Hz mono s16le, %d samples/packet -> %s:%d" % (SAMPLE_RATE, frames, args.host, args.port))
    with sd.RawInputStream(samplerate=SAMPLE_RATE, blocksize=frames, dtype="int16",
                           channels=CHANNELS, device=device, callback=callback):
        print("relay running, press Ctrl+C to stop")
        try:
            while True:
                sd.sleep(1000)
        except KeyboardInterrupt:
            print("\nstopped")


if __name__ == "__main__":
    main()
