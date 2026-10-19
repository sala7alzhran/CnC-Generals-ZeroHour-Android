package org.generalsx.zerohour;

import static org.junit.Assert.assertArrayEquals;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.RandomAccessFile;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import org.junit.Rule;
import org.junit.Test;
import org.junit.rules.TemporaryFolder;

public class BigAudioCompactorTest {

    @Rule public TemporaryFolder tmp = new TemporaryFolder();

    // ---- helpers -------------------------------------------------------------------------------

    /** A 16-bit PCM wav: a sine sweep, so the encoder has real work to do. */
    static byte[] pcmWav(int channels, int rate, int frames) {
        ByteBuffer b = ByteBuffer.allocate(44 + frames * channels * 2).order(ByteOrder.LITTLE_ENDIAN);
        b.put("RIFF".getBytes(StandardCharsets.ISO_8859_1)).putInt(36 + frames * channels * 2)
         .put("WAVE".getBytes(StandardCharsets.ISO_8859_1));
        b.put("fmt ".getBytes(StandardCharsets.ISO_8859_1)).putInt(16)
         .putShort((short) 1).putShort((short) channels).putInt(rate)
         .putInt(rate * channels * 2).putShort((short) (channels * 2)).putShort((short) 16);
        b.put("data".getBytes(StandardCharsets.ISO_8859_1)).putInt(frames * channels * 2);
        for (int i = 0; i < frames; i++) {
            for (int c = 0; c < channels; c++) {
                double f = 200 + 2000.0 * i / frames + c * 50;
                b.putShort((short) (12000 * Math.sin(2 * Math.PI * f * i / rate)));
            }
        }
        return b.array();
    }

    /** Writes a BIGF archive with the given entries (name -> bytes), in order. */
    static File bigFile(File f, Map<String, byte[]> entries) throws Exception {
        int indexLen = 16;
        for (String n : entries.keySet()) indexLen += 8 + n.length() + 1;
        ByteArrayOutputStream data = new ByteArrayOutputStream();
        ByteBuffer index = ByteBuffer.allocate(indexLen);
        index.put("BIGF".getBytes(StandardCharsets.ISO_8859_1));
        int total = indexLen;
        for (byte[] d : entries.values()) total += d.length;
        index.order(ByteOrder.LITTLE_ENDIAN).putInt(total);
        index.order(ByteOrder.BIG_ENDIAN).putInt(entries.size()).putInt(indexLen);
        int off = indexLen;
        for (Map.Entry<String, byte[]> e : entries.entrySet()) {
            index.putInt(off).putInt(e.getValue().length);
            index.put(e.getKey().getBytes(StandardCharsets.ISO_8859_1)).put((byte) 0);
            data.write(e.getValue());
            off += e.getValue().length;
        }
        ByteArrayOutputStream all = new ByteArrayOutputStream();
        all.write(index.array());
        all.write(data.toByteArray());
        Files.write(f.toPath(), all.toByteArray());
        return f;
    }

    /** Reads a BIGF archive back the way StdBIGFileSystem::openArchiveFile does. */
    static Map<String, byte[]> readBig(File f) throws Exception {
        Map<String, byte[]> out = new LinkedHashMap<>();
        try (RandomAccessFile raf = new RandomAccessFile(f, "r")) {
            byte[] magic = new byte[4];
            raf.readFully(magic);
            assertEquals("BIGF", new String(magic, StandardCharsets.ISO_8859_1));
            raf.seek(8);
            int count = raf.readInt();
            raf.seek(16);
            List<long[]> pos = new ArrayList<>();
            List<String> names = new ArrayList<>();
            for (int i = 0; i < count; i++) {
                long off = raf.readInt() & 0xFFFFFFFFL;
                int size = raf.readInt();
                StringBuilder sb = new StringBuilder();
                int c;
                while ((c = raf.read()) > 0) sb.append((char) c);
                pos.add(new long[] { off, size });
                names.add(sb.toString());
            }
            for (int i = 0; i < count; i++) {
                byte[] d = new byte[(int) pos.get(i)[1]];
                raf.seek(pos.get(i)[0]);
                raf.readFully(d);
                out.put(names.get(i), d);
            }
        }
        return out;
    }

