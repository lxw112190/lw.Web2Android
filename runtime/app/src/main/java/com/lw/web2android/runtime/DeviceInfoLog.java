package com.lw.web2android.runtime;

import android.content.Context;
import android.content.pm.PackageInfo;
import android.content.pm.PackageManager;
import android.net.ConnectivityManager;
import android.net.Network;
import android.net.NetworkCapabilities;
import android.os.Build;
import android.util.DisplayMetrics;
import android.webkit.WebView;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.util.Date;
import java.util.Locale;
import java.util.TimeZone;

/** Bounded startup snapshots for device, WebView, network and Runtime configuration diagnostics. */
final class DeviceInfoLog {
    private static final long MAX_FILE_SIZE = 2L * 1024L * 1024L;
    private static final int MAX_ARCHIVES = 5;
    private static final String FILE_NAME = "device-info.log";
    private static final Object LOCK = new Object();

    private DeviceInfoLog() {}

    static void writeSnapshot(Context context, RuntimeConfig config) {
        synchronized (LOCK) {
            try {
                File directory = RuntimeLog.resolveLogDirectory(context.getApplicationContext());
                File file = new File(directory, FILE_NAME);
                byte[] snapshot = createSnapshot(context, config).getBytes(StandardCharsets.UTF_8);
                if (file.length() > 0L && file.length() + snapshot.length > MAX_FILE_SIZE) {
                    rotate(directory, file);
                }
                try (FileOutputStream output = new FileOutputStream(file, true)) {
                    output.write(snapshot);
                    output.flush();
                }
                RuntimeLog.info("Device diagnostics written: " + file.getAbsolutePath());
            } catch (IOException | RuntimeException error) {
                RuntimeLog.error("Unable to write device diagnostics", error);
            }
        }
    }

    private static String createSnapshot(Context context, RuntimeConfig config) {
        StringBuilder output = new StringBuilder(2048);
        Date now = new Date();
        output.append("==================================================\n");
        output.append("lw.Web2Android Device Diagnostics\n");
        output.append("Time (Local)   : ").append(RuntimeLog.formatLocal(now)).append('\n');
        output.append("Time (UTC)     : ").append(RuntimeLog.formatUtc(now)).append('\n');
        output.append("\nApp\n");
        output.append("  Package      : ").append(clean(context.getPackageName())).append('\n');
        output.append("  Version      : ").append(applicationVersion(context)).append('\n');
        output.append("  Runtime      : ").append(clean(config.runtimeVersion)).append('\n');
        output.append("\nDevice\n");
        output.append("  Manufacturer : ").append(clean(Build.MANUFACTURER)).append('\n');
        output.append("  Brand        : ").append(clean(Build.BRAND)).append('\n');
        output.append("  Model        : ").append(clean(Build.MODEL)).append('\n');
        output.append("  Android      : ").append(clean(Build.VERSION.RELEASE)).append('\n');
        output.append("  SDK          : ").append(Build.VERSION.SDK_INT).append('\n');
        output.append("  Security     : ").append(clean(Build.VERSION.SECURITY_PATCH)).append('\n');
        output.append("  ABI          : ").append(supportedAbis()).append('\n');
        output.append("  Locale       : ").append(clean(Locale.getDefault().toLanguageTag())).append('\n');
        output.append("  Time zone    : ").append(clean(TimeZone.getDefault().getID())).append('\n');
        output.append("\nWebView\n");
        output.append("  Provider     : ").append(webViewProvider()).append('\n');
        output.append("\nViewport Policy\n");
        output.append("  Wide ViewPort : ").append(RuntimeWebViewPolicy.USE_WIDE_VIEW_PORT).append('\n');
        output.append("  Overview Mode : ").append(RuntimeWebViewPolicy.LOAD_WITH_OVERVIEW_MODE).append('\n');
        DisplayMetrics metrics = context.getResources().getDisplayMetrics();
        output.append("\nDisplay\n");
        output.append("  Width px     : ").append(metrics.widthPixels).append('\n');
        output.append("  Height px    : ").append(metrics.heightPixels).append('\n');
        output.append("  Density      : ").append(metrics.density).append('\n');
        output.append("  Density DPI  : ").append(metrics.densityDpi).append('\n');
        output.append("\nNetwork\n");
        output.append(networkSummary(context));
        output.append("\nApplication\n");
        output.append("  Mode         : ").append(config.mode).append('\n');
        output.append("  Start        : ").append(RuntimeLog.safeUrl(config.startUrl())).append('\n');
        output.append("  Allow HTTP   : ").append(config.allowHttp).append('\n');
        output.append("  Mixed Content: ")
                .append(config.allowHttp ? "ALWAYS_ALLOW" : "NEVER")
                .append('\n');
        output.append("  Fullscreen   : ").append(config.fullscreen).append('\n');
        output.append("  Orientation  : ").append(config.orientation).append('\n');
        output.append("==================================================\n\n");
        return output.toString();
    }

