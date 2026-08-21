package com.lw.web2android.runtime;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.fail;

import org.junit.Test;

import java.nio.charset.StandardCharsets;

public final class ExternalTextDecoderTest {
    @Test
    public void decodesUtf8AndBomVariants() throws Exception {
        assertDecoded("中文", "utf-8", "中文".getBytes(StandardCharsets.UTF_8));
        assertDecoded("中文", "utf-8", concat(new byte[]{(byte) 0xef, (byte) 0xbb, (byte) 0xbf},
                "中文".getBytes(StandardCharsets.UTF_8)));
        assertDecoded("中文", "utf-16le", concat(new byte[]{(byte) 0xff, (byte) 0xfe},
                "中文".getBytes(StandardCharsets.UTF_16LE)));
        assertDecoded("中文", "utf-16be", concat(new byte[]{(byte) 0xfe, (byte) 0xff},
                "中文".getBytes(StandardCharsets.UTF_16BE)));
    }

    @Test
    public void rejectsInvalidUtf8BinaryAndOversize() throws Exception {
        assertFailure(new byte[]{(byte) 0xc3, 0x28}, 1024,
                ExternalTextDecoder.Failure.UNSUPPORTED_ENCODING);
        assertFailure(new byte[]{'a', 0, 'b'}, 1024, ExternalTextDecoder.Failure.BINARY);
        assertFailure("too large".getBytes(StandardCharsets.UTF_8), 2,
                ExternalTextDecoder.Failure.OVERSIZE);
    }

    private static void assertDecoded(String expected, String encoding, byte[] bytes) throws Exception {
        ExternalTextDecoder.Result result = ExternalTextDecoder.decode(bytes, 1024);
        assertEquals(expected, result.text);
        assertEquals(encoding, result.encoding);
    }

    private static void assertFailure(byte[] bytes, long maxBytes, ExternalTextDecoder.Failure expected)
            throws Exception {
        try {
            ExternalTextDecoder.decode(bytes, maxBytes);
            fail("Expected decoding failure");
        } catch (ExternalTextDecoder.DecodeException error) {
            assertEquals(expected, error.failure);
        }
    }

    private static byte[] concat(byte[] first, byte[] second) {
        byte[] result = new byte[first.length + second.length];
        System.arraycopy(first, 0, result, 0, first.length);
        System.arraycopy(second, 0, result, first.length, second.length);
        return result;
    }
}
