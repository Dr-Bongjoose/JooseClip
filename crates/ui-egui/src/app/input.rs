use super::*;

impl FilmcraftApp {
    /// Import dropped files, on the main window and on dock panels in their own OS windows.
    /// With Timeline ▸ drop imports to the timeline, the new items are also queued to be placed
    /// at the pointer (see `timeline::interact`).
    pub(super) fn handle_drops(&mut self, ctx: &egui::Context) {
        let to_timeline = self.session.prefs.timeline.drop_import_to_timeline;
        // While files hover, the OS sends no cursor events: poll the cursor each frame to draw the Timeline preview.
        let hovering = ctx.input(|i| i.raw.hovered_files.len());
        self.file_drag_hover = None;
        if hovering > 0 && to_timeline {
            ctx.request_repaint();
            self.file_drag_hover = self.drop_pointer(ctx, egui::ViewportId::ROOT, None).map(|p| (hovering, p, egui::ViewportId::ROOT));
        }
        let dropped = ctx.input(|i| i.raw.dropped_files.clone());
        if !dropped.is_empty() {
            let pointer = self.drop_pointer(ctx, egui::ViewportId::ROOT, None);
            self.import_dropped(&dropped, pointer.map(|p| (p, egui::ViewportId::ROOT)));
        }
        for files in std::mem::take(&mut self.window_files) {
            // Only the Timeline places files at the pointer; elsewhere they go to the bin.
            let at_timeline = to_timeline && files.panel == PanelKind::Timeline;
            let pointer = if at_timeline { self.drop_pointer(ctx, files.viewport, files.pos) } else { None };
            if !files.hovered.is_empty() && self.file_drag_hover.is_none() && at_timeline {
                ctx.request_repaint();
                self.file_drag_hover = pointer.map(|p| (files.hovered.len(), p, files.viewport));
            }
            if !files.dropped.is_empty() {
                self.import_dropped(&files.dropped, pointer.map(|p| (p, files.viewport)));
            }
        }
    }

    /// Import the dropped files that exist; with a pointer and Timeline ▸ drop imports to the
    /// timeline, queue the new items for the Timeline drawn in that viewport. A dropped folder
    /// expands to its importable contents (`folder_import`); the drop reports what it did and
    /// why — it never dies silently (the fork's d909284 semantics, re-landed on the importer).
    fn import_dropped(&mut self, dropped: &[egui::DroppedFileHandle], pointer: Option<(egui::Pos2, egui::ViewportId)>) {
        let mut paths = Vec::new();
        let mut rejected = Vec::new();
        for f in dropped {
            let p = f.path();
            if !p.exists() {
                rejected.push(format!("{} (not on disk)", p.display()));
            } else if p.is_dir() {
                let found = folder_import(&p, 0);
                if found.is_empty() {
                    rejected.push(format!("{} (a folder without importable files)", p.display()));
                }
                paths.extend(found.iter().map(|p| p.to_string_lossy().to_string()));
            } else if p.extension().and_then(|e| e.to_str()).is_some_and(|e| e.eq_ignore_ascii_case("fcproj")) {
                rejected.push(format!("{} (a project file: open it with File ▸ Open instead of dropping it into a project)", p.display()));
            } else {
                paths.push(p.to_string_lossy().to_string());
            }
        }
        if !paths.is_empty() {
            let imported = self.session.execute("file.import", json!({"paths": paths, "bin": self.import_bin().0}));
            match imported {
                Ok(v) => {
                    let n = |v: &Value, key: &str| v.get(key).and_then(Value::as_array).map_or(0, |a| a.len());
                    let (ok_n, bad_n) = (n(&v, "items"), n(&v, "errors"));
                    if ok_n > 0 && bad_n > 0 {
                        self.ui.status = format!("Imported {ok_n} item(s); {bad_n} file(s) failed");
                    } else if ok_n > 0 {
                        self.ui.status = format!("Imported {ok_n} item(s) from the drop");
                    } else {
                        // every path failed (the engine also returns this as Err; belt and braces)
                        self.ui.status = format!(
                            "Nothing imported: {}",
                            v.get("errors").and_then(Value::as_array).map_or("the dropped file(s) are not importable".to_string(), |a| a
                                .iter()
                                .filter_map(Value::as_str)
                                .next()
                                .unwrap_or("not importable")
                                .to_string())
                        );
                    }
                    if ok_n > 0
                        && let Some((pos, viewport)) = pointer
                        && self.session.prefs.timeline.drop_import_to_timeline
                    {
                        let items: Vec<filmcraft_engine::project::ItemId> =
                            v["items"].as_array().map(|a| a.iter().filter_map(|i| i.as_u64()).map(filmcraft_engine::project::ItemId).collect()).unwrap_or_default();
                        if !items.is_empty() {
                            self.pending_timeline_drop = Some((items, pos, viewport));
                        }
                    }
                }
                Err(e) => self.ui.status = format!("Import failed: {e}"),
            }
        }
        if !rejected.is_empty() {
            let lead = !paths.is_empty();
            let msg = format!("Dropped, not imported: {}", rejected.join("; "));
            self.ui.status = if lead {
                let cur = self.ui.status.clone();
                format!("{cur}; {msg}")
            } else {
                msg
            };
        }
    }

