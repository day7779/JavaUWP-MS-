import argparse
import array
import collections
import select
import socket
import struct
import sys
import threading
import time


HEADER = struct.Struct("<4sIIBBH")
KEEPALIVE_INTERVAL = 1.5
STATUS_INTERVAL = 0.2
NO_PACKET_WARNING = 3.0


class JitterBuffer:
    def __init__(self, sample_rate, channels, latency_ms):
        self.frame_bytes = channels * 2
        requested = int(
            sample_rate * self.frame_bytes * latency_ms / 1000.0
        )
        requested -= requested % self.frame_bytes
        self.target_bytes = max(self.frame_bytes, requested)
        self.capacity_bytes = self.target_bytes
        self.data = bytearray()
        self.lock = threading.Lock()
        self.armed = False
        self.overflow_events = 0
        self.underruns = 0

    def push(self, payload):
        with self.lock:
            if len(payload) >= self.capacity_bytes:
                keep = self.capacity_bytes
                keep -= keep % self.frame_bytes
                payload = payload[-keep:]
                self.data.clear()
                self.overflow_events += 1
            else:
                excess = (
                    len(self.data)
                    + len(payload)
                    - self.capacity_bytes
                )

                if excess > 0:
                    excess = (
                        (excess + self.frame_bytes - 1)
                        // self.frame_bytes
                        * self.frame_bytes
                    )
                    del self.data[:excess]
                    self.overflow_events += 1

            self.data.extend(payload)

    def read_frames(self, frames):
        required = frames * self.frame_bytes

        with self.lock:
            if not self.armed:
                if len(self.data) < self.target_bytes:
                    return bytes(required)
                self.armed = True

            if len(self.data) < required:
                available = bytes(self.data)
                self.data.clear()
                self.armed = False
                self.underruns += 1
                return available + bytes(required - len(available))

            output = bytes(self.data[:required])
            del self.data[:required]
            return output

    def snapshot(self):
        with self.lock:
            return (
                len(self.data),
                self.overflow_events,
                self.underruns,
            )


class ReceiverStats:
    def __init__(self):
        self.packets = 0
        self.drops = 0
        self.sequence_gaps = 0
        self.last_peak = 0
        self.last_packet_time = None
        self.rate_window = collections.deque()

    def add_rate_sample(self, timestamp, byte_count):
        self.rate_window.append((timestamp, byte_count))

    def kbps(self, now):
        cutoff = now - 1.0

        while self.rate_window and self.rate_window[0][0] < cutoff:
            self.rate_window.popleft()

        return (
            sum(byte_count for _, byte_count in self.rate_window)
            * 8.0
            / 1000.0
        )


def parse_device(value):
    if value is None:
        return None

    try:
        return int(value)
    except ValueError:
        return value


def print_devices(sounddevice):
    devices = sounddevice.query_devices()

    for index, device in enumerate(devices):
        output_channels = int(device["max_output_channels"])
        if output_channels <= 0:
            continue

        print(
            f"{index}: {device['name']} "
            f"({output_channels} output channels, "
            f"default {device['default_samplerate']:.0f} Hz)"
        )


def packet_peak(payload):
    samples = array.array("h")
    samples.frombytes(payload)

    if sys.byteorder != "little":
        samples.byteswap()

    if not samples:
        return 0

    return min(32767, max(max(samples), -min(samples)))


def parse_packet(data):
    if len(data) < HEADER.size:
        return None

    magic, sequence, sample_rate, channels, bits, frames = (
        HEADER.unpack_from(data)
    )

    if magic != b"BMA2":
        return None

    if bits != 16 or channels not in (1, 2):
        return None

    if sample_rate < 8000 or sample_rate > 384000:
        return None

    if frames == 0:
        return None

    payload_size = frames * channels * 2

    if payload_size > 1200:
        return None

    if len(data) != HEADER.size + payload_size:
        return None

    return (
        sequence,
        sample_rate,
        channels,
        data[HEADER.size:],
    )


def make_parser():
    parser = argparse.ArgumentParser(
        description="Receive Bandit Launcher Xbox audio over UDP."
    )
    parser.add_argument(
        "--host",
        help="Xbox IP address or hostname",
    )
    parser.add_argument(
        "--port",
        type=int,
        default=7341,
        help="Xbox audio relay control port (default: 7341)",
    )
    parser.add_argument(
        "--latency-ms",
        type=float,
        default=80.0,
        help="Jitter-buffer latency in milliseconds (default: 80)",
    )
    parser.add_argument(
        "--device",
        help="sounddevice output device name or index",
    )
    parser.add_argument(
        "--list-devices",
        action="store_true",
        help="List output devices and exit",
    )
    return parser


