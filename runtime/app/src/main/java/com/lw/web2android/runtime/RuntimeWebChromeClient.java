package com.lw.web2android.runtime;

import android.app.Activity;
import android.content.ClipData;
import android.content.ActivityNotFoundException;
import android.content.Intent;
import android.content.pm.ActivityInfo;
import android.graphics.Color;
import android.net.Uri;
import android.provider.MediaStore;
import android.view.View;
import android.view.ViewGroup;
import android.webkit.ValueCallback;
import android.webkit.ConsoleMessage;
import android.webkit.WebChromeClient;
import android.webkit.WebView;
import android.widget.Toast;

import androidx.core.content.FileProvider;

import java.io.File;
import java.io.IOException;
import java.util.ArrayList;
import java.util.List;

final class RuntimeWebChromeClient extends WebChromeClient {
    private static final int FILE_CHOOSER_REQUEST_CODE = 0x4c57;
    private static final int MAX_SELECTED_FILES = 20;
    private static final long STALE_CAPTURE_AGE_MILLIS = 24L * 60L * 60L * 1000L;

    private final MainActivity activity;
    private ValueCallback<Uri[]> pendingFileCallback;
    private Uri pendingCameraUri;
    private File pendingCameraFile;
    private View customView;
    private CustomViewCallback customViewCallback;
    private int originalOrientation;
    private int originalSystemUiVisibility;

    RuntimeWebChromeClient(MainActivity activity) {
        this.activity = activity;
        cleanStaleCameraCaptures();
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
            boolean multiple = fileChooserParams.getMode() == FileChooserParams.MODE_OPEN_MULTIPLE;
            boolean capture = fileChooserParams.isCaptureEnabled();
            String[] acceptTypes = fileChooserParams.getAcceptTypes();
            RuntimeLog.info("File chooser requested; accept=" + describeAcceptTypes(acceptTypes)
                    + ", capture=" + capture + ", multiple=" + multiple);
            if (FileChooserPolicy.shouldCaptureImage(capture, multiple, acceptTypes)
                    && openCameraCapture()) {
                return true;
            }
            openSystemFileChooser(fileChooserParams, multiple);
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

    private void openSystemFileChooser(FileChooserParams params, boolean multiple) {
        Intent intent = params.createIntent();
        activity.startActivityForResult(intent, FILE_CHOOSER_REQUEST_CODE);
        RuntimeLog.info("File chooser opened; mode=" + params.getMode()
                + ", multiple=" + multiple);
    }

    private boolean openCameraCapture() {
        File captureDirectory = new File(activity.getCacheDir(), "camera-captures");
        try {
            if (!captureDirectory.isDirectory() && !captureDirectory.mkdirs()) {
                throw new IOException("capture directory was not created");
            }
            File output = File.createTempFile("capture-", ".jpg", captureDirectory);
            Uri outputUri = FileProvider.getUriForFile(
                    activity, activity.getPackageName() + ".fileprovider", output);
            Intent camera = new Intent(MediaStore.ACTION_IMAGE_CAPTURE);
            camera.putExtra(MediaStore.EXTRA_OUTPUT, outputUri);
            camera.setClipData(ClipData.newRawUri("camera-output", outputUri));
            camera.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION
                    | Intent.FLAG_GRANT_WRITE_URI_PERMISSION);
            pendingCameraFile = output;
            pendingCameraUri = outputUri;
            activity.startActivityForResult(camera, FILE_CHOOSER_REQUEST_CODE);
            RuntimeLog.info("Camera capture launched; output=private-cache-content-uri");
            return true;
        } catch (ActivityNotFoundException | SecurityException | IllegalArgumentException | IOException error) {
            RuntimeLog.warn("Camera capture unavailable; falling back to file chooser; exception="
                    + error.getClass().getSimpleName());
            deletePendingCameraFile();
            return false;
        }
    }

