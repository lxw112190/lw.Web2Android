package com.lw.web2android.runtime;

import android.app.Activity;
import android.content.ActivityNotFoundException;
import android.content.Intent;
import android.content.pm.ActivityInfo;
import android.graphics.Color;
import android.net.Uri;
import android.view.View;
import android.view.ViewGroup;
import android.webkit.ValueCallback;
import android.webkit.ConsoleMessage;
import android.webkit.WebChromeClient;
import android.webkit.WebView;
import android.widget.Toast;

import java.util.ArrayList;
import java.util.List;

final class RuntimeWebChromeClient extends WebChromeClient {
    private static final int FILE_CHOOSER_REQUEST_CODE = 0x4c57;
    private static final int MAX_SELECTED_FILES = 20;

    private final MainActivity activity;
    private ValueCallback<Uri[]> pendingFileCallback;
    private View customView;
    private CustomViewCallback customViewCallback;
    private int originalOrientation;
    private int originalSystemUiVisibility;

    RuntimeWebChromeClient(MainActivity activity) {
        this.activity = activity;
    }

    @Override
    public void onShowCustomView(View view, CustomViewCallback callback) {
        showCustomView(view, FullscreenOrientationPolicy.resolve(activity.getRequestedOrientation()), callback);
    }

    @Override
    @SuppressWarnings("deprecation")
    public void onShowCustomView(
            View view,
            int requestedOrientation,
            CustomViewCallback callback) {
        RuntimeLog.debug("HTML5 fullscreen requested orientation hint="
                + FullscreenOrientationPolicy.requestedOrientationName(requestedOrientation));
        showCustomView(view, FullscreenOrientationPolicy.resolve(activity.getRequestedOrientation()), callback);
    }

    private void showCustomView(
            View view,
            int requestedOrientation,
            CustomViewCallback callback) {
        if (view == null) {
            if (callback != null) callback.onCustomViewHidden();
            return;
        }
        if (customView != null) {
            RuntimeLog.warn("Ignoring duplicate HTML5 video fullscreen request");
            if (callback != null) callback.onCustomViewHidden();
            return;
        }

        View decorView = activity.getWindow().getDecorView();
        if (!(decorView instanceof ViewGroup)) {
            RuntimeLog.warn("Unable to enter HTML5 video fullscreen: no decor container");
            if (callback != null) callback.onCustomViewHidden();
            return;
        }

        originalOrientation = activity.getRequestedOrientation();
        originalSystemUiVisibility = decorView.getSystemUiVisibility();
        customView = view;
        customViewCallback = callback;
        try {
            if (view.getParent() instanceof ViewGroup) {
                ((ViewGroup) view.getParent()).removeView(view);
            }
            view.setBackgroundColor(Color.BLACK);
            view.setKeepScreenOn(true);
            ((ViewGroup) decorView).addView(
                    view,
                    new ViewGroup.LayoutParams(
                            ViewGroup.LayoutParams.MATCH_PARENT,
                            ViewGroup.LayoutParams.MATCH_PARENT));
            activity.setRequestedOrientation(requestedOrientation);
            activity.applyImmersiveFlags();
            RuntimeLog.info("HTML5 fullscreen entered; originalOrientation="
                    + FullscreenOrientationPolicy.requestedOrientationName(originalOrientation)
                    + ", fullscreenOrientation="
                    + FullscreenOrientationPolicy.requestedOrientationName(requestedOrientation)
                    + ", displayOrientation=" + activity.currentDisplayOrientationName());
        } catch (RuntimeException error) {
            RuntimeLog.warn("Unable to enter HTML5 video fullscreen; exception="
                    + error.getClass().getSimpleName());
            removeCustomViewFromParent();
            restoreCustomViewState();
            notifyCustomViewHidden();
        }
    }

    @Override
    public void onHideCustomView() {
        hideCustomView();
    }

    boolean isCustomViewShowing() {
        return customView != null;
    }

    void hideCustomView() {
        if (customView == null) return;
        removeCustomViewFromParent();
        restoreCustomViewState();
        notifyCustomViewHidden();
        RuntimeLog.info("HTML5 fullscreen exited; restoredOrientation="
                + FullscreenOrientationPolicy.requestedOrientationName(originalOrientation));
    }

