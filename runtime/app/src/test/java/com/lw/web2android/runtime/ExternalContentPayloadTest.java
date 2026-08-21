package com.lw.web2android.runtime;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import org.json.JSONObject;
import org.junit.Test;

public final class ExternalContentPayloadTest {
    @Test
    public void payloadUsesSafeJsonEscapingAndKeepsUnicode() throws Exception {
        String text = "中文\n\"quoted\"\\path</script>";
        ExternalContentPayload payload = new ExternalContentPayload(
                ExternalContentPayload.Kind.FILE, "view", "配置.json", "json",
                "application/json", text.getBytes("UTF-8").length, "utf-8", text);
        String serialized = payload.toJsonString();
        JSONObject json = new JSONObject(serialized);
        assertEquals(1, json.getInt("schemaVersion"));
        assertEquals("file", json.getString("kind"));
        assertEquals("配置.json", json.getString("name"));
        assertEquals(text, json.getString("text"));
        assertFalse(serialized.contains("content://"));
        assertTrue(serialized.contains("\\n"));
        assertTrue(serialized.contains("\\\"quoted\\\""));
    }
}
