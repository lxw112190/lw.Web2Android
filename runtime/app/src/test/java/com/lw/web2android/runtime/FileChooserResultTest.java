package com.lw.web2android.runtime;

import static org.junit.Assert.*;

import android.app.Activity;
import android.content.ClipData;
import android.content.Intent;
import android.net.Uri;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.robolectric.RobolectricTestRunner;
import org.robolectric.annotation.Config;

/** Real Android Intent/ClipData objects on the host; no SDK installation on end-user PCs. */
@RunWith(RobolectricTestRunner.class)
@Config(manifest = Config.NONE, sdk = 28)
public final class FileChooserResultTest {
    private static final Uri FIRST = Uri.parse("content://photos/private/one?secret=value");
    private static final Uri SECOND = Uri.parse("content://photos/private/two");

    private Intent clipIntent(Uri... uris) {
        ClipData clip = ClipData.newRawUri("selection", uris[0]);
        for (int i = 1; i < uris.length; i++) clip.addItem(new ClipData.Item(uris[i]));
        Intent intent = new Intent();
        intent.setClipData(clip);
        return intent;
    }

    @Test public void singleDataUri() {
        FileChooserResult result = FileChooserResult.parse(Activity.RESULT_OK, new Intent().setData(FIRST));
        assertArrayEquals(new Uri[] {FIRST}, result.uris());
        assertEquals("accepted", result.status());
    }

    @Test public void singleImageInClipDataOnly() {
        FileChooserResult result = FileChooserResult.parse(Activity.RESULT_OK, clipIntent(FIRST));
        assertArrayEquals(new Uri[] {FIRST}, result.uris());
        assertFalse(result.hasDataUri);
        assertEquals(1, result.clipItems);
    }

    @Test public void multipleImagesInClipDataOnly() {
        assertArrayEquals(new Uri[] {FIRST, SECOND},
                FileChooserResult.parse(Activity.RESULT_OK, clipIntent(FIRST, SECOND)).uris());
    }

    @Test public void mergeBothChannelsAndDeduplicateInOrder() {
        FileChooserResult result = FileChooserResult.parse(Activity.RESULT_OK,
                clipIntent(FIRST, SECOND, SECOND).setData(FIRST));
        assertArrayEquals(new Uri[] {FIRST, SECOND}, result.uris());
        assertEquals(2, result.duplicates);
    }

    @Test public void cancelAndNonSuccessIgnoreEvenPopulatedResults() {
        Intent intent = clipIntent(FIRST).setData(SECOND);
        FileChooserResult canceled = FileChooserResult.parse(Activity.RESULT_CANCELED, intent);
        assertEquals(0, canceled.uris().length);
        assertEquals("canceled", canceled.status());
        FileChooserResult failed = FileChooserResult.parse(42, intent);
        assertEquals(0, failed.uris().length);
        assertEquals("non-success", failed.status());
    }

    @Test public void nullOrEmptyResult() {
        assertEquals("empty", FileChooserResult.parse(Activity.RESULT_OK, null).status());
        assertEquals("empty", FileChooserResult.parse(Activity.RESULT_OK, new Intent()).status());
    }

    @Test public void rejectFileNetworkAndTextItemsButKeepContent() {
        Intent intent = clipIntent(Uri.parse("file:///sdcard/private.jpg"),
                Uri.parse("https://example.com/image.png"), FIRST);
        intent.getClipData().addItem(new ClipData.Item("not a URI"));
        FileChooserResult result = FileChooserResult.parse(Activity.RESULT_OK, intent);
        assertArrayEquals(new Uri[] {FIRST}, result.uris());
        assertEquals(3, result.rejected);
        assertEquals("filtered", FileChooserResult.parse(Activity.RESULT_OK,
                new Intent().setData(Uri.parse("file:///sdcard/test.jpg"))).status());
    }

    @Test public void capAtTwentyUniqueContentUris() {
        Intent intent = clipIntent(FIRST);
        for (int i = 0; i < 30; i++) {
            intent.getClipData().addItem(new ClipData.Item(Uri.parse("content://photos/" + i)));
        }
        FileChooserResult result = FileChooserResult.parse(Activity.RESULT_OK, intent);
        assertEquals(20, result.uris().length);
        assertEquals(Uri.parse("content://photos/18"), result.uris()[19]);
    }

    @Test public void diagnosticsDoNotContainUrisPathsOrQuerySecrets() {
        String summary = FileChooserResult.parse(Activity.RESULT_OK, clipIntent(FIRST)).logSummary();
        assertTrue(summary.contains("clipItems=1"));
        assertFalse(summary.contains("content://"));
        assertFalse(summary.contains("private"));
        assertFalse(summary.contains("secret"));
    }
}