    void onWindowFocusChanged(boolean hasFocus) {
        if (hasFocus && customView != null) activity.applyImmersiveFlags();
    }

    void destroy() {
        cancelPendingFileChooser();
        hideCustomView();
    }

    private void removeCustomViewFromParent() {
        if (customView == null) return;
        try {
            customView.setKeepScreenOn(false);
            if (customView.getParent() instanceof ViewGroup) {
                ((ViewGroup) customView.getParent()).removeView(customView);
            }
        } catch (RuntimeException error) {
            RuntimeLog.warn("Unable to detach HTML5 video fullscreen view; exception="
                    + error.getClass().getSimpleName());
        }
    }

    private void restoreCustomViewState() {
        try {
            activity.setRequestedOrientation(originalOrientation);
            activity.getWindow().getDecorView().setSystemUiVisibility(originalSystemUiVisibility);
        } catch (RuntimeException error) {
            RuntimeLog.warn("Unable to restore window after HTML5 video fullscreen; exception="
                    + error.getClass().getSimpleName());
        }
    }

    private void notifyCustomViewHidden() {
        CustomViewCallback callback = customViewCallback;
        customView = null;
        customViewCallback = null;
        if (callback != null) {
            try {
                callback.onCustomViewHidden();
            } catch (RuntimeException error) {
                RuntimeLog.warn("HTML5 video fullscreen callback failed; exception="
                        + error.getClass().getSimpleName());
            }
        }
    }

    @Override
    public boolean onShowFileChooser(
            WebView webView,
            ValueCallback<Uri[]> filePathCallback,
            FileChooserParams fileChooserParams) {
        cancelPendingFileChooser();
        pendingFileCallback = filePathCallback;
        try {
            Intent intent = fileChooserParams.createIntent();
            activity.startActivityForResult(intent, FILE_CHOOSER_REQUEST_CODE);
            RuntimeLog.info("File chooser opened; mode=" + fileChooserParams.getMode()
                    + ", multiple="
                    + (fileChooserParams.getMode() == FileChooserParams.MODE_OPEN_MULTIPLE));
        } catch (ActivityNotFoundException | SecurityException error) {
            RuntimeLog.warn("Unable to open the Android file chooser; exception="
                    + error.getClass().getSimpleName());
            cancelPendingFileChooser();
            Toast.makeText(activity, "设备没有可用的文件选择器", Toast.LENGTH_SHORT).show();
        } catch (RuntimeException error) {
            RuntimeLog.warn("File chooser failed; exception=" + error.getClass().getSimpleName());
            cancelPendingFileChooser();
            Toast.makeText(activity, "无法选择文件", Toast.LENGTH_SHORT).show();
        }
        return true;
    }

    boolean handleActivityResult(int requestCode, int resultCode, Intent data) {
        if (requestCode != FILE_CHOOSER_REQUEST_CODE) return false;
        ValueCallback<Uri[]> callback = pendingFileCallback;
        pendingFileCallback = null;
        if (callback == null) return true;

        Uri[] accepted;
        try {
            accepted = acceptContentUris(FileChooserParams.parseResult(resultCode, data));
            callback.onReceiveValue(accepted.length == 0 ? null : accepted);
        } catch (RuntimeException error) {
            RuntimeLog.warn("Unable to parse file chooser result; exception="
                    + error.getClass().getSimpleName());
            callback.onReceiveValue(null);
            return true;
        }
        if (resultCode == Activity.RESULT_CANCELED || accepted.length == 0) {
            RuntimeLog.info("File chooser canceled or returned no safe content URI");
        } else {
            RuntimeLog.info("File chooser completed; selectedFiles=" + accepted.length);
        }
        return true;
    }

    void cancelPendingFileChooser() {
        ValueCallback<Uri[]> callback = pendingFileCallback;
        pendingFileCallback = null;
        if (callback != null) callback.onReceiveValue(null);
    }

    private static Uri[] acceptContentUris(Uri[] candidates) {
        if (candidates == null || candidates.length == 0) return new Uri[0];
        List<Uri> accepted = new ArrayList<>();
        for (Uri candidate : candidates) {
            if (candidate == null || !"content".equalsIgnoreCase(candidate.getScheme())) continue;
            accepted.add(candidate);
            if (accepted.size() == MAX_SELECTED_FILES) break;
        }
        return accepted.toArray(new Uri[0]);
    }

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
