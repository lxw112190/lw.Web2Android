package com.lw.web2android.runtime;

import android.app.Activity;
import android.content.pm.ActivityInfo;
import android.graphics.Color;
import android.os.Build;
import android.os.Bundle;
import android.view.Gravity;
import android.view.KeyEvent;
import android.view.View;
import android.view.ViewGroup;
import android.view.WindowManager;
import android.webkit.WebChromeClient;
import android.webkit.WebSettings;
import android.webkit.WebView;
import android.widget.TextView;

import androidx.webkit.WebViewAssetLoader;

/** Fixed, resource-independent Android Runtime entry point. */
public final class MainActivity extends Activity implements RuntimeWebViewClient.ErrorHandler {
    private WebView webView;
    private RuntimeConfig config;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        try {
            config = RuntimeConfig.load(this);
        } catch (RuntimeConfig.ConfigException error) {
            showRuntimeError(error.getMessage());
            return;
        }

        applyRuntimeWindowConfig();
        createWebView();

        if (savedInstanceState == null || webView.restoreState(savedInstanceState) == null) {
            webView.loadUrl(config.startUrl());
        }
    }

    private void createWebView() {
        webView = new WebView(this);
        webView.setBackgroundColor(Color.WHITE);
        webView.setLayoutParams(new ViewGroup.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.MATCH_PARENT));

        WebSettings settings = webView.getSettings();
        settings.setJavaScriptEnabled(true);
        settings.setDomStorageEnabled(true);
        settings.setDatabaseEnabled(true);
        settings.setAllowFileAccess(false);
        settings.setAllowContentAccess(false);
        settings.setMixedContentMode(config.allowHttp
                ? WebSettings.MIXED_CONTENT_COMPATIBILITY_MODE
                : WebSettings.MIXED_CONTENT_NEVER_ALLOW);
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            settings.setSafeBrowsingEnabled(true);
        }

        WebViewAssetLoader assetLoader = new WebViewAssetLoader.Builder()
                .addPathHandler(
                        "/assets/",
                        new WebViewAssetLoader.AssetsPathHandler(this))
                .build();

        webView.setWebViewClient(new RuntimeWebViewClient(this, config, assetLoader, this));
        webView.setWebChromeClient(new WebChromeClient());
        setContentView(webView);
    }

    private void applyRuntimeWindowConfig() {
        switch (config.orientation) {
            case PORTRAIT:
                setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_PORTRAIT);
                break;
            case LANDSCAPE:
                setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE);
                break;
            case AUTO:
            default:
                setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_UNSPECIFIED);
                break;
        }

        if (config.fullscreen) {
            getWindow().setFlags(
                    WindowManager.LayoutParams.FLAG_FULLSCREEN,
                    WindowManager.LayoutParams.FLAG_FULLSCREEN);
            applyImmersiveFlags();
        }
    }

    private void applyImmersiveFlags() {
        getWindow().getDecorView().setSystemUiVisibility(
                View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                        | View.SYSTEM_UI_FLAG_FULLSCREEN
                        | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                        | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                        | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                        | View.SYSTEM_UI_FLAG_LAYOUT_STABLE);
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus && config != null && config.fullscreen) {
            applyImmersiveFlags();
        }
    }

    @Override
    protected void onSaveInstanceState(Bundle outState) {
        if (webView != null) {
            webView.saveState(outState);
        }
        super.onSaveInstanceState(outState);
    }

    @Override
    protected void onPause() {
        if (webView != null) {
            webView.onPause();
        }
        super.onPause();
    }

    @Override
    protected void onResume() {
        super.onResume();
        if (webView != null) {
            webView.onResume();
        }
    }

    @Override
    public boolean onKeyDown(int keyCode, KeyEvent event) {
        if (keyCode == KeyEvent.KEYCODE_BACK && webView != null && webView.canGoBack()) {
            webView.goBack();
            return true;
        }
        return super.onKeyDown(keyCode, event);
    }

    @Override
    public void onMainFrameError(final String message) {
        runOnUiThread(() -> showRuntimeError(message));
    }

    private void showRuntimeError(String message) {
        destroyWebView();
        TextView errorView = new TextView(this);
        errorView.setLayoutParams(new ViewGroup.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.MATCH_PARENT));
        int padding = Math.round(24 * getResources().getDisplayMetrics().density);
        errorView.setPadding(padding, padding, padding, padding);
        errorView.setGravity(Gravity.CENTER);
        errorView.setTextColor(Color.rgb(120, 30, 30));
        errorView.setBackgroundColor(Color.rgb(255, 247, 247));
        errorView.setText("lw.Web2Android\n\n" + message);
        errorView.setTextIsSelectable(true);
        setContentView(errorView);
    }

    private void destroyWebView() {
        if (webView != null) {
            webView.stopLoading();
            webView.setWebChromeClient(null);
            webView.setWebViewClient(null);
            webView.destroy();
            webView = null;
        }
    }

    @Override
    protected void onDestroy() {
        destroyWebView();
        super.onDestroy();
    }
}

