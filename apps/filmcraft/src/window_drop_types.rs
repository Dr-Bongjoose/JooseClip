//! macOS window drag-and-drop: import video files dragged from the Finder.
//!
//! winit 0.30's macOS windows register exactly one drag type at creation: the deprecated
//! `NSFilenamesPboardType` (winit window_delegate.rs, "register for drag and drop
//! operations"). Modern macOS promises dropped files as `NSPasteboardTypeFileURL` (file
//! URLs); where the Finder no longer promises the legacy type, the window rejects every
//! drag before winit sees it and dropping files into the editor did nothing. There is
//! nothing to set in winit's attributes, so once the window exists we re-register with the
//! modern file-URL type added (`registerForDraggedTypes:` replaces the list; the legacy
//! filenames registration stays in winit's own list, so both end up covered — adding the
//! legacy constant here is impossible anyway: it is an `unsafe` static and this crate is
//! `unsafe_code = "forbid"`).
//!
//! Plain objc2 API, no `unsafe` in the app crate (AGENTS.md §0.3). Non-macOS systems:
//! winit's registration is untouched.

/// Add the modern Finder drag types to every window; returns how many content views got them.
#[cfg_attr(not(target_os = "macos"), allow(dead_code))]
pub fn register_types() -> Result<usize, String> {
    #[cfg(target_os = "macos")]
    {
        use objc2_app_kit::{NSApplication, NSPasteboardTypeFileURL};
        use objc2_foundation::NSArray;

        let Some(mtm) = objc2::MainThreadMarker::new() else {
            // The attempt runs on the first frame (the main thread); anything else is a bug.
            return Err("drop types not registered: not on the main thread".into());
        };
        // `NSPasteboardTypeFileURL` is a `&'static NSString` constant: straight into the
        // reference-slice constructor.
        let types = NSArray::from_slice(&[NSPasteboardTypeFileURL]);
        let app = NSApplication::sharedApplication(mtm);
        let mut registered = 0usize;
        for w in app.windows() {
            // Drag delivery is on the content view (the NSWindow variant is deprecated and
            // forwards there anyway).
            if let Some(view) = w.contentView() {
                view.registerForDraggedTypes(&types);
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