    boolean handleActivityResult(int requestCode, int resultCode, Intent data) {
        if (requestCode != FILE_CHOOSER_REQUEST_CODE) return false;
        ValueCallback<Uri[]> callback = pendingFileCallback;
        pendingFileCallback = null;
        Uri cameraUri = pendingCameraUri;
        File cameraFile = pendingCameraFile;
        pendingCameraUri = null;
        pendingCameraFile = null;
        revokeCameraUriPermission(cameraUri);
        if (callback == null) {
            deleteFile(cameraFile);
            return true;
        }
        if (cameraUri != null) {
            if (resultCode == Activity.RESULT_OK && cameraFile != null
                    && cameraFile.isFile() && cameraFile.length() > 0L) {
                callback.onReceiveValue(new Uri[] {cameraUri});
                RuntimeLog.info("Camera capture completed; mime=image/jpeg, bytes="
                        + cameraFile.length());
                return true;
            }
            Uri[] fallback = resultCode == Activity.RESULT_OK
                    ? acceptContentUris(contentUrisFromIntent(data)) : new Uri[0];
            if (fallback.length > 0) {
                deleteFile(cameraFile);
                callback.onReceiveValue(fallback);
                RuntimeLog.info("Camera capture completed with returned content URI");
                return true;
            }
            deleteFile(cameraFile);
            callback.onReceiveValue(null);
            RuntimeLog.info(resultCode == Activity.RESULT_CANCELED
                    ? "Camera capture canceled" : "Camera capture returned no image");
            return true;
        }

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
        deletePendingCameraFile();
        if (callback != null) callback.onReceiveValue(null);
    }

    private void deletePendingCameraFile() {
        File file = pendingCameraFile;
        Uri uri = pendingCameraUri;
        pendingCameraFile = null;
        pendingCameraUri = null;
        revokeCameraUriPermission(uri);
        deleteFile(file);
    }

    private void revokeCameraUriPermission(Uri uri) {
        if (uri == null) return;
        try {
            activity.revokeUriPermission(uri, Intent.FLAG_GRANT_READ_URI_PERMISSION
                    | Intent.FLAG_GRANT_WRITE_URI_PERMISSION);
        } catch (RuntimeException error) {
            RuntimeLog.debug("Unable to revoke camera URI permission; exception="
                    + error.getClass().getSimpleName());
        }
    }

    private static void deleteFile(File file) {
        if (file != null && file.isFile() && !file.delete()) {
            RuntimeLog.debug("Unable to remove private camera capture file");
        }
    }

    private void cleanStaleCameraCaptures() {
        File directory = new File(activity.getCacheDir(), "camera-captures");
        File[] files = directory.listFiles();
        if (files == null) return;
        long cutoff = System.currentTimeMillis() - STALE_CAPTURE_AGE_MILLIS;
        int removed = 0;
        for (File file : files) {
            if (file.isFile() && file.lastModified() < cutoff && file.delete()) removed++;
        }
        if (removed > 0) RuntimeLog.debug("Removed stale camera captures; count=" + removed);
    }

    private static Uri[] contentUrisFromIntent(Intent data) {
        if (data == null) return new Uri[0];
        List<Uri> values = new ArrayList<>();
        if (data.getData() != null) values.add(data.getData());
        ClipData clipData = data.getClipData();
        if (clipData != null) {
            for (int index = 0; index < clipData.getItemCount(); index++) {
                Uri uri = clipData.getItemAt(index).getUri();
                if (uri != null) values.add(uri);
            }
        }
        return values.toArray(new Uri[0]);
    }

    private static String describeAcceptTypes(String[] acceptTypes) {
        if (acceptTypes == null || acceptTypes.length == 0) return "unspecified";
        StringBuilder result = new StringBuilder();
        for (String type : acceptTypes) {
            if (type == null || type.trim().isEmpty()) continue;
            if (result.length() > 0) result.append(',');
            result.append(type.trim().replace('\r', ' ').replace('\n', ' ').replace('\t', ' '));
            if (result.length() >= 160) return result.substring(0, 160) + "...";
        }
        return result.length() == 0 ? "unspecified" : result.toString();
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
