package com.lw.web2android.runtime;

import android.content.ContentResolver;
import android.content.Intent;
import android.database.Cursor;
import android.net.Uri;
import android.provider.OpenableColumns;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.util.Locale;

/** Converts one explicit Android share/open Intent into a bounded text payload. */
final class ExternalContentReceiver {
    static final class Result {
        final ExternalContentPayload payload;
        final String userMessage;

        private Result(ExternalContentPayload payload, String userMessage) {
            this.payload = payload;
            this.userMessage = userMessage;
        }

        static Result accepted(ExternalContentPayload payload) {
            return new Result(payload, null);
        }

        static Result rejected(String userMessage) {
            return new Result(null, userMessage);
        }

        static Result ignored() {
            return new Result(null, null);
        }
    }

    private static final class Metadata {
        String displayName = "";
        long declaredSize = -1L;
    }

    private final ContentResolver resolver;
    private final RuntimeConfig.ExternalContent config;
    private final ExternalContentPolicy policy;

    ExternalContentReceiver(ContentResolver resolver, RuntimeConfig.ExternalContent config) {
        this.resolver = resolver;
        this.config = config;
        this.policy = new ExternalContentPolicy(config);
    }

    Result receive(Intent intent) {
        if (intent == null || !config.enabled) return Result.ignored();
        String action = intent.getAction();
        if (Intent.ACTION_MAIN.equals(action)) return Result.ignored();
        if (Intent.ACTION_SEND_MULTIPLE.equals(action)) {
            RuntimeLog.warn("External content rejected; reason=multiple-files-unsupported");
            return Result.rejected("暂不支持一次打开多个文件");
        }
        RuntimeLog.info("External intent received; action=" + actionName(action));
        if (Intent.ACTION_SEND.equals(action)) {
            Uri stream = intent.getParcelableExtra(Intent.EXTRA_STREAM);
            if (stream != null) return receiveFile(stream, intent.getType(), "share");
            if (!config.receiveSharedText) return rejectUnsupported();
            CharSequence sharedText = intent.getCharSequenceExtra(Intent.EXTRA_TEXT);
            if (sharedText == null) return rejectUnsupported();
            return receiveText(sharedText.toString(), intent.getType());
        }
        if (Intent.ACTION_VIEW.equals(action)) {
            return receiveFile(intent.getData(), intent.getType(), "view");
        }
        return Result.ignored();
    }

    private Result receiveText(String text, String rawMimeType) {
        byte[] bytes = text.getBytes(StandardCharsets.UTF_8);
        if (bytes.length > config.maxTextBytes) return rejectOversize();
        String mimeType = normalizeMime(rawMimeType);
        if (mimeType.isEmpty()) mimeType = "text/plain";
        ExternalContentPayload payload = new ExternalContentPayload(
                ExternalContentPayload.Kind.TEXT, "share", "", "", mimeType,
                bytes.length, "utf-8", text);
        RuntimeLog.info("External content accepted; kind=text; mime=" + mimeType
                + "; extension=; bytes=" + bytes.length);
        return Result.accepted(payload);
    }

    private Result receiveFile(Uri uri, String intentMimeType, String sourceAction) {
        if (!config.openFiles || uri == null || !"content".equalsIgnoreCase(uri.getScheme())) {
            return rejectUnsupported();
        }
        Metadata metadata = queryMetadata(uri);
        String mimeType = normalizeMime(intentMimeType);
        if (mimeType.isEmpty()) mimeType = resolveMimeType(uri);
        if (!policy.accepts(metadata.displayName, mimeType)) return rejectUnsupported();
        if (metadata.declaredSize > config.maxTextBytes) return rejectOversize();
        try {
            byte[] bytes = readBounded(uri);
            ExternalTextDecoder.Result decoded = ExternalTextDecoder.decode(bytes, config.maxTextBytes);
            String extension = ExternalContentPolicy.extensionOf(metadata.displayName);
            ExternalContentPayload payload = new ExternalContentPayload(
                    ExternalContentPayload.Kind.FILE, sourceAction, metadata.displayName,
                    extension, mimeType, bytes.length, decoded.encoding, decoded.text);
            RuntimeLog.info("External content accepted; kind=file; mime=" + safeMime(mimeType)
                    + "; extension=" + extension + "; bytes=" + bytes.length);
            return Result.accepted(payload);
        } catch (ExternalTextDecoder.DecodeException error) {
            switch (error.failure) {
                case OVERSIZE: return rejectOversize();
                case BINARY:
                    RuntimeLog.warn("External file rejected; reason=binary-content");
                    return Result.rejected("文件内容不是有效文本");
                case UNSUPPORTED_ENCODING:
                default:
                    RuntimeLog.warn("External file rejected; reason=unsupported-encoding");
                    return Result.rejected("暂不支持该文本编码");
            }
        } catch (IOException | SecurityException error) {
            RuntimeLog.warn("External file rejected; reason=read-failed; exception="
                    + error.getClass().getSimpleName());
            return Result.rejected("无法读取所选文件");
        }
    }

