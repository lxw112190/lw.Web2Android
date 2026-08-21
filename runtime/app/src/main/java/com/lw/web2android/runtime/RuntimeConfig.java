package com.lw.web2android.runtime;

import android.content.Context;
import android.net.Uri;

import org.json.JSONException;
import org.json.JSONArray;
import org.json.JSONObject;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.util.Collections;
import java.util.LinkedHashSet;
import java.util.Locale;
import java.util.Set;

final class RuntimeConfig {
    static final int SUPPORTED_SCHEMA_VERSION = 2;
    private static final long MIN_EXTERNAL_TEXT_BYTES = 64L * 1024L;
    private static final long MAX_EXTERNAL_TEXT_BYTES = 32L * 1024L * 1024L;

    enum Mode {
        LOCAL,
        REMOTE
    }

    enum Orientation {
        AUTO,
        PORTRAIT,
        LANDSCAPE
    }

    final Mode mode;
    final String runtimeVersion;
    final String entry;
    final String url;
    final boolean fullscreen;
    final Orientation orientation;
    final boolean allowHttp;
    final ExternalContent externalContent;

    private RuntimeConfig(
            Mode mode,
            String runtimeVersion,
            String entry,
            String url,
            boolean fullscreen,
            Orientation orientation,
            boolean allowHttp,
            ExternalContent externalContent) {
        this.mode = mode;
        this.runtimeVersion = runtimeVersion;
        this.entry = entry;
        this.url = url;
        this.fullscreen = fullscreen;
        this.orientation = orientation;
        this.allowHttp = allowHttp;
        this.externalContent = externalContent;
    }

    static RuntimeConfig load(Context context) throws ConfigException {
        try (InputStream input = context.getAssets().open("lw-config.json")) {
            JSONObject json = new JSONObject(readUtf8(input));
            int schemaVersion = json.optInt("schemaVersion", -1);
            if (schemaVersion != SUPPORTED_SCHEMA_VERSION) {
                throw new ConfigException("Unsupported config schemaVersion: " + schemaVersion);
            }

            Mode mode = parseMode(json.optString("mode", ""));
            String runtimeVersion = json.optString("runtimeVersion", "unknown").trim();
            if (runtimeVersion.isEmpty() || runtimeVersion.length() > 64) {
                throw new ConfigException("runtimeVersion is invalid");
            }
            boolean allowHttp = json.optBoolean("allowHttp", false);
            String entry = json.optString("entry", "index.html").trim();
            String url = json.optString("url", "").trim();

            if (mode == Mode.LOCAL) {
                validateEntry(entry);
            } else {
                validateRemoteUrl(url, allowHttp);
            }
            ExternalContent externalContent = parseExternalContent(
                    json.optJSONObject("externalContent"), mode);

            return new RuntimeConfig(
                    mode,
                    runtimeVersion,
                    entry,
                    url,
                    json.optBoolean("fullscreen", false),
                    parseOrientation(json.optString("orientation", "auto")),
                    allowHttp,
                    externalContent);
        } catch (IOException | JSONException error) {
            throw new ConfigException("Unable to read assets/lw-config.json", error);
        }
    }

    String startUrl() {
        if (mode == Mode.REMOTE) {
            return url;
        }
        return "https://appassets.androidplatform.net/assets/" + Uri.encode(entry, "/");
    }

    boolean allowsNavigation(Uri uri) {
        String scheme = normalized(uri.getScheme());
        if ("https".equals(scheme)) {
            return true;
        }
        return "http".equals(scheme) && allowHttp;
    }

    private static Mode parseMode(String rawMode) throws ConfigException {
        String mode = normalized(rawMode);
        if ("local".equals(mode)) {
            return Mode.LOCAL;
        }
        if ("remote".equals(mode)) {
            return Mode.REMOTE;
        }
        throw new ConfigException("mode must be 'local' or 'remote'");
    }

    private static Orientation parseOrientation(String rawOrientation) throws ConfigException {
        String orientation = normalized(rawOrientation);
        if ("auto".equals(orientation)) {
            return Orientation.AUTO;
        }
        if ("portrait".equals(orientation)) {
            return Orientation.PORTRAIT;
        }
        if ("landscape".equals(orientation)) {
            return Orientation.LANDSCAPE;
        }
        throw new ConfigException("orientation must be 'auto', 'portrait', or 'landscape'");
    }

    private static void validateEntry(String entry) throws ConfigException {
        if (entry.isEmpty() || entry.startsWith("/") || entry.startsWith("\\")) {
            throw new ConfigException("entry must be a relative asset path");
        }
        if (entry.indexOf('\\') >= 0 || entry.indexOf('?') >= 0
                || entry.indexOf('#') >= 0 || entry.indexOf('\0') >= 0) {
            throw new ConfigException("entry must use forward slashes and contain no query, fragment, or NUL characters");
        }
        for (String segment : entry.split("/")) {
            if (segment.isEmpty() || ".".equals(segment) || "..".equals(segment)) {
                throw new ConfigException("entry contains an invalid path segment");
            }
        }
    }

