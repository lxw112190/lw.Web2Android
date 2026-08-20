package com.lw.web2android.runtime;

import android.content.pm.ActivityInfo;

final class FullscreenOrientationPolicy {
    private FullscreenOrientationPolicy() {}

    static int resolve(int appRequestedOrientation) {
        switch (appRequestedOrientation) {
            case ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE:
            case ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE:
            case ActivityInfo.SCREEN_ORIENTATION_USER_LANDSCAPE:
            case ActivityInfo.SCREEN_ORIENTATION_REVERSE_LANDSCAPE:
                return ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE;
            default:
                return ActivityInfo.SCREEN_ORIENTATION_SENSOR;
        }
    }

    static String requestedOrientationName(int value) {
        switch (value) {
            case ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE: return "landscape";
            case ActivityInfo.SCREEN_ORIENTATION_PORTRAIT: return "portrait";
            case ActivityInfo.SCREEN_ORIENTATION_SENSOR: return "sensor";
            case ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE: return "sensorLandscape";
            case ActivityInfo.SCREEN_ORIENTATION_SENSOR_PORTRAIT: return "sensorPortrait";
            case ActivityInfo.SCREEN_ORIENTATION_USER: return "user";
            case ActivityInfo.SCREEN_ORIENTATION_USER_LANDSCAPE: return "userLandscape";
            case ActivityInfo.SCREEN_ORIENTATION_REVERSE_LANDSCAPE: return "reverseLandscape";
            case ActivityInfo.SCREEN_ORIENTATION_REVERSE_PORTRAIT: return "reversePortrait";
            case ActivityInfo.SCREEN_ORIENTATION_UNSPECIFIED: return "unspecified";
            default: return "value(" + value + ")";
        }
    }
}