def main():
    parser = make_parser()
    arguments = parser.parse_args()

    try:
        import sounddevice
    except ImportError:
        print(
            "sounddevice is required: "
            "python -m pip install sounddevice",
            file=sys.stderr,
        )
        return 2

    if arguments.list_devices:
        print_devices(sounddevice)
        return 0

    if not arguments.host:
        parser.error("--host is required unless --list-devices is used")

    if arguments.port < 1 or arguments.port > 65535:
        parser.error("--port must be between 1 and 65535")

    if arguments.latency_ms <= 0:
        parser.error("--latency-ms must be greater than zero")

    address_info = socket.getaddrinfo(
        arguments.host,
        arguments.port,
        socket.AF_INET,
        socket.SOCK_DGRAM,
    )

    if not address_info:
        print("Unable to resolve Xbox address", file=sys.stderr)
        return 3

    target = address_info[0][4]
    output_device = parse_device(arguments.device)

    receiver = socket.socket(
        socket.AF_INET,
        socket.SOCK_DGRAM,
        socket.IPPROTO_UDP,
    )
    receiver.setsockopt(
        socket.SOL_SOCKET,
        socket.SO_RCVBUF,
        1024 * 1024,
    )
    receiver.setblocking(False)

    stats = ReceiverStats()
    jitter = None
    stream = None
    stream_format = None
    expected_sequence = None
    started_at = time.monotonic()
    next_keepalive = started_at
    next_status = started_at
    warned_no_packets = False
    last_send_warning = 0.0

    def output_callback(outdata, frames, time_info, callback_status):
        payload = jitter.read_frames(frames)
        outdata[:] = payload

    try:
        while True:
            now = time.monotonic()

            if now >= next_keepalive:
                try:
                    receiver.sendto(b"BMAS", target)
                except OSError as error:
                    if now - last_send_warning >= 3.0:
                        print(
                            f"\nwarning: keepalive send failed: {error}",
                            file=sys.stderr,
                        )
                        last_send_warning = now

                next_keepalive = now + KEEPALIVE_INTERVAL

            timeout = min(
                0.1,
                max(0.0, next_keepalive - now),
                max(0.0, next_status - now),
            )

            readable, _, _ = select.select(
                [receiver],
                [],
                [],
                timeout,
            )

            if readable:
                while True:
                    try:
                        data, source = receiver.recvfrom(65535)
                    except BlockingIOError:
                        break
                    except OSError:
                        stats.drops += 1
                        break

                    if (
                        source[0] != target[0]
                        or source[1] != target[1]
                    ):
                        continue

                    received_at = time.monotonic()
                    parsed = parse_packet(data)

                    if parsed is None:
                        stats.drops += 1
                        continue

                    sequence, sample_rate, channels, payload = parsed
                    previous_packet_time = stats.last_packet_time

                    if (
                        previous_packet_time is None
                        or received_at - previous_packet_time
                        >= NO_PACKET_WARNING
                    ):
                        expected_sequence = None

                    if expected_sequence is not None:
                        delta = (
                            sequence - expected_sequence
                        ) & 0xFFFFFFFF

                        if delta == 0:
                            pass
                        elif delta < 0x80000000:
                            stats.sequence_gaps += delta
                        else:
                            stats.drops += 1
                            continue

                    expected_sequence = (sequence + 1) & 0xFFFFFFFF
                    stats.last_packet_time = received_at
                    warned_no_packets = False

                    packet_format = (sample_rate, channels)

                    if stream_format is None:
                        stream_format = packet_format
                        jitter = JitterBuffer(
                            sample_rate,
                            channels,
                            arguments.latency_ms,
                        )

                        try:
                            stream = sounddevice.RawOutputStream(
                                samplerate=sample_rate,
                                channels=channels,
                                dtype="int16",
                                blocksize=0,
                                device=output_device,
                                callback=output_callback,
                                start=False,
                            )

                            stream.start()
                        except (
                            sounddevice.PortAudioError,
                            ValueError,
                        ) as error:
                            print(
                                f"Unable to open output stream: {error}",
                                file=sys.stderr,
                            )
                            return 4

                        try:
                            device_name = sounddevice.query_devices(
                                stream.device
                            )["name"]
                        except Exception:
                            device_name = "default output"

                        print(
                            f"Audio format: {sample_rate} Hz, "
                            f"{channels} channel(s), s16le -> "
                            f"{device_name}"
                        )

                    elif packet_format != stream_format:
                        stats.drops += 1
                        continue

                    jitter.push(payload)
                    stats.packets += 1
                    stats.last_peak = packet_peak(payload)
                    stats.add_rate_sample(received_at, len(data))

            now = time.monotonic()
            silence_age = (
                now - stats.last_packet_time
                if stats.last_packet_time is not None
                else now - started_at
            )

            if (
                silence_age >= NO_PACKET_WARNING
                and not warned_no_packets
            ):
                print(
                    "\nwarning: no packets for 3 s "
                    "(is the game running / helper started?)"
                )
                warned_no_packets = True

            if now >= next_status:
                if (
                    stats.last_packet_time is None
                    or now - stats.last_packet_time > 0.5
                ):
                    displayed_peak = 0
                else:
                    displayed_peak = stats.last_peak

                ratio = displayed_peak / 32767.0
                active_segments = min(20, int(ratio * 20.0))
                level_bar = (
                    "#" * active_segments
                    + "-" * (20 - active_segments)
                )

                if jitter is None:
                    buffered_ms = 0.0
                    overflow_drops = 0
                    underruns = 0
                else:
                    buffered_bytes, overflow_drops, underruns = (
                        jitter.snapshot()
                    )
                    sample_rate, channels = stream_format
                    buffered_ms = (
                        buffered_bytes
                        * 1000.0
                        / (sample_rate * channels * 2)
                    )

                total_drops = stats.drops + overflow_drops

                status = (
                    f"\rlevel [{level_bar}] "
                    f"{stats.kbps(now):6.1f} kbps | "
                    f"packets {stats.packets} | "
                    f"drops {total_drops} | "
                    f"gaps {stats.sequence_gaps} | "
                    f"underruns {underruns} | "
                    f"buffer {buffered_ms:5.1f} ms"
                )

                print(status, end="", flush=True)
                next_status = now + STATUS_INTERVAL

    except KeyboardInterrupt:
        pass
    finally:
        print()

        if stream is not None:
            try:
                stream.stop()
            finally:
                stream.close()

        receiver.close()

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
