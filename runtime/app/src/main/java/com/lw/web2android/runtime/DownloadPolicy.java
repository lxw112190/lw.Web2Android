package com.lw.web2android.runtime;

/** Pure download URL classification and portable destination filename handling. */
final class DownloadPolicy {
    private static final int MAX_FILE_NAME_LENGTH = 120;

    private DownloadPolicy() {}

    static boolean isSupportedScheme(String scheme) {
        return "http".equalsIgnoreCase(scheme) || "https".equalsIgnoreCase(scheme);
    }

    static String destinationFileName(String guessedName, long requestNonce) {
        String source = guessedName == null ? "" : guessedName.trim();
        StringBuilder cleaned = new StringBuilder();
        for (int index = 0; index < source.length(); ++index) {
            char character = source.charAt(index);
            boolean forbidden = character < 0x20 || character == 0x7f ||
                    character == '\\' || character == '/' || character == ':' ||
                    character == '*' || character == '?' || character == '"' ||
                    character == '<' || character == '>' || character == '|';
            cleaned.append(forbidden ? '_' : character);
        }
        String name = cleaned.toString();
        while (name.startsWith(".")) name = name.substring(1);
        if (name.isEmpty()) name = "download.bin";
        if (name.length() > MAX_FILE_NAME_LENGTH) {
            int extensionStart = name.lastIndexOf('.');
            String extension = extensionStart > 0 && name.length() - extensionStart <= 16
                    ? name.substring(extensionStart)
                    : "";
            int end = MAX_FILE_NAME_LENGTH - extension.length();
            if (end > 0 && Character.isHighSurrogate(name.charAt(end - 1))) --end;
            name = name.substring(0, end) + extension;
        }
        return Long.toString(Math.max(0L, requestNonce)) + "-" + name;
    }
}
