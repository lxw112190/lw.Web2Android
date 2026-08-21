package com.lw.web2android.runtime;

import java.nio.ByteBuffer;
import java.nio.CharBuffer;
import java.nio.charset.CharacterCodingException;
import java.nio.charset.Charset;
import java.nio.charset.CodingErrorAction;
import java.nio.charset.StandardCharsets;

/** Strict, bounded text decoding shared by the Runtime receiver and local unit tests. */
final class ExternalTextDecoder {
    enum Failure {
        OVERSIZE,
        BINARY,
        UNSUPPORTED_ENCODING
    }

    static final class DecodeException extends Exception {
        final Failure failure;

        DecodeException(Failure failure) {
            super(failure.name());
            this.failure = failure;
        }
    }

    static final class Result {
        final String text;
        final String encoding;

        Result(String text, String encoding) {
            this.text = text;
            this.encoding = encoding;
        }
    }

    private ExternalTextDecoder() {}

    static Result decode(byte[] bytes, long maxBytes) throws DecodeException {
        if (bytes.length > maxBytes) throw new DecodeException(Failure.OVERSIZE);
        if (startsWith(bytes, 0xef, 0xbb, 0xbf)) {
            return decode(bytes, 3, StandardCharsets.UTF_8, "utf-8");
        }
        if (startsWith(bytes, 0xff, 0xfe)) {
            return decode(bytes, 2, StandardCharsets.UTF_16LE, "utf-16le");
        }
        if (startsWith(bytes, 0xfe, 0xff)) {
            return decode(bytes, 2, StandardCharsets.UTF_16BE, "utf-16be");
        }
        if (looksBinary(bytes)) throw new DecodeException(Failure.BINARY);
        return decode(bytes, 0, StandardCharsets.UTF_8, "utf-8");
    }

    private static Result decode(byte[] bytes, int offset, Charset charset, String encoding)
            throws DecodeException {
        try {
            CharBuffer decoded = charset.newDecoder()
                    .onMalformedInput(CodingErrorAction.REPORT)
                    .onUnmappableCharacter(CodingErrorAction.REPORT)
                    .decode(ByteBuffer.wrap(bytes, offset, bytes.length - offset));
            String text = decoded.toString();
            if (text.indexOf('\0') >= 0) throw new DecodeException(Failure.BINARY);
            return new Result(text, encoding);
        } catch (CharacterCodingException error) {
            throw new DecodeException(Failure.UNSUPPORTED_ENCODING);
        }
    }

    private static boolean startsWith(byte[] bytes, int... prefix) {
        if (bytes.length < prefix.length) return false;
        for (int index = 0; index < prefix.length; ++index) {
            if ((bytes[index] & 0xff) != prefix[index]) return false;
        }
        return true;
    }

    private static boolean looksBinary(byte[] bytes) {
        int inspected = Math.min(bytes.length, 4096);
        int controls = 0;
        for (int index = 0; index < inspected; ++index) {
            int value = bytes[index] & 0xff;
            if (value == 0) return true;
            if ((value < 0x20 && value != '\t' && value != '\n' && value != '\r') || value == 0x7f) {
                ++controls;
            }
        }
        return inspected > 0 && controls * 10 > inspected * 3;
    }
}