    private static void validateRemoteUrl(String url, boolean allowHttp) throws ConfigException {
        Uri uri = Uri.parse(url);
        String scheme = normalized(uri.getScheme());
        if (uri.getHost() == null || uri.getHost().isEmpty()) {
            throw new ConfigException("remote url must contain a host");
        }
        if ("https".equals(scheme)) {
            return;
        }
        if ("http".equals(scheme) && allowHttp) {
            return;
        }
        if ("http".equals(scheme)) {
            throw new ConfigException("remote HTTP url requires allowHttp=true");
        }
        throw new ConfigException("remote url must use HTTPS or explicitly allowed HTTP");
    }

    private static String normalized(String value) {
        return value == null ? "" : value.trim().toLowerCase(Locale.ROOT);
    }

    private static ExternalContent parseExternalContent(JSONObject json, Mode mode)
            throws ConfigException, JSONException {
        if (json == null) return ExternalContent.disabled();
        boolean enabled = json.optBoolean("enabled", false);
        boolean receiveSharedText = json.optBoolean("receiveSharedText", false);
        boolean openFiles = json.optBoolean("openFiles", false);
        boolean acceptOctetStream = json.optBoolean("acceptOctetStream", false);
        long maxTextBytes = json.optLong("maxTextBytes", 8L * 1024L * 1024L);
        if (enabled && mode != Mode.LOCAL) {
            throw new ConfigException("External content integration requires local mode");
        }
        if (enabled && !receiveSharedText && !openFiles) {
            throw new ConfigException("externalContent enables no receive capability");
        }
        if (maxTextBytes < MIN_EXTERNAL_TEXT_BYTES || maxTextBytes > MAX_EXTERNAL_TEXT_BYTES) {
            throw new ConfigException("externalContent.maxTextBytes is outside the supported range");
        }
        Set<String> extensions = readStringSet(json.optJSONArray("extensions"), "extensions");
        Set<String> fileNames = readStringSet(json.optJSONArray("fileNames"), "fileNames");
        Set<String> mimeTypes = readStringSet(json.optJSONArray("mimeTypes"), "mimeTypes");
        for (String extension : extensions) {
            if (!extension.matches("[a-z0-9][a-z0-9._+\\-]*") || extension.startsWith(".")
                    || "..".equals(extension) || extension.indexOf('/') >= 0
                    || extension.indexOf('\\') >= 0) {
                throw new ConfigException("externalContent contains an invalid extension");
            }
        }
        for (String fileName : fileNames) {
            if (fileName.isEmpty() || ".".equals(fileName) || "..".equals(fileName)
                    || fileName.indexOf('/') >= 0 || fileName.indexOf('\\') >= 0) {
                throw new ConfigException("externalContent contains an invalid file name");
            }
        }
        for (String mimeType : mimeTypes) {
            if ("*/*".equals(mimeType)
                    || !mimeType.matches("[a-z0-9!#$&^_.+\\-]+/([a-z0-9!#$&^_.+\\-]+|\\*)")) {
                throw new ConfigException("externalContent contains an invalid MIME type");
            }
        }
        return new ExternalContent(enabled, receiveSharedText, openFiles, acceptOctetStream,
                maxTextBytes, extensions, fileNames, mimeTypes);
    }

    private static Set<String> readStringSet(JSONArray array, String property)
            throws ConfigException, JSONException {
        if (array == null) return Collections.emptySet();
        LinkedHashSet<String> values = new LinkedHashSet<>();
        for (int index = 0; index < array.length(); ++index) {
            Object item = array.get(index);
            if (!(item instanceof String)) {
                throw new ConfigException("externalContent." + property + " must contain strings");
            }
            String value = normalized((String) item);
            if (value.isEmpty()) {
                throw new ConfigException("externalContent." + property + " contains an empty value");
            }
            values.add(value);
        }
        return Collections.unmodifiableSet(values);
    }

    private static String readUtf8(InputStream input) throws IOException {
        ByteArrayOutputStream output = new ByteArrayOutputStream();
        byte[] buffer = new byte[4096];
        int read;
        while ((read = input.read(buffer)) != -1) {
            output.write(buffer, 0, read);
        }
        return new String(output.toByteArray(), StandardCharsets.UTF_8);
    }

    static final class ConfigException extends Exception {
        ConfigException(String message) {
            super(message);
        }

        ConfigException(String message, Throwable cause) {
            super(message, cause);
        }
    }

    static final class ExternalContent {
        final boolean enabled;
        final boolean receiveSharedText;
        final boolean openFiles;
        final boolean acceptOctetStream;
        final long maxTextBytes;
        final Set<String> extensions;
        final Set<String> fileNames;
        final Set<String> mimeTypes;

        ExternalContent(
                boolean enabled,
                boolean receiveSharedText,
                boolean openFiles,
                boolean acceptOctetStream,
                long maxTextBytes,
                Set<String> extensions,
                Set<String> fileNames,
                Set<String> mimeTypes) {
            this.enabled = enabled;
            this.receiveSharedText = receiveSharedText;
            this.openFiles = openFiles;
            this.acceptOctetStream = acceptOctetStream;
            this.maxTextBytes = maxTextBytes;
            this.extensions = extensions;
            this.fileNames = fileNames;
            this.mimeTypes = mimeTypes;
        }

        static ExternalContent disabled() {
            return new ExternalContent(false, false, false, false, 8L * 1024L * 1024L,
                    Collections.emptySet(), Collections.emptySet(), Collections.emptySet());
        }
    }
}
