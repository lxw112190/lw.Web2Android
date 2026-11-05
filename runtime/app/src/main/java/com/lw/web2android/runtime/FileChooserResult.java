package com.lw.web2android.runtime;

import android.app.Activity;
import android.content.ClipData;
import android.content.Intent;
import android.net.Uri;

import java.util.ArrayList;
import java.util.List;

/** Reads both Android picker result channels; never resolves paths or broadens permissions. */
final class FileChooserResult {
    static final int MAX_SELECTED_FILES = 20;

    private final List<Uri> accepted = new ArrayList<>();
    final int resultCode;
    boolean hasDataUri;
    int clipItems;
    int rejected;
    int duplicates;

    private FileChooserResult(int resultCode) {
        this.resultCode = resultCode;
    }

    static FileChooserResult parse(int resultCode, Intent data) {
        FileChooserResult result = new FileChooserResult(resultCode);
        // A canceled/non-success result must never deliver even a populated Intent.
        if (resultCode != Activity.RESULT_OK || data == null) return result;
        Uri primary = data.getData();
        result.hasDataUri = primary != null;
        if (primary != null) result.add(primary);
        ClipData clip = data.getClipData();
        result.clipItems = clip == null ? 0 : clip.getItemCount();
        for (int index = 0; index < result.clipItems
                && result.accepted.size() < MAX_SELECTED_FILES; index++) {
            result.add(clip.getItemAt(index).getUri());
        }
        return result;
    }

    private void add(Uri uri) {
        if (uri == null || !"content".equalsIgnoreCase(uri.getScheme())) {
            rejected++;
        } else if (accepted.contains(uri)) {
            duplicates++;
        } else {
            accepted.add(uri);
        }
    }

    Uri[] uris() {
        return accepted.toArray(new Uri[0]);
    }

    String status() {
        if (resultCode == Activity.RESULT_CANCELED) return "canceled";
        if (resultCode != Activity.RESULT_OK) return "non-success";
        if (!accepted.isEmpty()) return "accepted";
        return rejected > 0 ? "filtered" : "empty";
    }

    String logSummary() {
        return "File chooser result; status=" + status() + ", resultCode=" + resultCode
                + ", hasDataUri=" + hasDataUri + ", clipItems=" + clipItems
                + ", selectedFiles=" + accepted.size() + ", rejected=" + rejected
                + ", duplicates=" + duplicates
                + ", limitReached=" + (accepted.size() == MAX_SELECTED_FILES);
    }
}
