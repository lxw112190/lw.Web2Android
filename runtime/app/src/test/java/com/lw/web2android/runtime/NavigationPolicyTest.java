package com.lw.web2android.runtime;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import org.junit.Test;

public final class NavigationPolicyTest {
    @Test
    public void httpAndHttpsRemainWebNavigations() {
        assertTrue(NavigationPolicy.isWebScheme("http"));
        assertTrue(NavigationPolicy.isWebScheme("HTTPS"));
        assertFalse(NavigationPolicy.isWebScheme("baiduboxapp"));
        assertFalse(NavigationPolicy.isWebScheme("intent"));
    }

    @Test
    public void externalSummaryKeepsOnlySchemeHostAndPath() {
        assertEquals(
                "scheme=baiduboxapp host=v1 path=/browser/open",
                NavigationPolicy.externalLogSummary(
                        "baiduboxapp", "v1", "/browser/open"));
    }

    @Test
    public void opaqueSchemesDoNotExposeRecipientData() {
        assertEquals("scheme=tel", NavigationPolicy.externalLogSummary("tel", null, null));
        assertEquals("scheme=mailto", NavigationPolicy.externalLogSummary("mailto", null, null));
    }

    @Test
    public void repeatedUnknownSchemesRemainExternalAndStateless() {
        assertFalse(NavigationPolicy.isWebScheme("abc"));
        assertFalse(NavigationPolicy.isWebScheme("xyz"));
        assertFalse(NavigationPolicy.isWebScheme("foo"));
    }

    @Test
    public void externalSummaryRemovesLineBreaksAndBoundsLongPaths() {
        StringBuilder longPath = new StringBuilder("/");
        for (int index = 0; index < 400; ++index) longPath.append('a');
        String summary = NavigationPolicy.externalLogSummary(
                "CUSTOM\r\n", "host\nname", longPath.toString());
        assertTrue(summary.startsWith("scheme=custom host=host name path=/"));
        assertTrue(summary.endsWith("..."));
        assertFalse(summary.contains("\r"));
        assertFalse(summary.contains("\n"));
        assertTrue(summary.length() < 450);
    }

    @Test
    public void downloadPolicyAcceptsOnlyHttpAndHttps() {
        assertTrue(DownloadPolicy.isSupportedScheme("https"));
        assertTrue(DownloadPolicy.isSupportedScheme("HTTP"));
        assertFalse(DownloadPolicy.isSupportedScheme("blob"));
        assertFalse(DownloadPolicy.isSupportedScheme("data"));
        assertFalse(DownloadPolicy.isSupportedScheme("file"));
    }

    @Test
    public void downloadFilenameIsPortableBoundedAndUnique() {
        String first = DownloadPolicy.destinationFileName("../用户:说明?.pdf", 100L);
        String second = DownloadPolicy.destinationFileName("../用户:说明?.pdf", 101L);
        assertEquals("100-_用户_说明_.pdf", first);
        assertEquals("101-_用户_说明_.pdf", second);
        assertFalse(first.equals(second));
        assertTrue(DownloadPolicy.destinationFileName("", 0L).endsWith("download.bin"));
        assertTrue(DownloadPolicy.destinationFileName(
                "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz"
                        + "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz.pdf", 5L).length() <= 140);
    }
}
