package banditvault.micrelay;

import java.util.Objects;

final class ByteRingBuffer {
    private final byte[] storage;

    private int head;
    private int size;

    ByteRingBuffer(int capacity) {
        if (capacity <= 0) {
            throw new IllegalArgumentException("capacity must be positive: " + capacity);
        }
        storage = new byte[capacity];
    }

    synchronized void write(byte[] src, int off, int len) {
        Objects.requireNonNull(src, "src");
        Objects.checkFromIndexSize(off, len, src.length);
        if (len == 0) {
            return;
        }
        if (len >= storage.length) {
            System.arraycopy(src, off + len - storage.length, storage, 0, storage.length);
            head = 0;
            size = storage.length;
            notifyAll();
            return;
        }
        int overflow = size + len - storage.length;
        if (overflow > 0) {
            head = (head + overflow) % storage.length;
            size -= overflow;
        }
        int tail = (head + size) % storage.length;
        int first = Math.min(len, storage.length - tail);
        System.arraycopy(src, off, storage, tail, first);
        if (len > first) {
            System.arraycopy(src, off + first, storage, 0, len - first);
        }
        size += len;
        notifyAll();
    }

    synchronized int read(byte[] dst, int off, int len, int timeoutMs) {
        Objects.requireNonNull(dst, "dst");
        Objects.checkFromIndexSize(off, len, dst.length);
        if (len == 0) {
            return 0;
        }
        if (size == 0 && timeoutMs > 0) {
            long deadline = System.currentTimeMillis() + timeoutMs;
            while (size == 0) {
                long remaining = deadline - System.currentTimeMillis();
                if (remaining <= 0) {
                    break;
                }
                try {
                    wait(remaining);
                } catch (InterruptedException e) {
                    Thread.currentThread().interrupt();
                    return 0;
                }
            }
        }
        if (size == 0) {
            return 0;
        }
        int count = Math.min(len, size);
        int first = Math.min(count, storage.length - head);
        System.arraycopy(storage, head, dst, off, first);
        if (count > first) {
            System.arraycopy(storage, 0, dst, off + first, count - first);
        }
        head = (head + count) % storage.length;
        size -= count;
        if (size == 0) {
            head = 0;
        }
        return count;
    }

    synchronized int available() {
        return size;
    }

    synchronized void clear() {
        head = 0;
        size = 0;
        notifyAll();
    }
}
