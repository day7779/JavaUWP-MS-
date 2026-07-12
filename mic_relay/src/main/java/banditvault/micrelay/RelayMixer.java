package banditvault.micrelay;

import java.util.concurrent.CopyOnWriteArrayList;
import javax.sound.sampled.AudioFormat;
import javax.sound.sampled.Control;
import javax.sound.sampled.DataLine;
import javax.sound.sampled.Line;
import javax.sound.sampled.LineEvent;
import javax.sound.sampled.LineListener;
import javax.sound.sampled.LineUnavailableException;
import javax.sound.sampled.Mixer;
import javax.sound.sampled.TargetDataLine;

final class RelayMixer implements Mixer {
    static final Mixer.Info INFO = new RelayMixerInfo();
    static final DataLine.Info TARGET_LINE_INFO =
            new DataLine.Info(TargetDataLine.class, RelayTargetDataLine.FORMAT);

    private static final RelayMixer INSTANCE = new RelayMixer();
    private static final Line.Info MIXER_LINE_INFO = new Line.Info(Mixer.class);
    private static final Line.Info[] NO_LINE_INFOS = new Line.Info[0];
    private static final Line[] NO_LINES = new Line[0];
    private static final Control[] NO_CONTROLS = new Control[0];

    private final RelayTargetDataLine targetLine = new RelayTargetDataLine();
    private final CopyOnWriteArrayList<LineListener> listeners = new CopyOnWriteArrayList<>();

    private volatile boolean open;

    private RelayMixer() {
    }

    static RelayMixer getInstance() {
        return INSTANCE;
    }

    @Override
    public Mixer.Info getMixerInfo() {
        return INFO;
    }

    @Override
    public Line.Info[] getSourceLineInfo() {
        return NO_LINE_INFOS.clone();
    }

    @Override
    public Line.Info[] getTargetLineInfo() {
        return new Line.Info[]{TARGET_LINE_INFO};
    }

    @Override
    public Line.Info[] getSourceLineInfo(Line.Info info) {
        return NO_LINE_INFOS.clone();
    }

    @Override
    public Line.Info[] getTargetLineInfo(Line.Info info) {
        if (isLineSupported(info)) {
            return new Line.Info[]{TARGET_LINE_INFO};
        }
        return NO_LINE_INFOS.clone();
    }

    @Override
    public boolean isLineSupported(Line.Info info) {
        if (info == null || !TargetDataLine.class.isAssignableFrom(info.getLineClass())) {
            return false;
        }
        if (!(info instanceof DataLine.Info dataInfo)) {
            return true;
        }
        AudioFormat[] formats = dataInfo.getFormats();
        if (formats == null || formats.length == 0) {
            return true;
        }
        for (AudioFormat format : formats) {
            if (format == null || RelayTargetDataLine.isFormatCompatible(format)) {
                return true;
            }
        }
        return false;
    }

    @Override
    public Line getLine(Line.Info info) throws LineUnavailableException {
        if (!isLineSupported(info)) {
            throw new IllegalArgumentException("unsupported line: " + info);
        }
        return targetLine;
    }

    @Override
    public int getMaxLines(Line.Info info) {
        return isLineSupported(info) ? 1 : 0;
    }

    @Override
    public Line[] getSourceLines() {
        return NO_LINES.clone();
    }

    @Override
    public Line[] getTargetLines() {
        if (targetLine.isOpen()) {
            return new Line[]{targetLine};
        }
        return NO_LINES.clone();
    }

    @Override
    public void synchronize(Line[] lines, boolean maintainSync) {
        throw new IllegalArgumentException("line synchronization not supported");
    }

    @Override
    public void unsynchronize(Line[] lines) {
        throw new IllegalArgumentException("line synchronization not supported");
    }

    @Override
    public boolean isSynchronizationSupported(Line[] lines, boolean maintainSync) {
        return false;
    }

    @Override
    public Line.Info getLineInfo() {
        return MIXER_LINE_INFO;
    }

    @Override
    public void open() throws LineUnavailableException {
        boolean fireOpen = false;
        synchronized (this) {
            if (!open) {
                open = true;
                fireOpen = true;
            }
        }
        if (fireOpen) {
            fire(LineEvent.Type.OPEN);
        }
    }

    @Override
    public void close() {
        targetLine.close();
        boolean fireClose = false;
        synchronized (this) {
            if (open) {
                open = false;
                fireClose = true;
            }
        }
        if (fireClose) {
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
        LineEvent event = new LineEvent(this, type, 0L);
        for (LineListener listener : listeners) {
            try {
                listener.update(event);
            } catch (RuntimeException e) {
                MicRelayLog.log("mixer listener failed", e);
            }
        }
    }

    private static final class RelayMixerInfo extends Mixer.Info {
        private RelayMixerInfo() {
            super(RelayConfig.name(), "BanditVault", "Relay virtual microphone (network)", "1.0");
        }
    }
}
