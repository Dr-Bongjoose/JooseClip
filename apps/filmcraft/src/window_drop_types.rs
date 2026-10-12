//! macOS window drag-and-drop: import video files dragged from the Finder.
//!
//! winit 0.30's macOS windows register exactly one drag type at creation: the deprecated
//! `NSFilenamesPboardType` (window_delegate.rs, "register for drag and drop operations").
//! Modern macOS promises dragged files as `NSPasteboardTypeFileURL` (file URLs); on systems
//! where the Finder no longer promises the legacy filenames type, the window rejects every
//! drag before winit sees it and drops into the editor did nothing. There is nothing to set
//! in winit's attributes, so once the window exists we re-register both types ourselves
//! (`registerForDraggedTypes:` replaces the list, every window, once per launch).
//!
//! The calls mirror winit's own macOS registration line for line (same objc2 family), with
//! both types instead of the one. Plain objc2 API: this crate keeps the workspace's no-
//! `unsafe` rule (AGENTS.md §0.3) — only the deprecated C constant is an unsafe read, shaped
//! exactly as winit does it. Non-macOS systems: winit's registration is untouched.

/// Re-register the Finder drag types on every window; returns how many content views got them.
#[cfg_attr(not(target_os = "macos"), allow(dead_code))]
pub fn register_types() -> Result<usize, String> {
    #[cfg(target_os = "macos")]
    {
        use objc2_app_kit::{NSApplication, NSFilenamesPboardType, NSPasteboardTypeFileURL};
        use objc2_foundation::NSArray;

        let Some(mtm) = objc2::MainThreadMarker::new() else {
            // The attempt runs on the first frame (the main thread); anything else is a bug.
            return Err("drop types not registered: not on the main thread".into());
        };
        let app = NSApplication::sharedApplication(mtm);
        let mut registered = 0usize;
        for w in app.windows() {
            // Drag delivery is on the content view (NSWindow's deprecated variant forwards there).
            if let Some(view) = w.contentView() {
                view.registerForDraggedTypes(&NSArray::from_id_slice(&[
                    NSPasteboardTypeFileURL.copy(),
                    // SAFETY: the deprecated legacy filenames type is an `unsafe` C constant in
                    // the bindings; winit itself reads it like this at window creation.
                    unsafe { NSFilenamesPboardType }.copy(),
                ]));
                registered += 1;
            }
        }
        if registered == 0 {
            return Err("drop types not registered: no window content views yet".into());
        }
        Ok(registered)
    }
    #[cfg(not(target_os = "macos"))]
    {
        Err(String::from("no extra drop types needed on this system"))
    }
}
