package com.lw.web2android.runtime;

import android.annotation.TargetApi;
import android.app.Activity;
import android.os.Build;
import android.window.OnBackInvokedCallback;
import android.window.OnBackInvokedDispatcher;

/** Isolates API-33 types from the minSdk-23 Activity. No new runtime dependency/resources. */
final class SystemBackRegistration {
    private SystemBackRegistration() {}

    static BackNavigationController.Registration create(Activity activity, Runnable action) {
        if (Build.VERSION.SDK_INT >= 33) return new Api33(activity, action);
        return enabled -> { /* Activity.onBackPressed handles API 23-32. */ };
    }

    @TargetApi(33)
    private static final class Api33 implements BackNavigationController.Registration {
        private final OnBackInvokedDispatcher dispatcher;
        private final OnBackInvokedCallback callback;

        Api33(Activity activity, Runnable action) {
            dispatcher = activity.getOnBackInvokedDispatcher();
            callback = action::run;
        }

        @Override public void setEnabled(boolean enabled) {
            if (enabled) {
                dispatcher.registerOnBackInvokedCallback(OnBackInvokedDispatcher.PRIORITY_DEFAULT, callback);
            } else {
                dispatcher.unregisterOnBackInvokedCallback(callback);
            }
        }
    }
}
