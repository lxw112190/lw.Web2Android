package com.lw.web2android.runtime;

import static org.junit.Assert.*;
import org.junit.Test;

public final class BackNavigationControllerTest {
    private static final class Host implements BackNavigationController.Host {
        boolean fullscreen;
        int index, backCalls, fullscreenCalls;
        String lastAction, lastSource;
        @Override public boolean fullscreen() { return fullscreen; }
        @Override public boolean canGoBack() { return index > 0; }
        @Override public void exitFullscreen() { fullscreen = false; fullscreenCalls++; }
        @Override public void goBack() { index--; backCalls++; }
        @Override public void logAction(String source, String action) { lastSource = source; lastAction = action; }
    }
    private static final class Registration implements BackNavigationController.Registration {
        boolean enabled;
        int calls;
        @Override public void setEnabled(boolean value) { enabled = value; calls++; }
    }
    @Test public void rootDoesNotInterceptSystemBack() {
        Host h = new Host(); Registration r = new Registration();
        BackNavigationController c = new BackNavigationController(h, r);
        c.refresh();
        assertFalse(r.enabled);
        assertEquals(0, r.calls);
        assertFalse(c.handleBack("legacy"));
        assertEquals("SYSTEM", h.lastAction);
        assertEquals(0, h.backCalls);
    }
    @Test public void oneEventGoesBackExactlyOnePage() {
        Host h = new Host(); h.index = 2; Registration r = new Registration();
        BackNavigationController c = new BackNavigationController(h, r);
        c.refresh();
        assertTrue(r.enabled);
        assertTrue(c.handleBack("platform-invoked"));
        assertEquals(1, h.index);
        assertEquals(1, h.backCalls);
        assertEquals("WEB_HISTORY", h.lastAction);
        assertEquals("platform-invoked", h.lastSource);
        assertTrue(r.enabled);
        assertTrue(c.handleBack("platform-invoked"));
        assertEquals(0, h.index);
        assertFalse(r.enabled);
        assertFalse(c.handleBack("platform-invoked"));
        assertEquals(2, h.backCalls);
    }
    @Test public void fullscreenWinsOverHistory() {
        Host h = new Host(); h.fullscreen = true; h.index = 2; Registration r = new Registration();
        BackNavigationController c = new BackNavigationController(h, r);
        c.refresh();
        assertTrue(c.handleBack("legacy"));
        assertEquals("EXIT_FULLSCREEN", h.lastAction);
        assertEquals(1, h.fullscreenCalls);
        assertEquals(0, h.backCalls);
        assertEquals(2, h.index);
        assertTrue(r.enabled);
    }
    @Test public void rootFullscreenUnregistersAfterExit() {
        Host h = new Host(); h.fullscreen = true; Registration r = new Registration();
        BackNavigationController c = new BackNavigationController(h, r);
        c.refresh();
        assertTrue(r.enabled);
        assertTrue(c.handleBack("platform-invoked"));
        assertFalse(r.enabled);
        assertEquals(2, r.calls);
    }
    @Test public void historyChangeRegistersWithoutPageReload() {
        Host h = new Host(); Registration r = new Registration();
        BackNavigationController c = new BackNavigationController(h, r);
        c.refresh();
        h.index = 1; c.refresh(); c.refresh();
        assertTrue(r.enabled);
        assertEquals(1, r.calls);
        h.index = 0; c.refresh(); c.refresh();
        assertFalse(r.enabled);
        assertEquals(2, r.calls);
    }
    @Test public void staleCallbackAtRootFallsThroughInsteadOfGoingBack() {
        Host h = new Host(); h.index = 1; Registration r = new Registration();
        BackNavigationController c = new BackNavigationController(h, r);
        c.refresh(); h.index = 0;
        assertFalse(c.handleBack("platform-invoked"));
        assertFalse(r.enabled);
        assertEquals(0, h.backCalls);
    }
    @Test public void legacyUsesTheSameHistoryPolicy() {
        Host h = new Host(); h.index = 1; Registration r = new Registration();
        BackNavigationController c = new BackNavigationController(h, r);
        assertTrue(c.handleBack("legacy"));
        assertEquals(1, h.backCalls);
        assertEquals("legacy", h.lastSource);
    }
    @Test public void destroyUnregistersOnceAndPreventsFurtherActions() {
        Host h = new Host(); h.index = 2; Registration r = new Registration();
        BackNavigationController c = new BackNavigationController(h, r);
        c.refresh(); c.destroy(); c.destroy(); c.refresh();
        assertFalse(r.enabled);
        assertEquals(2, r.calls);
        assertFalse(c.handleBack("platform-invoked"));
        assertEquals(0, h.backCalls);
    }
}
