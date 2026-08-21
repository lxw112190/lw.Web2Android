package com.lw.web2android.runtime;

import java.util.Locale;

/** Pure filename/MIME allow-list policy for external text content. */
final class ExternalContentPolicy {
    private final RuntimeConfig.ExternalContent config;

    ExternalContentPolicy(RuntimeConfig.ExternalContent config) {
        this.config = config;
    }

    boolean accepts(String displayName, String rawMimeType) {
        if (!config.enabled || !config.openFiles) return false;
        String name = normalize(displayName);
        String mimeType = normalizeMime(rawMimeType);
        boolean nameAllowed = config.fileNames.contains(name);
        String extension = extensionOf(name);
        boolean extensionAllowed = !extension.isEmpty() && config.extensions.contains(extension);
        if ("application/octet-stream".equals(mimeType)) {
            return config.acceptOctetStream && (nameAllowed || extensionAllowed);
        }
        return nameAllowed || extensionAllowed || matchesMime(mimeType);
    }

    static String extensionOf(String displayName) {
        String name = normalize(displayName);
        int slash = Math.max(name.lastIndexOf('/'), name.lastIndexOf('\\'));
        if (slash >= 0) name = name.substring(slash + 1);
        int dot = name.lastIndexOf('.');
        return dot > 0 && dot + 1 < name.length() ? name.substring(dot + 1) : "";
    }

    private boolean matchesMime(String mimeType) {
        if (mimeType.isEmpty()) return false;
        if (config.mimeTypes.contains(mimeType)) return true;
        int slash = mimeType.indexOf('/');
        return slash > 0 && config.mimeTypes.contains(mimeType.substring(0, slash) + "/*");
    }

    private static String normalizeMime(String value) {
        String normalized = normalize(value);
        int parameter = normalized.indexOf(';');
        return parameter < 0 ? normalized : normalized.substring(0, parameter).trim();
    }

    private static String normalize(String value) {
        return value == null ? "" : value.trim().toLowerCase(Locale.ROOT);
    }
}