    /** Reference IMA ADPCM decoder (the algorithm FFmpeg's adpcm_ima_wav implements). */
    static short[] decodeIma(byte[] wav) {
        ByteBuffer b = ByteBuffer.wrap(wav).order(ByteOrder.LITTLE_ENDIAN);
        int ch = b.getShort(22), align = b.getShort(32), spb = b.getShort(38) & 0xFFFF;
        int dataLen = b.getInt(56);   // RIFF 12 + fmt 28 + fact 12 + "data" tag
        int blocks = dataLen / align;
        int[] steps = {
            7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66,
            73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408,
            449, 494, 544, 598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066,
            2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630,
            9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767 };
        int[] idxTab = { -1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8 };
        short[] out = new short[blocks * spb * ch];
        for (int blk = 0; blk < blocks; blk++) {
            int base = 60 + blk * align;
            int[] pred = new int[ch], idx = new int[ch];
            for (int c = 0; c < ch; c++) {
                pred[c] = b.getShort(base + 4 * c);
                idx[c] = b.get(base + 4 * c + 2);
                out[(blk * spb) * ch + c] = (short) pred[c];
            }
            int p = base + 4 * ch;
            for (int g = 0; g < (spb - 1) / 8; g++) {
                for (int c = 0; c < ch; c++) {
                    for (int k = 0; k < 8; k++) {
                        int byt = b.get(p + k / 2) & 0xFF;
                        int nib = (k & 1) == 0 ? byt & 15 : byt >> 4;
                        int step = steps[idx[c]];
                        int diff = step >> 3;
                        if ((nib & 4) != 0) diff += step;
                        if ((nib & 2) != 0) diff += step >> 1;
                        if ((nib & 1) != 0) diff += step >> 2;
                        pred[c] = Math.max(-32768, Math.min(32767, (nib & 8) != 0 ? pred[c] - diff : pred[c] + diff));
                        idx[c] = Math.max(0, Math.min(88, idx[c] + idxTab[nib]));
                        out[(blk * spb + 1 + g * 8 + k) * ch + c] = (short) pred[c];
                    }
                    p += 4;
                }
            }
        }
        return out;
    }

    static double snrDb(byte[] pcmWav, short[] decoded, int channels) {
        ImaAdpcmWav.PcmWav pcm = ImaAdpcmWav.parsePcm16(pcmWav);
        ByteBuffer b = ByteBuffer.wrap(pcmWav, pcm.dataOffset, pcm.dataLength).order(ByteOrder.LITTLE_ENDIAN);
        int n = pcm.dataLength / 2;
        double sig = 0, noise = 0;
        for (int i = 0; i < n; i++) {
            int ref = b.getShort();
            sig += (double) ref * ref;
            double d = ref - decoded[i];
            noise += d * d;
        }
        return 10 * Math.log10(sig / Math.max(noise, 1));
    }

    // ---- tests ---------------------------------------------------------------------------------

    @Test public void encodesMonoWithTheRetailHeaderLayout() {
        byte[] in = pcmWav(1, 22050, 5000);
        byte[] out = ImaAdpcmWav.encode(in, ImaAdpcmWav.parsePcm16(in));
        ByteBuffer b = ByteBuffer.wrap(out).order(ByteOrder.LITTLE_ENDIAN);
        assertEquals(0x11, b.getShort(20));            // WAVE_FORMAT_IMA_ADPCM
        assertEquals(1, b.getShort(22));
        assertEquals(22050, b.getInt(24));
        assertEquals(512, b.getShort(32));             // block align, as in the retail files
        assertEquals(4, b.getShort(34));
        assertEquals(1017, b.getShort(38));            // samples per block
        assertEquals(5000, b.getInt(48));              // fact: real sample count
        assertEquals(0, (out.length - 60) % 512);
        assertTrue(out.length < in.length / 3);
    }

