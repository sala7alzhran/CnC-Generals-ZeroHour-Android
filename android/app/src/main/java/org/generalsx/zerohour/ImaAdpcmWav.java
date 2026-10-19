package org.generalsx.zerohour;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;

/**
 * @port Android — re-encodes an uncompressed 16-bit PCM .wav as Microsoft IMA ADPCM
 * (WAVE_FORMAT_IMA_ADPCM, 4 bits per sample), the format Zero Hour already ships most of its
 * speech in. The engine decodes both through FFmpeg, so the game plays the result unchanged
 * while the file is about a quarter of the size.
 *
 * Plain Java (no Android types) so it can be unit-tested on the build machine.
 */
final class ImaAdpcmWav {

    private static final int WAVE_FORMAT_PCM = 1;
    private static final int WAVE_FORMAT_IMA_ADPCM = 0x11;

    private static final int[] STEP_TABLE = {
        7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45,
        50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307,
        337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552,
        1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484,
        7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385,
        24623, 27086, 29794, 32767
    };
    private static final int[] INDEX_TABLE = { -1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8 };

    private ImaAdpcmWav() {}

    /** The parts of a .wav the encoder needs, or null if it is not one it handles. */
    static final class PcmWav {
        final int channels, sampleRate, dataOffset, dataLength;
        PcmWav(int channels, int sampleRate, int dataOffset, int dataLength) {
            this.channels = channels; this.sampleRate = sampleRate;
            this.dataOffset = dataOffset; this.dataLength = dataLength;
        }
    }

    /** Parses {@code wav} and returns it if it is 16-bit PCM, mono or stereo; otherwise null. */
    static PcmWav parsePcm16(byte[] wav) {
        if (wav.length < 12 || !tag(wav, 0, "RIFF") || !tag(wav, 8, "WAVE")) return null;
        ByteBuffer b = ByteBuffer.wrap(wav).order(ByteOrder.LITTLE_ENDIAN);
        int channels = 0, rate = 0, bits = 0, format = -1;
        int pos = 12;
        while (pos + 8 <= wav.length) {
            int size = b.getInt(pos + 4);
            if (size < 0) return null;
            int body = pos + 8;
            if (tag(wav, pos, "fmt ")) {
                if (size < 16 || body + 16 > wav.length) return null;
                format = b.getShort(body) & 0xFFFF;
                channels = b.getShort(body + 2) & 0xFFFF;
                rate = b.getInt(body + 4);
                bits = b.getShort(body + 14) & 0xFFFF;
            } else if (tag(wav, pos, "data")) {
                if (format != WAVE_FORMAT_PCM || bits != 16 || (channels != 1 && channels != 2) || rate <= 0) {
                    return null;
                }
                int len = Math.min(size, wav.length - body);
                len -= len % (2 * channels);
                return len > 0 ? new PcmWav(channels, rate, body, len) : null;
            }
            long next = (long) body + size + (size & 1);   // chunks are word aligned
            if (next > wav.length) return null;
            pos = (int) next;
        }
        return null;
    }

    /** Block size the way Microsoft's encoder picks it: 256 bytes per channel per 11025 Hz. */
    static int blockAlign(int channels, int sampleRate) {
        int perChannel = 256 * Math.max(1, sampleRate / 11025);
        return perChannel * channels;
    }

    static int samplesPerBlock(int channels, int blockAlign) {
        return (blockAlign - 4 * channels) * 8 / (4 * channels) + 1;
    }

