/*
 * ART 补丁：Android 运行时完全不带 javax.sound（core-oj / core-libart / framework
 * 里都没有），但 MC 26.2 的声音系统用 javax.sound.sampled.AudioFormat 描述音频格式：
 *   - JOrbisAudioStream 构造：new AudioFormat(rate, 16, channels, true, false)
 *   - OpenAlUtil.audioFormatToOpenAL 按 getEncoding()/getChannels()/getSampleSizeInBits()
 *     映射到 AL_FORMAT_*
 *   - Channel.calculateBufferSize 用 getSampleRate()/getChannels()/getSampleSizeInBits()
 *
 * 缺这个类时，音频流会在解码阶段就抛 NoClassDefFoundError，
 * 表现为 MC 日志里一直有 "Playing sound ..." 却完全没声音，
 * 同时 FallbackResourceManager 报 "Leaked resource: ...ogg"（流没能走到 close）。
 *
 * 这里只实现 MC 用到的子集（外加几个常规访问器），语义与 JDK 版一致。
 */
package javax.sound.sampled;

public class AudioFormat {
    /** 采样编码。JDK 里是个可扩展类，这里只需要 PCM 两种。 */
    public static class Encoding {
        public static final Encoding PCM_SIGNED = new Encoding("PCM_SIGNED");
        public static final Encoding PCM_UNSIGNED = new Encoding("PCM_UNSIGNED");

        private final String name;

        protected Encoding(String name) {
            this.name = name;
        }

        public final String toString() {
            return this.name;
        }

        @Override
        public final boolean equals(Object o) {
            if (this == o) {
                return true;
            }
            return o instanceof Encoding && this.name.equals(((Encoding) o).name);
        }

        @Override
        public final int hashCode() {
            return this.name.hashCode();
        }
    }

    private final Encoding encoding;
    private final float sampleRate;
    private final int sampleSizeInBits;
    private final int channels;
    private final float frameRate;
    private final boolean bigEndian;

    public AudioFormat(float sampleRate, int sampleSizeInBits, int channels,
                       boolean signed, boolean bigEndian) {
        this(signed ? Encoding.PCM_SIGNED : Encoding.PCM_UNSIGNED,
             sampleRate, sampleSizeInBits, channels,
             channels > 0 && sampleRate > 0 ? sampleRate / channels : AudioSystem_NOT_SPECIFIED,
             bigEndian);
    }

    /** JDK 里是 AudioSystem.NOT_SPECIFIED，这里避免额外依赖，直接用 -1 表示未指定。 */
    private static final float AudioSystem_NOT_SPECIFIED = -1.0f;

    public AudioFormat(Encoding encoding, float sampleRate, int sampleSizeInBits, int channels,
                       float frameRate, boolean bigEndian) {
        if (encoding == null) {
            throw new IllegalArgumentException("encoding is null");
        }
        this.encoding = encoding;
        this.sampleRate = sampleRate;
        this.sampleSizeInBits = sampleSizeInBits;
        this.channels = channels;
        this.frameRate = frameRate;
        this.bigEndian = bigEndian;
    }

    public Encoding getEncoding() {
        return this.encoding;
    }

    public float getSampleRate() {
        return this.sampleRate;
    }

    public int getSampleSizeInBits() {
        return this.sampleSizeInBits;
    }

    public int getChannels() {
        return this.channels;
    }

    public float getFrameRate() {
        return this.frameRate;
    }

    public boolean isBigEndian() {
        return this.bigEndian;
    }

    public int getFrameSize() {
        return (this.sampleSizeInBits + 7) / 8 * this.channels;
    }

    public boolean matches(AudioFormat format) {
        if (format == null) {
            return false;
        }
        return this.encoding.equals(format.encoding)
            && this.channels == format.channels
            && this.sampleSizeInBits == format.sampleSizeInBits
            && this.sampleRate == format.sampleRate
            && this.bigEndian == format.bigEndian;
    }

    @Override
    public String toString() {
        return this.encoding + " " + this.sampleRate + " Hz, " + this.sampleSizeInBits
            + " bit, " + (this.channels == 1 ? "mono" : this.channels + " channel")
            + (this.channels > 1 ? "s" : "") + ", "
            + (this.bigEndian ? "big-endian" : "little-endian");
    }
}
