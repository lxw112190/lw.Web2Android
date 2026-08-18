package com.lw.web2android.runtime;

import android.content.ActivityNotFoundException;
import android.content.Intent;
import android.net.Uri;
import android.webkit.WebResourceError;
import android.webkit.WebResourceRequest;
import android.webkit.WebResourceResponse;
import android.webkit.WebView;

import androidx.webkit.WebViewAssetLoader;
import androidx.webkit.WebViewClientCompat;

final class RuntimeWebViewClient extends WebViewClientCompat {
    interface ErrorHandler {
        void onMainFrameError(String message);
    }

    private final MainActivity activity;
    private final RuntimeConfig config;
    private final WebViewAssetLoader assetLoader;
    private final ErrorHandler errorHandler;

    RuntimeWebViewClient(
            MainActivity activity,
            RuntimeConfig config,
            WebViewAssetLoader assetLoader,
            ErrorHandler errorHandler) {
        this.activity = activity;
        this.config = config;
        this.assetLoader = assetLoader;
        this.errorHandler = errorHandler;
    }

    @Override
    public WebResourceResponse shouldInterceptRequest(WebView view, WebResourceRequest request) {
        return assetLoader.shouldInterceptRequest(request.getUrl());
    }

    @Override
    @SuppressWarnings("deprecation")
    public WebResourceResponse shouldInterceptRequest(WebView view, String url) {
        return assetLoader.shouldInterceptRequest(Uri.parse(url));
    }

    @Override
    public boolean shouldOverrideUrlLoading(WebView view, WebResourceRequest request) {
        return handleNavigation(request.getUrl());
    }

    @Override
    @SuppressWarnings("deprecation")
    public boolean shouldOverrideUrlLoading(WebView view, String url) {
        return handleNavigation(Uri.parse(url));
    }

    @Override
    public void onReceivedError(WebView view, WebResourceRequest request, WebResourceError error) {
        if (request.isForMainFrame()) {
            errorHandler.onMainFrameError("Unable to load page: " + error.getDescription());
        }
    }

    private boolean handleNavigation(Uri uri) {
        String scheme = uri.getScheme();
        if ("http".equalsIgnoreCase(scheme) || "https".equalsIgnoreCase(scheme)) {
            if (!config.allowsNavigation(uri)) {
                errorHandler.onMainFrameError("Blocked cleartext HTTP navigation. Enable allowHttp to continue.");
                return true;
            }
            return false;
        }

        try {
            activity.startActivity(new Intent(Intent.ACTION_VIEW, uri));
        } catch (ActivityNotFoundException | SecurityException ignored) {
            errorHandler.onMainFrameError("No installed application can open this link.");
        }
        return true;
    }
}

