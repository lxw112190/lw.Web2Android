package com.lw.web2android.runtime;

import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import org.junit.Test;

public final class FileChooserPolicyTest {
    @Test
    public void imageCaptureUsesCamera() {
        assertTrue(FileChooserPolicy.shouldCaptureImage(
                true, false, new String[] {"image/*"}));
        assertTrue(FileChooserPolicy.shouldCaptureImage(
                true, false, new String[] {" image/jpeg, image/png "}));
    }

    @Test
    public void ordinaryImageInputUsesPicker() {
        assertFalse(FileChooserPolicy.shouldCaptureImage(
                false, false, new String[] {"image/*"}));
    }

    @Test
    public void multipleOrMixedInputUsesPicker() {
        assertFalse(FileChooserPolicy.shouldCaptureImage(
                true, true, new String[] {"image/*"}));
        assertFalse(FileChooserPolicy.shouldCaptureImage(
                true, false, new String[] {"image/*", "video/*"}));
        assertFalse(FileChooserPolicy.shouldCaptureImage(
                true, false, new String[] {"*/*"}));
        assertFalse(FileChooserPolicy.shouldCaptureImage(
                true, false, new String[] {""}));
    }
}