    /// Where files dragged in from the OS are over `viewport`. The OS reports no cursor motion during such a drag
    /// (winit, Windows), so ask it through the host hook, else use `last` (a native window's last
    /// pointer position), else the last pointer position egui saw in the main window.
    fn drop_pointer(&self, ctx: &egui::Context, viewport: egui::ViewportId, last: Option<egui::Pos2>) -> Option<egui::Pos2> {
        let os_pointer = self
            .hooks
            .cursor_screen_position
            .as_ref()
            .and_then(|f| f())
            .and_then(|px| ctx.input_for(viewport, |i| i.viewport().inner_rect.map(|r| screen_px_to_ui(px, i.pixels_per_point, r.min))));
        let fallback = || if viewport == egui::ViewportId::ROOT { ctx.input(|i| i.pointer.hover_pos().or(i.pointer.latest_pos())) } else { None };
        os_pointer.or(last).or_else(fallback)
    }

    /// Whether keyboard focus is on any dock divider or floating-window resize grip, as drawn last pass.
    pub(super) fn divider_focused(&self, ctx: &egui::Context) -> bool {
        use craft_ui::docking::{DockArea, Hit};
        let Some(focused) = ctx.memory(|m| m.focused()) else { return false };
        let area = dock_ui::area_id();
        DockArea::<PanelKind>::hit_rects(ctx, area)
            .iter()
            .any(|(hit, _)| matches!(hit, Hit::Divider(_) | Hit::Resize(..)) && DockArea::hit_id(area, hit) == focused)
    }

    /// Remember the undo history as a mouse press begins, before any panel acts on it (a color
    /// picker sets its color on the press itself), for [`Self::cancel_value_drag`].
    pub(super) fn begin_press(&mut self, ctx: &egui::Context) {
        match ctx.input(|i| i.pointer.press_start_time().filter(|_| i.pointer.any_down())) {
            None => self.value_press = None,
            Some(p) if self.value_press.is_some_and(|(q, _)| q == p) => {}
            Some(p) => self.value_press = Some((p, self.session.undo_mark())),
        }
    }

    /// Latch Escape during pass 0, where the key event is fresh.
    pub(super) fn latch_escape_cancel(&mut self, ctx: &egui::Context) {
        if ctx.input(|i| i.pointer.any_down() && i.key_pressed(egui::Key::Escape)) {
            self.escape_cancel = true;
        }
    }

    /// Escape while a mouse button is held takes back every edit this press made (#580): value
    /// drags (one merged step or one step per change), color picks, anything committed on the
    /// Escape frame. Called once after the final non-discarded UI pass; the latch outlives a
    /// discarded pass, and pass 1+ has no Escape key event to re-read.
    pub(super) fn cancel_value_drag(&mut self, _ctx: &egui::Context) {
        if !self.escape_cancel {
            return;
        }
        self.escape_cancel = false;
        if let Some((press, mark)) = self.value_press
            && self.session.revert_to(mark) > 0
        {
            self.value_press = Some((press, self.session.undo_mark()));
        }
    }
}

/// A physical-pixel screen position as a point in the window's UI space, given the window's content origin in points.
///
/// Assumes the whole virtual screen uses the window's `pixels_per_point`: with monitors of mixed
/// DPI the point can be off when the cursor and the window's origin are on different monitors.
fn screen_px_to_ui(px: (i32, i32), pixels_per_point: f32, content_min: egui::Pos2) -> egui::Pos2 {
    let ppp = if pixels_per_point.is_finite() && pixels_per_point > 0.0 { pixels_per_point } else { 1.0 };
    egui::pos2(px.0 as f32 / ppp - content_min.x, px.1 as f32 / ppp - content_min.y)
}

