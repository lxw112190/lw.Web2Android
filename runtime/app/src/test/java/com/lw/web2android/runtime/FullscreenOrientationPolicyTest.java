package com.lw.web2android.runtime;

import static org.junit.Assert.assertEquals;

import android.content.pm.ActivityInfo;

import org.junit.Test;

public final class FullscreenOrientationPolicyTest {
    @Test
    public void landscapeFamilyStaysLandscape() {
        assertEquals(ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE,
                FullscreenOrientationPolicy.resolve(ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE));
        assertEquals(ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE,
                FullscreenOrientationPolicy.resolve(ActivityInfo.SCREEN_ORIENTATION_USER_LANDSCAPE));
        assertEquals(ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE,
                FullscreenOrientationPolicy.resolve(ActivityInfo.SCREEN_ORIENTATION_REVERSE_LANDSCAPE));
    }

    @Test
    public void otherModesUseSensor() {
        assertEquals(ActivityInfo.SCREEN_ORIENTATION_SENSOR,
                FullscreenOrientationPolicy.resolve(ActivityInfo.SCREEN_ORIENTATION_PORTRAIT));
        assertEquals(ActivityInfo.SCREEN_ORIENTATION_SENSOR,
                FullscreenOrientationPolicy.resolve(ActivityInfo.SCREEN_ORIENTATION_SENSOR_PORTRAIT));
        assertEquals(ActivityInfo.SCREEN_ORIENTATION_SENSOR,
                FullscreenOrientationPolicy.resolve(ActivityInfo.SCREEN_ORIENTATION_USER));
        assertEquals(ActivityInfo.SCREEN_ORIENTATION_SENSOR,
                FullscreenOrientationPolicy.resolve(ActivityInfo.SCREEN_ORIENTATION_UNSPECIFIED));
    }
}
