package com.lw.web2android.runtime;

/** One navigation decision per back event; independent of Android for regression tests. */
final class BackNavigationController {
    interface Host {
        boolean fullscreen();
        boolean canGoBack();
        void exitFullscreen();
        void goBack();
        void logAction(String source, String action);
    }

    interface Registration { void setEnabled(boolean enabled); }

    private final Host host;
    private final Registration registration;
    private boolean enabled;
    private boolean destroyed;

    BackNavigationController(Host host, Registration registration) {
        this.host = host;
        this.registration = registration;
    }

    void refresh() {
        if (destroyed) return;
        boolean wanted = host.fullscreen() || host.canGoBack();
        if (enabled != wanted) {
            registration.setEnabled(wanted);
            enabled = wanted;
        }
    }

    boolean handleBack(String source) {
        if (destroyed) return false;
        boolean fullscreen = host.fullscreen();
        boolean history = !fullscreen && host.canGoBack();
        host.logAction(source, fullscreen ? "EXIT_FULLSCREEN" : history ? "WEB_HISTORY" : "SYSTEM");
        try {
            if (fullscreen) host.exitFullscreen();
            else if (history) host.goBack();
            else return false;
            return true;
        } finally {
            refresh();
        }
    }

    void destroy() {
        if (destroyed) return;
        destroyed = true;
        if (enabled) registration.setEnabled(false);
        enabled = false;
    }
}
