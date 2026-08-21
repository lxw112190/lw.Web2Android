package com.lw.web2android.runtime;

import android.app.Activity;
import android.content.Intent;
import android.content.pm.ActivityInfo;
import android.content.res.Configuration;
import android.graphics.Color;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.util.DisplayMetrics;
import android.view.Gravity;
import android.view.KeyEvent;
import android.view.View;
import android.view.ViewGroup;
import android.view.WindowManager;
import android.webkit.WebSettings;
import android.webkit.WebView;
import android.widget.TextView;
import android.widget.Toast;

import androidx.webkit.WebViewAssetLoader;

import java.util.ArrayDeque;

/** Fixed, resource-independent Android Runtime entry point. */
public final class MainActivity extends Activity implements RuntimeWebViewClient.ErrorHandler {
    private static final int MAX_PENDING_EXTERNAL_CONTENT = 4;
    private WebView webView;
    private RuntimeConfig config;
    private RuntimeWebChromeClient webChromeClient;
    private ExternalContentReceiver externalContentReceiver;
    private final ArrayDeque<ExternalContentPayload> pendingExternalContent = new ArrayDeque<>();
    private boolean webPageReady;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        RuntimeLog.initialize(this);
        RuntimeLog.installCrashHandler();
        RuntimeLog.info("MainActivity.onCreate; Android API " + Build.VERSION.SDK_INT
                + ", package " + getPackageName());

        try {
            config = RuntimeConfig.load(this);
        } catch (RuntimeConfig.ConfigException error) {
            RuntimeLog.error("Runtime configuration failed", error);
            showRuntimeError(error.getMessage());
            return;
        }

        DeviceInfoLog.writeSnapshot(this, config);
        RuntimeLog.info("Configuration loaded; mode=" + config.mode
                + ", runtimeVersion=" + config.runtimeVersion
                + ", start=" + RuntimeLog.safeUrl(config.startUrl())
                + ", fullscreen=" + config.fullscreen
                + ", orientation=" + config.orientation
                + ", allowHttp=" + config.allowHttp);
        externalContentReceiver = new ExternalContentReceiver(
                getContentResolver(), config.externalContent);
        consumeExternalIntent(getIntent());

