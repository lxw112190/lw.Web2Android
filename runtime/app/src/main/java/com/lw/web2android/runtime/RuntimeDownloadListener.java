package com.lw.web2android.runtime;

import android.app.DownloadManager;
import android.content.Context;
import android.net.Uri;
import android.os.Environment;
import android.webkit.CookieManager;
import android.webkit.DownloadListener;
import android.webkit.URLUtil;
import android.widget.Toast;

/** Standard Android DownloadManager integration; no JavaScript or Native Bridge is exposed. */
final class RuntimeDownloadListener implements DownloadListener {
    private final MainActivity activity;
    private final RuntimeConfig config;

    RuntimeDownloadListener(MainActivity activity, RuntimeConfig config) {
        this.activity = activity;
        this.config = config;
    }

    @Override
    public void onDownloadStart(
            String url,
            String userAgent,
            String contentDisposition,
            String mimeType,
            long contentLength) {
        Uri uri;
        try {
            uri = Uri.parse(url);
        } catch (RuntimeException error) {
            RuntimeLog.warn("Download rejected: invalid URL");
            showMessage("无法识别下载地址");
            return;
        }

        if (!DownloadPolicy.isSupportedScheme(uri.getScheme())) {
            RuntimeLog.warn("Download scheme is not supported without a Native Bridge: "
                    + NavigationPolicy.externalLogSummary(
                            uri.getScheme(), uri.getHost(), uri.getEncodedPath()));
            showMessage("当前只支持 HTTP/HTTPS 下载");
            return;
        }
        if (!config.allowsNavigation(uri)) {
            RuntimeLog.warn("Blocked cleartext download: " + RuntimeLog.safeUrl(url));
            showMessage("HTTP 下载已被安全设置阻止");
            return;
        }

        try {
            DownloadManager manager =
                    (DownloadManager) activity.getSystemService(Context.DOWNLOAD_SERVICE);
            if (manager == null) throw new IllegalStateException("DownloadManager unavailable");
            String guessedName = URLUtil.guessFileName(url, contentDisposition, mimeType);
            String fileName = DownloadPolicy.destinationFileName(
                    guessedName, System.currentTimeMillis());
            DownloadManager.Request request = new DownloadManager.Request(uri)
                    .setTitle(fileName)
                    .setDescription("lw.Web2Android download")
                    .setNotificationVisibility(
                            DownloadManager.Request.VISIBILITY_VISIBLE_NOTIFY_COMPLETED)
                    .setAllowedOverMetered(true)
                    .setAllowedOverRoaming(false)
                    .setDestinationInExternalFilesDir(
                            activity, Environment.DIRECTORY_DOWNLOADS, fileName);
            if (mimeType != null && !mimeType.isEmpty()) request.setMimeType(mimeType);
            if (safeHeaderValue(userAgent)) request.addRequestHeader("User-Agent", userAgent);
            String cookie = CookieManager.getInstance().getCookie(url);
            if (safeHeaderValue(cookie)) request.addRequestHeader("Cookie", cookie);
            long downloadId = manager.enqueue(request);
            RuntimeLog.info("Download queued: id=" + downloadId
                    + ", file=" + fileName
                    + ", bytes=" + Math.max(contentLength, -1L)
                    + ", url=" + RuntimeLog.safeUrl(url));
            showMessage("已加入下载队列：" + fileName);
        } catch (IllegalArgumentException | IllegalStateException | SecurityException error) {
            RuntimeLog.warn("Unable to queue download: " + RuntimeLog.safeUrl(url)
                    + "; exception=" + error.getClass().getSimpleName());
            showMessage("下载任务创建失败");
        }
    }

    private static boolean safeHeaderValue(String value) {
        return value != null && !value.isEmpty() &&
                value.indexOf('\r') < 0 && value.indexOf('\n') < 0;
    }

    private void showMessage(String message) {
        activity.runOnUiThread(() ->
                Toast.makeText(activity, message, Toast.LENGTH_SHORT).show());
    }
}
