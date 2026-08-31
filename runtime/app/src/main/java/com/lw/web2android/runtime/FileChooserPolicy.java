package com.lw.web2android.runtime;

import java.util.Locale;

/** Resolves HTML file-input requests without depending on an Activity. */
final class FileChooserPolicy {
    private FileChooserPolicy() {}

    static boolean shouldCaptureImage(
            boolean captureEnabled,
            boolean multiple,
            String[] acceptTypes) {
        if (!captureEnabled || multiple || acceptTypes == null) return false;
        boolean foundImageType = false;
        for (String group : acceptTypes) {
            if (group == null) continue;
            for (String rawType : group.split(",")) {
                String type = rawType.trim().toLowerCase(Locale.ROOT);
                if (type.isEmpty()) continue;
                if (!type.startsWith("image/")) return false;
                foundImageType = true;
            }
        }
        return foundImageType;
    }
}
