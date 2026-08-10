package banditvault.micrelay;

import javax.sound.sampled.Mixer;
import javax.sound.sampled.spi.MixerProvider;

public final class RelayMixerProvider extends MixerProvider {
    @Override
    public Mixer.Info[] getMixerInfo() {
        if (!RelayConfig.enabled()) {
            return new Mixer.Info[0];
        }
        return new Mixer.Info[]{RelayMixer.INFO};
    }

    @Override
    public Mixer getMixer(Mixer.Info info) {
        if (!RelayConfig.enabled() || (info != null && !RelayMixer.INFO.equals(info))) {
            throw new IllegalArgumentException("mixer not supported: " + info);
        }
        return RelayMixer.getInstance();
    }

    @Override
    public boolean isMixerSupported(Mixer.Info info) {
        return RelayConfig.enabled() && (info == null || RelayMixer.INFO.equals(info));
    }
}