    /** Encodes {@code pcm} (a 16-bit PCM wav) as an IMA ADPCM wav. */
    static byte[] encode(byte[] wav, PcmWav pcm) {
        final int ch = pcm.channels;
        final int frames = pcm.dataLength / (2 * ch);
        final int align = blockAlign(ch, pcm.sampleRate);
        final int spb = samplesPerBlock(ch, align);
        final int blocks = (frames + spb - 1) / spb;
        final int dataLen = blocks * align;

        ByteBuffer in = ByteBuffer.wrap(wav, pcm.dataOffset, pcm.dataLength).order(ByteOrder.LITTLE_ENDIAN);
        short[] samples = new short[frames * ch];
        in.asShortBuffer().get(samples);

        // RIFF header + fmt (20 bytes) + fact (4 bytes) + data
        final int headerLen = 12 + (8 + 20) + (8 + 4) + 8;
        ByteBuffer out = ByteBuffer.allocate(headerLen + dataLen).order(ByteOrder.LITTLE_ENDIAN);
        out.put(ascii("RIFF")).putInt(headerLen - 8 + dataLen).put(ascii("WAVE"));
        out.put(ascii("fmt ")).putInt(20)
           .putShort((short) WAVE_FORMAT_IMA_ADPCM).putShort((short) ch)
           .putInt(pcm.sampleRate)
           .putInt((int) Math.round((double) pcm.sampleRate * align / spb))
           .putShort((short) align).putShort((short) 4)
           .putShort((short) 2).putShort((short) spb);
        out.put(ascii("fact")).putInt(4).putInt(frames);
        out.put(ascii("data")).putInt(dataLen);

        int[] predictor = new int[ch];
        int[] index = new int[ch];
        byte[] block = new byte[align];
        for (int blk = 0; blk < blocks; blk++) {
            int first = blk * spb;
            // Header per channel: the first sample verbatim, the step index, a zero byte.
            for (int c = 0; c < ch; c++) {
                int s = sampleAt(samples, frames, ch, first, c);
                predictor[c] = s;
                block[4 * c] = (byte) s;
                block[4 * c + 1] = (byte) (s >> 8);
                block[4 * c + 2] = (byte) index[c];
                block[4 * c + 3] = 0;
            }
            // Body: 8 samples per channel per 4-byte group, channels interleaved group by group,
            // low nibble first.
            int pos = 4 * ch;
            for (int group = 0; group < (spb - 1) / 8; group++) {
                for (int c = 0; c < ch; c++) {
                    for (int k = 0; k < 4; k++) {
                        int f = first + 1 + group * 8 + k * 2;
                        int lo = encodeNibble(sampleAt(samples, frames, ch, f, c), predictor, index, c);
                        int hi = encodeNibble(sampleAt(samples, frames, ch, f + 1, c), predictor, index, c);
                        block[pos++] = (byte) (lo | (hi << 4));
                    }
                }
            }
            out.put(block, 0, align);
        }
        return out.array();
    }

    /** Sample {@code frame} of channel {@code c}; past the end the last sample is repeated. */
    private static int sampleAt(short[] s, int frames, int ch, int frame, int c) {
        if (frames == 0) return 0;
        if (frame >= frames) frame = frames - 1;
        return s[frame * ch + c];
    }

    private static int encodeNibble(int sample, int[] predictor, int[] index, int c) {
        int step = STEP_TABLE[index[c]];
        int diff = sample - predictor[c];
        int nibble = 0;
        if (diff < 0) { nibble = 8; diff = -diff; }
        int vpdiff = step >> 3;
        if (diff >= step) { nibble |= 4; diff -= step; vpdiff += step; }
        step >>= 1;
        if (diff >= step) { nibble |= 2; diff -= step; vpdiff += step; }
        step >>= 1;
        if (diff >= step) { nibble |= 1; vpdiff += step; }
        int p = (nibble & 8) != 0 ? predictor[c] - vpdiff : predictor[c] + vpdiff;
        predictor[c] = Math.max(-32768, Math.min(32767, p));
        index[c] = Math.max(0, Math.min(88, index[c] + INDEX_TABLE[nibble]));
        return nibble;
    }

    private static boolean tag(byte[] b, int at, String t) {
        return at + 4 <= b.length && b[at] == t.charAt(0) && b[at + 1] == t.charAt(1)
            && b[at + 2] == t.charAt(2) && b[at + 3] == t.charAt(3);
    }

    private static byte[] ascii(String s) {
        byte[] r = new byte[s.length()];
        for (int i = 0; i < r.length; i++) r[i] = (byte) s.charAt(i);
        return r;
    }
}
