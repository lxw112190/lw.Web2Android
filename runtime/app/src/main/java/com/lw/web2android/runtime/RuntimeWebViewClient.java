package com.lw.web2android.runtime;

import android.content.ActivityNotFoundException;
import android.content.Intent;
import android.annotation.TargetApi;
import android.net.Uri;
import android.net.http.SslError;
import android.os.Build;
import android.webkit.RenderProcessGoneDetail;
import android.webkit.SslErrorHandler;
import android.webkit.WebResourceRequest;
import android.webkit.WebResourceResponse;
import android.webkit.WebView;

import androidx.webkit.WebResourceErrorCompat;
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
    public void onReceivedError(WebView view, WebResourceRequest request, WebResourceErrorCompat error) {
        String diagnostic = RuntimeLog.safeUrl(request.getUrl().toString())
                + "; code=" + error.getErrorCode() + "; " + error.getDescription();
        if (request.isForMainFrame()) {
            RuntimeLog.error("Main page load failed: " + diagnostic);
            errorHandler.onMainFrameError("Unable to load page: " + error.getDescription());
        } else {
            RuntimeLog.warn("Web resource load failed: " + diagnostic);
        }
    }

    @Override
    public void onPageStarted(WebView view, String url, android.graphics.Bitmap favicon) {
        RuntimeLog.info("Page started: " + RuntimeLog.safeUrl(url));
        super.onPageStarted(view, url, favicon);
    }

    @Override
    public void onPageFinished(WebView view, String url) {
        RuntimeLog.info("Page finished: " + RuntimeLog.safeUrl(url));
        super.onPageFinished(view, url);
    }

    @Override
    public void onReceivedHttpError(
            WebView view, WebResourceRequest request, WebResourceResponse errorResponse) {
        RuntimeLog.warn("HTTP " + errorResponse.getStatusCode() + " for "
                + RuntimeLog.safeUrl(request.getUrl().toString())
                + (request.isForMainFrame() ? " (main frame)" : ""));
        super.onReceivedHttpError(view, request, errorResponse);
    }

    @Override
    public void onReceivedSslError(WebView view, SslErrorHandler handler, SslError error) {
        RuntimeLog.error("SSL error " + error.getPrimaryError() + " for "
                + RuntimeLog.safeUrl(error.getUrl()) + "; navigation canceled");
        super.onReceivedSslError(view, handler, error);
    }

    @Override
    @TargetApi(Build.VERSION_CODES.O)
    public boolean onRenderProcessGone(WebView view, RenderProcessGoneDetail detail) {
        RuntimeLog.error("WebView renderer exited; crashed=" + detail.didCrash()
                + ", priority=" + detail.rendererPriorityAtExit());
        errorHandler.onMainFrameError("Android WebView renderer exited unexpectedly.");
        return true;
    }

    private boolean handleNavigation(Uri uri) {
        RuntimeLog.debug("Navigation requested: " + RuntimeLog.safeUrl(uri.toString()));
        String scheme = uri.getScheme();
        if ("http".equalsIgnoreCase(scheme) || "https".equalsIgnoreCase(scheme)) {
            if (!config.allowsNavigation(uri)) {
                RuntimeLog.warn("Blocked cleartext navigation: " + RuntimeLog.safeUrl(uri.toString()));
                errorHandler.onMainFrameError("Blocked cleartext HTTP navigation. Enable allowHttp to continue.");
                return true;
            }
            return false;
        }

        try {
            RuntimeLog.info("Opening external URL: " + RuntimeLog.safeUrl(uri.toString()));
            activity.startActivity(new Intent(Intent.ACTION_VIEW, uri));
        } catch (ActivityNotFoundException | SecurityException error) {
            RuntimeLog.error("Unable to open external URL", error);
            errorHandler.onMainFrameError("No installed application can open this link.");
        }
        return true;
    }
}