    private static String applicationVersion(Context context) {
        try {
            PackageInfo info = context.getPackageManager().getPackageInfo(context.getPackageName(), 0);
            long versionCode = Build.VERSION.SDK_INT >= Build.VERSION_CODES.P
                    ? info.getLongVersionCode()
                    : info.versionCode;
            return clean(info.versionName) + " (" + versionCode + ")";
        } catch (PackageManager.NameNotFoundException | RuntimeException error) {
            return "unavailable";
        }
    }

    private static String supportedAbis() {
        if (Build.SUPPORTED_ABIS == null || Build.SUPPORTED_ABIS.length == 0) return "unavailable";
        StringBuilder result = new StringBuilder();
        for (String abi : Build.SUPPORTED_ABIS) {
            if (result.length() > 0) result.append(", ");
            result.append(clean(abi));
        }
        return result.toString();
    }

    private static String webViewProvider() {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.O) return "unavailable (Android API < 26)";
        try {
            PackageInfo provider = WebView.getCurrentWebViewPackage();
            if (provider == null) return "unavailable";
            return clean(provider.packageName) + " " + clean(provider.versionName);
        } catch (RuntimeException error) {
            return "unavailable";
        }
    }

    private static String networkSummary(Context context) {
        StringBuilder result = new StringBuilder();
        try {
            ConnectivityManager manager =
                    (ConnectivityManager) context.getSystemService(Context.CONNECTIVITY_SERVICE);
            Network active = manager == null ? null : manager.getActiveNetwork();
            NetworkCapabilities capabilities =
                    manager == null || active == null ? null : manager.getNetworkCapabilities(active);
            result.append("  Connected    : ").append(capabilities != null).append('\n');
            result.append("  Validated    : ")
                    .append(capabilities != null &&
                            capabilities.hasCapability(NetworkCapabilities.NET_CAPABILITY_VALIDATED))
                    .append('\n');
            result.append("  Transport    : ").append(transports(capabilities)).append('\n');
        } catch (RuntimeException error) {
            result.append("  Status       : unavailable\n");
        }
        return result.toString();
    }

    private static String transports(NetworkCapabilities capabilities) {
        if (capabilities == null) return "none";
        StringBuilder value = new StringBuilder();
        appendTransport(value, capabilities, NetworkCapabilities.TRANSPORT_WIFI, "WIFI");
        appendTransport(value, capabilities, NetworkCapabilities.TRANSPORT_CELLULAR, "CELLULAR");
        appendTransport(value, capabilities, NetworkCapabilities.TRANSPORT_ETHERNET, "ETHERNET");
        appendTransport(value, capabilities, NetworkCapabilities.TRANSPORT_VPN, "VPN");
        appendTransport(value, capabilities, NetworkCapabilities.TRANSPORT_BLUETOOTH, "BLUETOOTH");
        return value.length() == 0 ? "other" : value.toString();
    }

    private static void appendTransport(
            StringBuilder output, NetworkCapabilities capabilities, int transport, String name) {
        if (!capabilities.hasTransport(transport)) return;
        if (output.length() > 0) output.append(", ");
        output.append(name);
    }

    private static String clean(String value) {
        if (value == null || value.isEmpty()) return "unknown";
        String cleaned = value.replace('\r', ' ').replace('\n', ' ').trim();
        return cleaned.length() <= 256 ? cleaned : cleaned.substring(0, 256) + "...";
    }

    private static void rotate(File directory, File current) throws IOException {
        for (int index = MAX_ARCHIVES; index >= 1; --index) {
            File source = index == 1
                    ? current
                    : new File(directory, FILE_NAME + "." + (index - 1));
            File destination = new File(directory, FILE_NAME + "." + index);
            if (!source.exists()) continue;
            if (destination.exists() && !destination.delete()) {
                throw new IOException("Unable to delete old device log archive: " + destination);
            }
            if (!source.renameTo(destination)) {
                throw new IOException("Unable to rotate device log: " + source);
            }
        }
    }
}
