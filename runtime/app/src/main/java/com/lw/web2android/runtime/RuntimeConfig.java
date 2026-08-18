package com.lw.web2android.runtime;

import android.content.Context;
import android.net.Uri;

import org.json.JSONException;
import org.json.JSONObject;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.util.Locale;

final class RuntimeConfig {
    static final int SUPPORTED_SCHEMA_VERSION = 1;

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

    private RuntimeConfig(
            Mode mode,
            String runtimeVersion,
            String entry,
            String url,
            boolean fullscreen,
            Orientation orientation,
            boolean allowHttp) {
        this.mode = mode;
        this.runtimeVersion = runtimeVersion;
        this.entry = entry;
        this.url = url;
        this.fullscreen = fullscreen;
        this.orientation = orientation;
        this.allowHttp = allowHttp;
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

            return new RuntimeConfig(
                    mode,
                    runtimeVersion,
                    entry,
                    url,
                    json.optBoolean("fullscreen", false),
                    parseOrientation(json.optString("orientation", "auto")),
                    allowHttp);
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
}
