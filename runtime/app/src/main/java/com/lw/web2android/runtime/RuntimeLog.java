package com.lw.web2android.runtime;

import android.content.Context;
import android.net.Uri;
import android.os.Process;
import android.util.Log;

import java.io.BufferedWriter;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.OutputStreamWriter;
import java.io.PrintWriter;
import java.io.StringWriter;
import java.nio.charset.StandardCharsets;
import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.Locale;
import java.util.TimeZone;

/** Synchronous Runtime diagnostics with bounded, app-private external storage. */
final class RuntimeLog {
    private static final String TAG = "lw.Web2Android";
    private static final long MAX_FILE_SIZE = 2L * 1024L * 1024L;
    private static final int MAX_ARCHIVES = 5;
    private static final int MAX_MESSAGE_LENGTH = 64 * 1024;
    private static final Object INSTANCE_LOCK = new Object();

    private static RuntimeLog instance;
    private static boolean crashHandlerInstalled;

    private final File directory;
    private final File currentFile;
    private final SimpleDateFormat timestamp;
    private BufferedWriter writer;

    private RuntimeLog(Context context) throws IOException {
        directory = resolveLogDirectory(context);
        currentFile = new File(directory, "runtime.log");
        timestamp = new SimpleDateFormat("yyyy-MM-dd'T'HH:mm:ss.SSS'Z'", Locale.US);
        timestamp.setTimeZone(TimeZone.getTimeZone("UTC"));
        openWriter();
    }

    static File resolveLogDirectory(Context context) throws IOException {
        File selected = context.getExternalFilesDir("logs");
        if (selected == null) {
            selected = new File(context.getFilesDir(), "logs");
        }
        if ((!selected.isDirectory() && !selected.mkdirs()) || !selected.isDirectory()) {
            throw new IOException("Unable to create Runtime log directory: " + selected);
        }
        return selected;
    }

    static void initialize(Context context) {
        synchronized (INSTANCE_LOCK) {
            if (instance != null) {
                instance.info("Runtime logger reused");
                return;
            }
            try {
                instance = new RuntimeLog(context.getApplicationContext());
                instance.info("Runtime logger initialized: " + instance.currentFile.getAbsolutePath());
            } catch (IOException error) {
                Log.e(TAG, "Unable to initialize Runtime file logging", error);
            }
        }
    }

    static void installCrashHandler() {
        synchronized (INSTANCE_LOCK) {
            if (crashHandlerInstalled) return;
            final Thread.UncaughtExceptionHandler previous = Thread.getDefaultUncaughtExceptionHandler();
            Thread.setDefaultUncaughtExceptionHandler((thread, error) -> {
                RuntimeLog.error("Uncaught exception on thread " + thread.getName(), error);
                RuntimeLog.flush();
                if (previous != null) {
                    previous.uncaughtException(thread, error);
                }
            });
            crashHandlerInstalled = true;
        }
    }

    static void debug(String message) {
        Log.d(TAG, message);
        write("DEBUG", message, null);
    }

    static void info(String message) {
        Log.i(TAG, message);
        write("INFO", message, null);
    }

    static void warn(String message) {
        Log.w(TAG, message);
        write("WARN", message, null);
    }

    static void error(String message) {
        Log.e(TAG, message);
        write("ERROR", message, null);
    }

    static void error(String message, Throwable error) {
        Log.e(TAG, message, error);
        write("ERROR", message, error);
    }

    static void flush() {
        RuntimeLog logger = instance;
        if (logger == null) return;
        synchronized (logger) {
            try {
                if (logger.writer != null) logger.writer.flush();
            } catch (IOException error) {
                Log.e(TAG, "Unable to flush Runtime log", error);
            }
        }
    }

    static String directoryPath() {
        RuntimeLog logger = instance;
        return logger == null ? "Logcat only (file logger unavailable)" : logger.directory.getAbsolutePath();
    }

    static String safeUrl(String rawUrl) {
        if (rawUrl == null || rawUrl.isEmpty()) return "";
        try {
            Uri uri = Uri.parse(rawUrl);
            if (uri.getScheme() != null && uri.getHost() != null) {
                String host = uri.getHost();
                if (host.indexOf(':') >= 0 && !host.startsWith("[")) host = "[" + host + "]";
                String authority = uri.getPort() < 0 ? host : host + ":" + uri.getPort();
                Uri.Builder sanitized = new Uri.Builder()
                        .scheme(uri.getScheme())
                        .encodedAuthority(authority);
                if (uri.getEncodedPath() != null) sanitized.encodedPath(uri.getEncodedPath());
                return sanitized.build().toString();
            }
            return uri.buildUpon().clearQuery().fragment(null).build().toString();
        } catch (RuntimeException ignored) {
            return "<invalid-url>";
        }
    }

    private static void write(String level, String message, Throwable error) {
        RuntimeLog logger = instance;
        if (logger == null) return;
        logger.writeLine(level, message, error);
    }

    private synchronized void writeLine(String level, String message, Throwable error) {
        try {
            String payload = message == null ? "" : message;
            if (error != null) {
                StringWriter stack = new StringWriter();
                error.printStackTrace(new PrintWriter(stack));
                payload += "\n" + stack;
            }
            if (payload.length() > MAX_MESSAGE_LENGTH) {
                payload = payload.substring(0, MAX_MESSAGE_LENGTH) + "\n<truncated>";
            }
            String line = timestamp.format(new Date()) + " [" + level + "] [pid "
                    + Process.myPid() + "] [thread " + Thread.currentThread().getName() + "] "
                    + payload + System.lineSeparator();
            byte[] encoded = line.getBytes(StandardCharsets.UTF_8);
            if (currentFile.length() > 0L && currentFile.length() + encoded.length > MAX_FILE_SIZE) {
                rotate();
            }
            if (writer == null) openWriter();
            writer.write(line);
            writer.flush();
        } catch (IOException | RuntimeException loggingError) {
            Log.e(TAG, "Unable to write Runtime log", loggingError);
        }
    }

    private void rotate() throws IOException {
        closeWriter();
        for (int index = MAX_ARCHIVES; index >= 1; --index) {
            File source = index == 1
                    ? currentFile
                    : new File(directory, "runtime.log." + (index - 1));
            File destination = new File(directory, "runtime.log." + index);
            if (!source.exists()) continue;
            if (destination.exists() && !destination.delete()) {
                throw new IOException("Unable to delete old Runtime log archive: " + destination);
            }
            if (!source.renameTo(destination)) {
                throw new IOException("Unable to rotate Runtime log: " + source);
            }
        }
        openWriter();
    }

    private void openWriter() throws IOException {
        writer = new BufferedWriter(new OutputStreamWriter(
                new FileOutputStream(currentFile, true), StandardCharsets.UTF_8));
    }

    private void closeWriter() throws IOException {
        if (writer == null) return;
        writer.flush();
        writer.close();
        writer = null;
    }
}
