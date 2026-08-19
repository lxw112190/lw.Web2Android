package com.lw.web2android.runtime;

import java.util.Locale;

/** Pure navigation classification and privacy-safe external URI logging. */
final class NavigationPolicy {
    private static final int MAX_SCHEME_LENGTH = 32;
    private static final int MAX_HOST_LENGTH = 128;
    private static final int MAX_PATH_LENGTH = 256;

    private NavigationPolicy() {}

    static boolean isWebScheme(String scheme) {
        return "http".equalsIgnoreCase(scheme) || "https".equalsIgnoreCase(scheme);
    }

    static String externalLogSummary(String scheme, String host, String encodedPath) {
        StringBuilder summary = new StringBuilder();
        summary.append("scheme=").append(clean(scheme, MAX_SCHEME_LENGTH).toLowerCase(Locale.US));
        String safeHost = clean(host, MAX_HOST_LENGTH);
        if (!safeHost.isEmpty()) summary.append(" host=").append(safeHost);
        String safePath = clean(encodedPath, MAX_PATH_LENGTH);
        if (!safePath.isEmpty()) summary.append(" path=").append(safePath);
        return summary.toString();
    }

    private static String clean(String value, int maxLength) {
        if (value == null || value.isEmpty()) return "";
        String cleaned = value.replace('\r', ' ').replace('\n', ' ').replace('\t', ' ').trim();
        if (cleaned.length() <= maxLength) return cleaned;
        int end = maxLength;
        if (end > 0 && Character.isHighSurrogate(cleaned.charAt(end - 1))) --end;
        return cleaned.substring(0, end) + "...";
    }
}