        try {
            applyRuntimeWindowConfig();
            createWebView();

            if (savedInstanceState == null || webView.restoreState(savedInstanceState) == null) {
                RuntimeLog.info("Loading start page: " + RuntimeLog.safeUrl(config.startUrl()));
                webView.loadUrl(config.startUrl());
            } else {
                RuntimeLog.info("WebView state restored");
            }
        } catch (RuntimeException error) {
            RuntimeLog.error("Unable to initialize Android WebView", error);
            showRuntimeError("Unable to initialize Android WebView: " + error.getMessage());
        }
    }

    @Override
    protected void onNewIntent(Intent intent) {
        super.onNewIntent(intent);
        setIntent(intent);
        consumeExternalIntent(intent);
    }

    private void consumeExternalIntent(Intent intent) {
        if (externalContentReceiver == null) return;
        ExternalContentReceiver.Result result = externalContentReceiver.receive(intent);
        if (result.payload != null) enqueueExternalContent(result.payload);
        if (result.userMessage != null) {
            Toast.makeText(this, result.userMessage, Toast.LENGTH_SHORT).show();
        }
    }

    private void enqueueExternalContent(ExternalContentPayload payload) {
        if (pendingExternalContent.size() >= MAX_PENDING_EXTERNAL_CONTENT) {
            pendingExternalContent.removeFirst();
            RuntimeLog.warn("External content queue full; oldest payload dropped");
        }
        pendingExternalContent.addLast(payload);
        flushExternalContentIfReady();
    }

    void onWebPageStarted(String url) {
        webPageReady = false;
    }

    void onWebPageFinished(String url) {
        webPageReady = isTrustedLocalPage(url);
        if (webPageReady) {
            RuntimeLog.debug("Trusted local Web page ready for external content");
            flushExternalContentIfReady();
        }
    }

    private void flushExternalContentIfReady() {
        if (config == null || config.mode != RuntimeConfig.Mode.LOCAL || webView == null
                || !webPageReady || !isTrustedLocalPage(webView.getUrl())) return;
        while (!pendingExternalContent.isEmpty()) {
            ExternalContentPayload payload = pendingExternalContent.removeFirst();
            String payloadJson = payload.toJsonString()
                    .replace("\u2028", "\\u2028")
                    .replace("\u2029", "\\u2029");
            String script = "(function(payload){"
                    + "var q=window.__lwExternalContentQueue;"
                    + "if(!Array.isArray(q)){q=[];window.__lwExternalContentQueue=q;}"
                    + "q.push(payload);"
                    + "window.dispatchEvent(new CustomEvent('lw:external-content',{detail:payload}));"
                    + "})(" + payloadJson + ");";
            try {
                webView.evaluateJavascript(script, null);
                RuntimeLog.info("External content delivered to WebView; kind="
                        + payload.kind.wireValue);
            } catch (RuntimeException error) {
                pendingExternalContent.addFirst(payload);
                RuntimeLog.warn("External content delivery deferred; exception="
                        + error.getClass().getSimpleName());
                break;
            }
        }
    }

    static boolean isTrustedLocalPage(String rawUrl) {
        if (rawUrl == null || rawUrl.isEmpty()) return false;
        try {
            Uri uri = Uri.parse(rawUrl);
            String path = uri.getPath();
            return "https".equalsIgnoreCase(uri.getScheme())
                    && "appassets.androidplatform.net".equalsIgnoreCase(uri.getHost())
                    && path != null && path.startsWith("/assets/");
        } catch (RuntimeException error) {
            return false;
        }
    }

    private void createWebView() {
        RuntimeLog.info("Creating Android WebView");
        webView = new WebView(this);
        webView.setBackgroundColor(Color.WHITE);
        webView.setLayoutParams(new ViewGroup.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.MATCH_PARENT));

        WebSettings settings = webView.getSettings();
        settings.setJavaScriptEnabled(true);
        settings.setDomStorageEnabled(true);
        settings.setDatabaseEnabled(true);
        // Keep fixed-width legacy pages visible on mobile without reflowing their layout.
        RuntimeWebViewPolicy.apply(settings);
        RuntimeLog.info("WebView viewport: wide=" + settings.getUseWideViewPort()
                + ", overview=" + settings.getLoadWithOverviewMode());
        settings.setAllowFileAccess(false);
        settings.setAllowContentAccess(false);
        settings.setMixedContentMode(config.allowHttp
                ? WebSettings.MIXED_CONTENT_ALWAYS_ALLOW
                : WebSettings.MIXED_CONTENT_NEVER_ALLOW);
        RuntimeLog.info("Mixed Content Mode: "
                + (config.allowHttp ? "ALWAYS_ALLOW" : "NEVER"));
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            settings.setSafeBrowsingEnabled(true);
        }

        WebViewAssetLoader assetLoader = new WebViewAssetLoader.Builder()
                .addPathHandler(
                        "/assets/",
                        new WebViewAssetLoader.AssetsPathHandler(this))
                .build();

        webView.setWebViewClient(new RuntimeWebViewClient(this, config, assetLoader, this));
        webChromeClient = new RuntimeWebChromeClient(this);
        webView.setWebChromeClient(webChromeClient);
        webView.setDownloadListener(new RuntimeDownloadListener(this, config));
        setContentView(webView);
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            android.content.pm.PackageInfo provider = WebView.getCurrentWebViewPackage();
            if (provider != null) {
                RuntimeLog.info("WebView provider: " + provider.packageName + " " + provider.versionName);
            }
        }
        DisplayMetrics metrics = getResources().getDisplayMetrics();
        RuntimeLog.info("Display metrics: widthPx=" + metrics.widthPixels
                + ", heightPx=" + metrics.heightPixels
                + ", density=" + metrics.density
                + ", densityDpi=" + metrics.densityDpi);
    }

    void logWebViewport(WebView view) {
        if (view == null) return;
        try {
            view.evaluateJavascript(
                    "(function(){return 'innerWidth=' + window.innerWidth"
                            + " + ', innerHeight=' + window.innerHeight"
                            + " + ', screenWidth=' + screen.width"
                            + " + ', screenHeight=' + screen.height"
                            + " + ', scrollWidth=' + (document.documentElement"
                            + " ? document.documentElement.scrollWidth : 0)"
                            + " + ', scrollHeight=' + (document.documentElement"
                            + " ? document.documentElement.scrollHeight : 0)"
                            + " + ', dpr=' + window.devicePixelRatio;})()",
                    value -> RuntimeLog.debug("Web viewport: " + javascriptString(value)));
        } catch (RuntimeException error) {
            RuntimeLog.debug("Web viewport unavailable; exception="
                    + error.getClass().getSimpleName());
        }
    }

    private static String javascriptString(String value) {
        if (value == null || value.length() < 2) return value == null ? "unknown" : value;
        if (value.charAt(0) == '"' && value.charAt(value.length() - 1) == '"') {
            value = value.substring(1, value.length() - 1)
                    .replace("\\\"", "\"")
                    .replace("\\\\", "\\");
        }
        return value.length() <= 512 ? value : value.substring(0, 512) + "...";
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

    void applyImmersiveFlags() {
        getWindow().getDecorView().setSystemUiVisibility(
                View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                        | View.SYSTEM_UI_FLAG_FULLSCREEN
                        | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                        | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                        | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                        | View.SYSTEM_UI_FLAG_LAYOUT_STABLE);
    }

    String currentDisplayOrientationName() {
        switch (getResources().getConfiguration().orientation) {
            case Configuration.ORIENTATION_LANDSCAPE: return "landscape";
            case Configuration.ORIENTATION_PORTRAIT: return "portrait";
            default: return "undefined";
        }
    }

    @Override
    public void onConfigurationChanged(Configuration newConfig) {
        super.onConfigurationChanged(newConfig);
        boolean html5Fullscreen = webChromeClient != null && webChromeClient.isCustomViewShowing();
        RuntimeLog.debug("Configuration changed; orientation=" + currentDisplayOrientationName()
                + ", html5Fullscreen=" + html5Fullscreen);
        if (html5Fullscreen) applyImmersiveFlags();
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (webChromeClient != null && webChromeClient.isCustomViewShowing()) {
            webChromeClient.onWindowFocusChanged(hasFocus);
        } else if (hasFocus && config != null && config.fullscreen) {
            applyImmersiveFlags();
        }
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        if (webChromeClient != null &&
                webChromeClient.handleActivityResult(requestCode, resultCode, data)) {
            return;
        }
        super.onActivityResult(requestCode, resultCode, data);
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
        RuntimeLog.debug("MainActivity.onPause");
        if (webView != null) {
            webView.onPause();
        }
        super.onPause();
    }

    @Override
    protected void onResume() {
        super.onResume();
        RuntimeLog.debug("MainActivity.onResume");
        if (webView != null) {
            webView.onResume();
        }
    }

    @Override
    public boolean onKeyDown(int keyCode, KeyEvent event) {
        if (keyCode == KeyEvent.KEYCODE_BACK && handleBackNavigation()) return true;
        return super.onKeyDown(keyCode, event);
    }

    @Override
    @SuppressWarnings("deprecation")
    public void onBackPressed() {
        if (!handleBackNavigation()) super.onBackPressed();
    }

    private boolean handleBackNavigation() {
        if (webChromeClient != null && webChromeClient.isCustomViewShowing()) {
            webChromeClient.hideCustomView();
            return true;
        }
        if (webView != null && webView.canGoBack()) {
            webView.goBack();
            return true;
        }
        return false;
    }

    @Override
    public void onMainFrameError(final String message) {
        RuntimeLog.error("Main frame error: " + message);
        runOnUiThread(() -> showRuntimeError(message));
    }

    private void showRuntimeError(String message) {
        RuntimeLog.error("Showing Runtime error page: " + message);
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
        errorView.setText("lw.Web2Android\n\n" + message
                + "\n\nRuntime logs:\n" + RuntimeLog.directoryPath());
        errorView.setTextIsSelectable(true);
        setContentView(errorView);
    }

    private void destroyWebView() {
        if (webView != null) {
            RuntimeLog.debug("Destroying Android WebView");
            if (webChromeClient != null) webChromeClient.destroy();
            webView.setDownloadListener(null);
            webView.stopLoading();
            webView.setWebChromeClient(null);
            webView.setWebViewClient(null);
            webView.destroy();
            webView = null;
            webChromeClient = null;
            webPageReady = false;
        }
    }

    @Override
    protected void onDestroy() {
        RuntimeLog.info("MainActivity.onDestroy");
        destroyWebView();
        RuntimeLog.flush();
        super.onDestroy();
    }
}
