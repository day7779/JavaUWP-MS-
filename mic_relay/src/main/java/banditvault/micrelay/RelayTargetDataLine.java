package banditvault.micrelay;

import java.util.Arrays;
import java.util.Objects;
import java.util.concurrent.CopyOnWriteArrayList;
import java.util.concurrent.atomic.AtomicLong;
import javax.sound.sampled.AudioFormat;
import javax.sound.sampled.AudioSystem;
import javax.sound.sampled.Control;
import javax.sound.sampled.DataLine;
import javax.sound.sampled.LineEvent;
import javax.sound.sampled.LineListener;
import javax.sound.sampled.LineUnavailableException;
import javax.sound.sampled.TargetDataLine;

final class RelayTargetDataLine implements TargetDataLine {
    static final AudioFormat FORMAT = new AudioFormat(48000.0f, 16, 1, true, false);

    private static final int FRAME_SIZE = 2;
    private static final Control[] NO_CONTROLS = new Control[0];

    private final Object stateLock = new Object();
    private final CopyOnWriteArrayList<LineListener> listeners = new CopyOnWriteArrayList<>();
    private final AtomicLong framePosition = new AtomicLong();

    private volatile RelayAudioReceiver receiver;
    private volatile boolean open;
    private volatile boolean running;
    private volatile int bufferSize = RelayConfig.bufferBytes();

    static boolean isFormatCompatible(AudioFormat format) {
        if (format == null || !AudioFormat.Encoding.PCM_SIGNED.equals(format.getEncoding())) {
            return false;
        }
        if (!matches(format.getSampleRate(), FORMAT.getSampleRate())
                || !matches(format.getFrameRate(), FORMAT.getFrameRate())) {
            return false;
        }
        if (!matches(format.getSampleSizeInBits(), FORMAT.getSampleSizeInBits())
                || !matches(format.getChannels(), FORMAT.getChannels())
                || !matches(format.getFrameSize(), FORMAT.getFrameSize())) {
            return false;
        }
        return !format.isBigEndian();
    }

    private static boolean matches(int requested, int actual) {
        return requested == AudioSystem.NOT_SPECIFIED || requested == actual;
    }

    private static boolean matches(float requested, float actual) {
        return requested == AudioSystem.NOT_SPECIFIED || Float.compare(requested, actual) == 0;
    }

    @Override
    public void open() throws LineUnavailableException {
        open(FORMAT, RelayConfig.bufferBytes());
    }

    @Override
    public void open(AudioFormat format) throws LineUnavailableException {
        open(format, RelayConfig.bufferBytes());
    }

    @Override
    public void open(AudioFormat format, int requestedBufferSize) throws LineUnavailableException {
        if (!isFormatCompatible(format)) {
            throw new LineUnavailableException("unsupported format: " + format);
        }
        boolean fireOpen = false;
        synchronized (stateLock) {
            if (!open) {
                RelayAudioReceiver attached = RelayAudioReceiver.forPort(RelayConfig.port());
                attached.attach();
                receiver = attached;
                bufferSize = RelayConfig.bufferBytes();
                framePosition.set(0L);
                running = false;
                open = true;
                fireOpen = true;
            }
        }
        if (fireOpen) {
            MicRelayLog.log("capture line opened (udp " + RelayConfig.port() + ")");
            fire(LineEvent.Type.OPEN);
        }
    }

    @Override
    public void start() {
        boolean fireStart = false;
        synchronized (stateLock) {
            if (open && !running) {
                running = true;
                fireStart = true;
            }
        }
        if (fireStart) {
            fire(LineEvent.Type.START);
        }
    }

    @Override
    public void stop() {
        boolean fireStop = false;
        synchronized (stateLock) {
            if (running) {
                running = false;
                fireStop = true;
            }
        }
        if (fireStop) {
            fire(LineEvent.Type.STOP);
        }
    }

