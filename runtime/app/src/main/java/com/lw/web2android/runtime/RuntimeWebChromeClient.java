package com.lw.web2android.runtime;

import android.webkit.ConsoleMessage;
import android.webkit.WebChromeClient;
import android.webkit.WebView;

final class RuntimeWebChromeClient extends WebChromeClient {
    @Override
    public boolean onConsoleMessage(ConsoleMessage message) {
        String text = "[WEB-CONSOLE] " + message.messageLevel() + " "
                + RuntimeLog.safeUrl(message.sourceId()) + ":" + message.lineNumber()
                + " " + message.message();
        switch (message.messageLevel()) {
            case ERROR:
                RuntimeLog.error(text);
                break;
            case WARNING:
                RuntimeLog.warn(text);
                break;
            default:
                RuntimeLog.info(text);
                break;
        }
        return true;
    }

    @Override
    public void onProgressChanged(WebView view, int newProgress) {
        if (newProgress == 100) RuntimeLog.debug("WebView progress: 100%");
        super.onProgressChanged(view, newProgress);
    }

    @Override
    public void onReceivedTitle(WebView view, String title) {
        RuntimeLog.debug("WebView title received: " + title);
        super.onReceivedTitle(view, title);
    }
}