    private Metadata queryMetadata(Uri uri) {
        Metadata metadata = new Metadata();
        try (Cursor cursor = resolver.query(uri,
                new String[]{OpenableColumns.DISPLAY_NAME, OpenableColumns.SIZE},
                null, null, null)) {
            if (cursor != null && cursor.moveToFirst()) {
                int nameColumn = cursor.getColumnIndex(OpenableColumns.DISPLAY_NAME);
                int sizeColumn = cursor.getColumnIndex(OpenableColumns.SIZE);
                if (nameColumn >= 0 && !cursor.isNull(nameColumn)) {
                    metadata.displayName = basename(cursor.getString(nameColumn));
                }
                if (sizeColumn >= 0 && !cursor.isNull(sizeColumn)) {
                    metadata.declaredSize = cursor.getLong(sizeColumn);
                }
            }
        } catch (RuntimeException error) {
            RuntimeLog.debug("External metadata unavailable; exception="
                    + error.getClass().getSimpleName());
        }
        if (metadata.displayName.isEmpty()) metadata.displayName = basename(uri.getLastPathSegment());
        return metadata;
    }

    private String resolveMimeType(Uri uri) {
        try {
            return normalizeMime(resolver.getType(uri));
        } catch (RuntimeException error) {
            RuntimeLog.debug("External MIME type unavailable; exception="
                    + error.getClass().getSimpleName());
            return "";
        }
    }

    private byte[] readBounded(Uri uri) throws IOException, ExternalTextDecoder.DecodeException {
        try (InputStream input = resolver.openInputStream(uri)) {
            if (input == null) throw new IOException("Content provider returned no stream");
            ByteArrayOutputStream output = new ByteArrayOutputStream();
            byte[] buffer = new byte[8192];
            long total = 0L;
            int read;
            while ((read = input.read(buffer)) != -1) {
                total += read;
                if (total > config.maxTextBytes) {
                    throw new ExternalTextDecoder.DecodeException(ExternalTextDecoder.Failure.OVERSIZE);
                }
                output.write(buffer, 0, read);
            }
            return output.toByteArray();
        }
    }

    private Result rejectUnsupported() {
        RuntimeLog.warn("External file rejected; reason=unsupported-type");
        return Result.rejected("不支持此文件类型");
    }

    private Result rejectOversize() {
        RuntimeLog.warn("External file rejected; reason=oversize");
        return Result.rejected("文件过大，无法作为文本打开");
    }

    private static String normalizeMime(String value) {
        if (value == null) return "";
        String normalized = value.trim().toLowerCase(Locale.ROOT);
        int parameter = normalized.indexOf(';');
        return parameter < 0 ? normalized : normalized.substring(0, parameter).trim();
    }

    private static String safeMime(String value) {
        return value.isEmpty() ? "unknown" : value;
    }

    private static String basename(String value) {
        if (value == null) return "";
        String clean = value.replace('\r', ' ').replace('\n', ' ').trim();
        int slash = Math.max(clean.lastIndexOf('/'), clean.lastIndexOf('\\'));
        if (slash >= 0) clean = clean.substring(slash + 1);
        return clean.length() <= 256 ? clean : clean.substring(clean.length() - 256);
    }

    private static String actionName(String action) {
        if (Intent.ACTION_VIEW.equals(action)) return "VIEW";
        if (Intent.ACTION_SEND.equals(action)) return "SEND";
        if (Intent.ACTION_SEND_MULTIPLE.equals(action)) return "SEND_MULTIPLE";
        return "OTHER";
    }
}
