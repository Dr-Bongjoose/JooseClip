//! macOS window drag-and-drop: import video files dragged from the Finder.
//!
//! winit 0.30's macOS windows register exactly one drag type at creation: the deprecated
//! `NSFilenamesPboardType` (winit window_delegate.rs, "register for drag and drop
//! operations"). Modern macOS promises dropped files as `public.file-url` (file URLs);
//! where the Finder no longer promises the legacy type, the window rejects every drag
//! before winit sees it and dropping files into the editor did nothing. There is nothing
//! to set in winit's attributes, so once the window exists we re-register with the modern
//! type added (`registerForDraggedTypes:` replaces the list; winit keeps its legacy list
//! entry, so both end up covered).
//!
//! The pasteboard is UTI-string keyed; `objc2_foundation::ns_string!` builds the type
//! safely at compile time. Reading the `NSPasteboardTypeFileURL` constant directly is an
//! `unsafe` extern-static read and this crate is `unsafe_code = "forbid"` (AGENTS.md
//! §0.3); the string is the constant's stable UTI value.
//!
//! Plain objc2 API, no `unsafe` in the app crate. Non-macOS systems: winit's registration
//! is untouched.

/// Add the modern Finder drag types to every window; returns how many content views got them.
#[cfg_attr(not(target_os = "macos"), allow(dead_code))]
pub fn register_types() -> Result<usize, String> {
    #[cfg(target_os = "macos")]
    {
        use objc2_app_kit::NSApplication;
        use objc2_foundation::{ns_string, NSArray, NSString};

        let Some(mtm) = objc2::MainThreadMarker::new() else {
            // The attempt runs on the first frame (the main thread); anything else is a bug.
            return Err("drop types not registered: not on the main thread".into());
        };
        // ns_string! caches per thread; it is not const-evaluable (E0015)
        let file_url: &NSString = ns_string!("public.file-url"); // NSPasteboardTypeFileURL
        let types = NSArray::from_slice(&[file_url]);
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