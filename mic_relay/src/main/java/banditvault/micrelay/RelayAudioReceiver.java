package banditvault.micrelay;

import java.io.IOException;
import java.net.DatagramPacket;
import java.net.DatagramSocket;
import java.net.InetSocketAddress;
import java.net.SocketException;
import java.util.concurrent.ConcurrentHashMap;
import javax.sound.sampled.LineUnavailableException;

final class RelayAudioReceiver {
    private static final int HEADER_SIZE = 16;
    private static final int MAX_PACKET_SIZE = 4096;
    private static final int DROP_LOG_INTERVAL = 500;
    private static final ConcurrentHashMap<Integer, RelayAudioReceiver> INSTANCES =
            new ConcurrentHashMap<>();

    private final int port;
    private final ByteRingBuffer buffer;

    private DatagramSocket socket;
    private int references;

    private long received;
    private long dropped;
    private long seqGaps;
    private long lastSeq = -1;

    private RelayAudioReceiver(int port) {
        this.port = port;
        this.buffer = new ByteRingBuffer(RelayConfig.bufferBytes());
    }

    static RelayAudioReceiver forPort(int port) {
        if (port < 1 || port > 65535) {
            throw new IllegalArgumentException("invalid udp port: " + port);
        }
        return INSTANCES.computeIfAbsent(port, RelayAudioReceiver::new);
    }

    ByteRingBuffer buffer() {
        return buffer;
    }

    synchronized void attach() throws LineUnavailableException {
        if (references > 0) {
            references++;
            return;
        }
        DatagramSocket created = null;
        try {
            created = new DatagramSocket(null);
            created.setReuseAddress(false);
            created.setReceiveBufferSize(1 << 16);
            created.bind(new InetSocketAddress("0.0.0.0", port));
            buffer.clear();
            socket = created;
            references = 1;
            received = 0;
            dropped = 0;
            seqGaps = 0;
            lastSeq = -1;
            DatagramSocket active = created;
            Thread thread = new Thread(() -> receiveLoop(active), "bandit-mic-relay-" + port);
            thread.setDaemon(true);
            thread.start();
            MicRelayLog.log("listening for mic audio on udp 0.0.0.0:" + port);
        } catch (Exception e) {
            if (created != null) {
                created.close();
            }
            socket = null;
            references = 0;
            MicRelayLog.log("failed to bind mic relay udp port " + port, e);
            LineUnavailableException unavailable = new LineUnavailableException(
                    "cannot bind udp mic relay on port " + port + ": " + e);
            unavailable.initCause(e);
            throw unavailable;
        }
    }

    synchronized void detach() {
        if (references == 0) {
            return;
        }
        references--;
        if (references == 0) {
            DatagramSocket closing = socket;
            socket = null;
            if (closing != null) {
                closing.close();
            }
            buffer.clear();
            MicRelayLog.log("stopped mic relay on udp " + port + " (packets=" + received
                    + " dropped=" + dropped + " seq-gaps=" + seqGaps + ")");
        }
    }

    private void receiveLoop(DatagramSocket active) {
        byte[] raw = new byte[MAX_PACKET_SIZE];
        DatagramPacket packet = new DatagramPacket(raw, raw.length);
        while (!active.isClosed()) {
            try {
                packet.setLength(raw.length);
                active.receive(packet);
                parse(raw, packet.getLength());
            } catch (SocketException e) {
                if (!active.isClosed()) {
                    MicRelayLog.log("mic relay socket failed", e);
                }
                break;
            } catch (IOException e) {
                MicRelayLog.log("mic relay receive failed", e);
            } catch (RuntimeException e) {
                MicRelayLog.log("mic relay packet handling failed", e);
            }
        }
    }

    private void parse(byte[] data, int length) {
        if (length < HEADER_SIZE
                || data[0] != 'B' || data[1] != 'M' || data[2] != 'A' || data[3] != '1') {
            drop("short or non-BMA1 packet (" + length + " bytes)");
            return;
        }
        long seq = readUint32(data, 4);
        long sampleRate = readUint32(data, 8);
        int channels = data[12] & 0xFF;
        int bits = data[13] & 0xFF;
        int sampleCount = (data[14] & 0xFF) | ((data[15] & 0xFF) << 8);
        int payload = sampleCount * 2;
        if (length != HEADER_SIZE + payload) {
            drop("length mismatch seq=" + seq + " (expected " + (HEADER_SIZE + payload)
                    + " bytes, got " + length + ")");
            return;
        }
        if (sampleRate != 48000L || channels != 1 || bits != 16) {
            drop("unsupported format seq=" + seq + " (" + sampleRate + " Hz, " + channels
                    + " ch, " + bits + " bit)");
            return;
        }
        if (lastSeq >= 0 && seq != ((lastSeq + 1) & 0xFFFFFFFFL)) {
            seqGaps++;
        }
        lastSeq = seq;
        received++;
        if (payload > 0) {
            buffer.write(data, HEADER_SIZE, payload);
        }
        if ((received % 1000) == 0) {
            MicRelayLog.debug("mic relay stats: packets=" + received + " dropped=" + dropped
                    + " seq-gaps=" + seqGaps + " buffered=" + buffer.available());
        }
    }

    private void drop(String reason) {
        dropped++;
        if (dropped == 1 || (dropped % DROP_LOG_INTERVAL) == 0) {
            MicRelayLog.log("mic relay dropped " + dropped + " packet(s), latest: " + reason);
        }
    }

    private static long readUint32(byte[] data, int off) {
        return (data[off] & 0xFFL)
                | ((data[off + 1] & 0xFFL) << 8)
                | ((data[off + 2] & 0xFFL) << 16)
                | ((data[off + 3] & 0xFFL) << 24);
    }
}