/// Document files `file.import` reads from bytes (captions, interchange): kept when walking a
/// dropped folder. Direct file drops pass through unfiltered (the engine detects by reading).
const FOLDER_DOC_EXTENSIONS: &[&str] = &["srt", "vtt", "scc", "webvtt", "mcc", "stl", "ttml", "dfxp", "xml", "edl", "fcpxml", "otio", "aaf", "omf", "omfi"];

/// The files under a dropped folder that `file.import` handles: media (the engine's own
/// extension lists) and document files (captions, interchange), sorted for stable item order.
/// The walk is depth- and count-capped (AGENTS.md §0.4/§5: input-derived sizes are hostile);
/// an unreadable folder yields nothing instead of an error — the drop still reports.
fn folder_import(dir: &std::path::Path, depth: u8) -> Vec<std::path::PathBuf> {
    const MAX_DEPTH: u8 = 8;
    const MAX_FILES: usize = 512;
    if depth >= MAX_DEPTH {
        return Vec::new();
    }
    let mut out = Vec::new();
    if let Ok(entries) = std::fs::read_dir(dir) {
        let mut subdirs = Vec::new();
        for entry in entries.flatten() {
            if out.len() >= MAX_FILES {
                break;
            }
            let path = entry.path();
            if path.is_dir() {
                subdirs.push(path);
                continue;
            }
            let doc = path.extension().and_then(|e| e.to_str()).map(|e| FOLDER_DOC_EXTENSIONS.contains(&e.to_ascii_lowercase().as_str()));
            if filmcraft_media::is_importable(&path) || doc == Some(true) {
                out.push(path);
            }
        }
        for d in subdirs {
            if out.len() >= MAX_FILES {
                break;
            }
            out.extend(folder_import(&d, depth + 1));
        }
    }
    out.sort();
    out
}

#[cfg(test)]
mod screen_px_tests {
    use super::screen_px_to_ui;

    #[test]
    fn maps_screen_pixels_into_the_window() {
        let p = screen_px_to_ui((1300, 900), 2.0, egui::pos2(100.0, 50.0));
        assert_eq!((p.x, p.y), (550.0, 400.0));
    }

    #[test]
    fn a_hostile_scale_does_not_divide_by_zero() {
        for ppp in [0.0, -1.0, f32::NAN, f32::INFINITY] {
            let p = screen_px_to_ui((10, 20), ppp, egui::pos2(0.0, 0.0));
            assert!(p.x.is_finite() && p.y.is_finite());
        }
    }
}

#[cfg(test)]
mod folder_import_tests {
    use super::*;

    /// A dropped folder imports its media and caption/interchange files, nested, sorted.
    #[test]
    fn walks_media_and_documents() {
        let d = std::env::temp_dir().join(format!("jc-folder-import-{}", std::process::id()));
        let nested = d.join("b-roll");
        std::fs::create_dir_all(&nested).unwrap();
        std::fs::write(d.join("a.mp4"), b"x").unwrap();
        std::fs::write(d.join("c.MOV"), b"x").unwrap();
        std::fs::write(d.join("subs.srt"), b"x").unwrap();
        std::fs::write(d.join("notes.txt"), b"x").unwrap();
        std::fs::write(d.join("sheet.csv"), b"x").unwrap();
        std::fs::write(nested.join("nested.webm"), b"x").unwrap();
        let got = folder_import(&d, 0);
        let names: Vec<String> = got.iter().map(|p| p.file_name().unwrap().to_string_lossy().to_string()).collect();
        assert_eq!(names, ["a.mp4", "nested.webm", "c.MOV", "subs.srt"]);
        let _ = std::fs::remove_dir_all(&d);
    }

    /// Depth and count caps hold: a hostile tree (self-referencing depth) stays bounded.
    #[test]
    fn caps_are_respected() {
        // cap: more importable files than MAX_FILES → at most MAX_FILES returned
        let d = std::env::temp_dir().join(format!("jc-folder-cap-{}", std::process::id()));
        std::fs::create_dir_all(&d).unwrap();
        for i in 0..600 {
            std::fs::write(d.join(format!("v{i:03}.mp4")), b"x").unwrap();
        }
        let got = folder_import(&d, 0);
        assert!(got.len() <= 512, "got {}", got.len());
        let _ = std::fs::remove_dir_all(&d);
    }

    /// An unreadable folder (deleted race, permissions) yields nothing, not an error state.
    #[test]
    fn unreadable_folder_is_empty() {
        assert!(folder_import(std::path::Path::new("/jx/definitely/not/here"), 0).is_empty());
        assert!(folder_import(std::path::Path::new("/tmp"), 255).is_empty());
    }
}