    @Test public void monoDecodesCloseToTheOriginal() {
        byte[] in = pcmWav(1, 22050, 20000);
        byte[] out = ImaAdpcmWav.encode(in, ImaAdpcmWav.parsePcm16(in));
        assertTrue(snrDb(in, decodeIma(out), 1) > 20);
    }

    @Test public void stereoDecodesCloseToTheOriginal() {
        byte[] in = pcmWav(2, 44100, 30000);
        byte[] out = ImaAdpcmWav.encode(in, ImaAdpcmWav.parsePcm16(in));
        ByteBuffer b = ByteBuffer.wrap(out).order(ByteOrder.LITTLE_ENDIAN);
        assertEquals(2048, b.getShort(32));
        assertEquals(2041, b.getShort(38));
        assertTrue(snrDb(in, decodeIma(out), 2) > 20);
    }

    @Test public void leavesOtherWavFormatsAlone() {
        byte[] eightBit = pcmWav(1, 22050, 100);
        eightBit[34] = 8;   // bits per sample
        assertNull(ImaAdpcmWav.parsePcm16(eightBit));
        byte[] ima = ImaAdpcmWav.encode(pcmWav(1, 22050, 100), ImaAdpcmWav.parsePcm16(pcmWav(1, 22050, 100)));
        assertNull(ImaAdpcmWav.parsePcm16(ima));
        assertNull(ImaAdpcmWav.parsePcm16("not a wav".getBytes(StandardCharsets.ISO_8859_1)));
    }

    @Test public void compactsAnArchiveAndKeepsEverythingElse() throws Exception {
        Map<String, byte[]> entries = new LinkedHashMap<>();
        entries.put("Data\\Audio\\Sounds\\a.wav", pcmWav(1, 22050, 9000));
        entries.put("Data\\INI\\GameData.ini", "Weapon = Big\n".getBytes(StandardCharsets.ISO_8859_1));
        entries.put("Data\\Audio\\Sounds\\b.wav", pcmWav(2, 44100, 4000));
        entries.put("Data\\Audio\\Tracks\\m.mp3", new byte[] { (byte) 0xFF, (byte) 0xFB, 1, 2, 3 });
        File in = bigFile(tmp.newFile("Audio.big"), entries);
        File out = tmp.newFile("Audio.out.big");

        assertTrue(BigAudioCompactor.worthCompacting(in));
        assertTrue(BigAudioCompactor.compact(in, out, null));
        assertTrue(out.length() < in.length() / 2);

        Map<String, byte[]> got = readBig(out);
        assertEquals(new ArrayList<>(entries.keySet()), new ArrayList<>(got.keySet()));
        assertArrayEquals(entries.get("Data\\INI\\GameData.ini"), got.get("Data\\INI\\GameData.ini"));
        assertArrayEquals(entries.get("Data\\Audio\\Tracks\\m.mp3"), got.get("Data\\Audio\\Tracks\\m.mp3"));
        assertNull(ImaAdpcmWav.parsePcm16(got.get("Data\\Audio\\Sounds\\a.wav")));   // now ADPCM
        assertTrue(snrDb(entries.get("Data\\Audio\\Sounds\\a.wav"), decodeIma(got.get("Data\\Audio\\Sounds\\a.wav")), 1) > 20);

        // A second pass has nothing left to do.
        assertFalse(BigAudioCompactor.worthCompacting(out));
    }

    @Test public void rejectsFilesThatAreNotArchives() throws Exception {
        File f = tmp.newFile("notbig.big");
        Files.write(f.toPath(), "hello world, not an archive".getBytes(StandardCharsets.ISO_8859_1));
        assertFalse(BigAudioCompactor.worthCompacting(f));
        assertFalse(BigAudioCompactor.compact(f, tmp.newFile("x"), null));
        assertNotNull(f);
    }
}
