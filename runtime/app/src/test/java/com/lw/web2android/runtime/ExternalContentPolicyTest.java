package com.lw.web2android.runtime;

import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import org.junit.Test;

import java.util.Arrays;
import java.util.HashSet;

public final class ExternalContentPolicyTest {
    @Test
    public void textConfigAcceptsExpectedTextAndConfigFiles() {
        ExternalContentPolicy policy = policy(false,
                new String[]{"txt", "json", "yaml", "yml", "env"},
                new String[]{"dockerfile", ".env"},
                new String[]{"text/*", "application/json", "application/yaml"});
        assertTrue(policy.accepts("config.yaml", "application/yaml"));
        assertTrue(policy.accepts("config.yml", "text/plain"));
        assertTrue(policy.accepts("Dockerfile", "text/plain"));
        assertTrue(policy.accepts(".env", "text/plain"));
        assertFalse(policy.accepts("image.png", "image/png"));
        assertFalse(policy.accepts("archive.zip", "application/zip"));
        assertFalse(policy.accepts("unknown.bin", "application/octet-stream"));
    }

    @Test
    public void octetStreamRequiresExplicitOptInAndAllowedExtension() {
        ExternalContentPolicy policy = policy(true,
                new String[]{"yaml"}, new String[0], new String[]{"text/*"});
        assertTrue(policy.accepts("unknown.yaml", "application/octet-stream"));
        assertFalse(policy.accepts("unknown.bin", "application/octet-stream"));
    }

    @Test
    public void codeConfigAcceptsShellScripts() {
        ExternalContentPolicy policy = policy(false,
                new String[]{"sh", "vue"}, new String[0], new String[]{"text/*"});
        assertTrue(policy.accepts("deploy.sh", "text/plain"));
        assertFalse(policy.accepts("Editor.vue", "application/octet-stream"));
        assertTrue(policy.accepts("Editor.vue", "text/plain"));
    }

    private static ExternalContentPolicy policy(
            boolean acceptOctetStream, String[] extensions, String[] fileNames, String[] mimeTypes) {
        RuntimeConfig.ExternalContent config = new RuntimeConfig.ExternalContent(
                true, true, true, acceptOctetStream, 8L * 1024L * 1024L,
                new HashSet<>(Arrays.asList(extensions)),
                new HashSet<>(Arrays.asList(fileNames)),
                new HashSet<>(Arrays.asList(mimeTypes)));
        return new ExternalContentPolicy(config);
    }
}
