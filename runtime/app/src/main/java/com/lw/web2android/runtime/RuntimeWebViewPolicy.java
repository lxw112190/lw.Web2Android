package com.lw.web2android.runtime;

import android.webkit.WebSettings;

final class RuntimeWebViewPolicy {
    static final boolean USE_WIDE_VIEW_PORT = true;
    static final boolean LOAD_WITH_OVERVIEW_MODE = true;

    private RuntimeWebViewPolicy() {}

    static void apply(WebSettings settings) {
        settings.setUseWideViewPort(USE_WIDE_VIEW_PORT);
        settings.setLoadWithOverviewMode(LOAD_WITH_OVERVIEW_MODE);
    }
}
