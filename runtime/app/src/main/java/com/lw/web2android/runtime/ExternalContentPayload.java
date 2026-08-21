package com.lw.web2android.runtime;

import org.json.JSONException;
import org.json.JSONObject;

/** Stable one-way Native-to-Web payload. It intentionally contains no content URI. */
final class ExternalContentPayload {
    enum Kind {
        TEXT("text"),
        FILE("file");

        final String wireValue;

        Kind(String wireValue) {
            this.wireValue = wireValue;
        }
    }

    final Kind kind;
    final String sourceAction;
    final String name;
    final String extension;
    final String mimeType;
    final long size;
    final String encoding;
    final String text;

    ExternalContentPayload(
            Kind kind,
            String sourceAction,
            String name,
            String extension,
            String mimeType,
            long size,
            String encoding,
            String text) {
        this.kind = kind;
        this.sourceAction = safe(sourceAction);
        this.name = safe(name);
        this.extension = safe(extension);
        this.mimeType = safe(mimeType);
        this.size = size;
        this.encoding = safe(encoding);
        this.text = safe(text);
    }

    String toJsonString() {
        try {
            JSONObject json = new JSONObject();
            json.put("schemaVersion", 1);
            json.put("kind", kind.wireValue);
            json.put("sourceAction", sourceAction);
            json.put("name", name);
            json.put("extension", extension);
            json.put("mimeType", mimeType);
            json.put("size", size);
            json.put("encoding", encoding);
            json.put("text", text);
            return json.toString();
        } catch (JSONException error) {
            throw new IllegalStateException("Unable to serialize external content payload", error);
        }
    }

    private static String safe(String value) {
        return value == null ? "" : value;
    }
}