    @Override
    public int read(byte[] data, int offset, int length) {
        Objects.requireNonNull(data, "data");
        Objects.checkFromIndexSize(offset, length, data.length);
        if ((length % FRAME_SIZE) != 0) {
            throw new IllegalArgumentException(
                    "read length must be a multiple of the 2-byte frame size: " + length);
        }
        if (length == 0 || !running) {
            return 0;
        }
        RelayAudioReceiver current = receiver;
        int copied = 0;
        if (current != null) {
            ByteRingBuffer ring = current.buffer();
            long deadlineNanos = System.nanoTime() + RelayConfig.underrunMs() * 1_000_000L;
            while (copied < length && running) {
                long remainingMs = (deadlineNanos - System.nanoTime()) / 1_000_000L;
                int count = ring.read(data, offset + copied, length - copied,
                        remainingMs > 0 ? (int) remainingMs : 0);
                if (count <= 0) {
                    break;
                }
                copied += count;
            }
        }
        if (copied < length) {
            Arrays.fill(data, offset + copied, offset + length, (byte) 0);
        }
        framePosition.addAndGet(length / FRAME_SIZE);
        return length;
    }

    @Override
    public void drain() {
        RelayAudioReceiver current = receiver;
        if (current == null) {
            return;
        }
        ByteRingBuffer ring = current.buffer();
        long deadline = System.currentTimeMillis() + 200;
        while (ring.available() > 0 && System.currentTimeMillis() < deadline) {
            try {
                Thread.sleep(2);
            } catch (InterruptedException e) {
                Thread.currentThread().interrupt();
                return;
            }
        }
    }

    @Override
    public void flush() {
        RelayAudioReceiver current = receiver;
        if (current != null) {
            current.buffer().clear();
        }
    }

    @Override
    public AudioFormat getFormat() {
        return FORMAT;
    }

    @Override
    public int getBufferSize() {
        return bufferSize;
    }

    @Override
    public int available() {
        RelayAudioReceiver current = receiver;
        if (current == null) {
            return 0;
        }
        return current.buffer().available() & ~(FRAME_SIZE - 1);
    }

    @Override
    public int getFramePosition() {
        return (int) framePosition.get();
    }

    @Override
    public long getLongFramePosition() {
        return framePosition.get();
    }

    @Override
    public long getMicrosecondPosition() {
        long frames = framePosition.get();
        long seconds = frames / 48000L;
        long remainder = frames % 48000L;
        return seconds * 1_000_000L + remainder * 1_000_000L / 48000L;
    }

    @Override
    public float getLevel() {
        return -1.0f;
    }

    @Override
    public boolean isRunning() {
        return running;
    }

    @Override
    public boolean isActive() {
        return open && running;
    }

    @Override
    public DataLine.Info getLineInfo() {
        return RelayMixer.TARGET_LINE_INFO;
    }

    @Override
    public void close() {
        RelayAudioReceiver detached;
        boolean fireStop;
        boolean fireClose;
        synchronized (stateLock) {
            if (!open) {
                return;
            }
            fireStop = running;
            running = false;
            open = false;
            detached = receiver;
            receiver = null;
            fireClose = true;
        }
        if (fireStop) {
            fire(LineEvent.Type.STOP);
        }
        if (detached != null) {
            detached.detach();
        }
        if (fireClose) {
            MicRelayLog.log("capture line closed");
            fire(LineEvent.Type.CLOSE);
        }
    }

    @Override
    public boolean isOpen() {
        return open;
    }

    @Override
    public Control[] getControls() {
        return NO_CONTROLS.clone();
    }

    @Override
    public boolean isControlSupported(Control.Type control) {
        return false;
    }

    @Override
    public Control getControl(Control.Type control) {
        throw new IllegalArgumentException("no controls supported");
    }

    @Override
    public void addLineListener(LineListener listener) {
        if (listener != null) {
            listeners.addIfAbsent(listener);
        }
    }

    @Override
    public void removeLineListener(LineListener listener) {
        listeners.remove(listener);
    }

    private void fire(LineEvent.Type type) {
        LineEvent event = new LineEvent(this, type, framePosition.get());
        for (LineListener listener : listeners) {
            try {
                listener.update(event);
            } catch (RuntimeException e) {
                MicRelayLog.log("capture line listener failed", e);
            }
        }
    }
}
