//! Community and project links (Help menu, About dialog, header Discord button, Home screen).
//!
//! Joose Clip is an independent fork of FilmCraft by the ArtCraft team: the fork lives on Joose
//! Labs' own site and repository, and credits the upstream project.

/// The app's short name, used in the website and repository URLs.
pub const APP: &str = "joose-clip";

/// The Joose Labs community chat (placeholder until the fork has its own room).
pub const DISCORD: &str = "https://jooselabs.com/";
/// The fork's website.
pub const WEBSITE: &str = "https://jooselabs.com";
/// Joose Clip's page on the Joose Labs website.
pub const APP_PAGE: &str = "https://jooselabs.com/joose-clip";
/// Joose Clip's source repository.
pub const GITHUB: &str = "https://github.com/JooseLabs/joose-clip";
/// New issue on Joose Clip's repository.
pub const ISSUES: &str = "https://github.com/JooseLabs/joose-clip/issues";
/// The upstream FilmCraft project (this fork's base). Linking the upstream is required by the
/// fork's attribution rules.
pub const UPSTREAM: &str = "https://github.com/storytold/filmcraft";

/// (command id, label, url) for every link, in menu order.
pub const ALL: [(&str, &str, &str); 5] = [
    ("help.discord", "Join the Joose Labs chat…", DISCORD),
    ("help.website", "Joose Labs Website", WEBSITE),
    ("help.appPage", "Joose Clip on jooselabs.com", APP_PAGE),
    ("help.github", "Joose Clip on GitHub", GITHUB),
    ("help.reportIssue", "Report an Issue…", ISSUES),
];

/// The URL a `help.*` link command opens.
pub fn url_for(command: &str) -> Option<&'static str> {
    ALL.iter().find(|(id, _, _)| *id == command).map(|(_, _, u)| *u)
}

/// Open `url` in the system browser (a new tab on the web). The desktop host's
/// [`HostHooks::open_url`](crate::HostHooks::open_url) opens it; without the hook, or when it
/// fails, it goes through `ctx.open_url`, which only does something on the web: the native egui
/// backend is built without its `links` feature, so there it would only log (#642).
pub fn open(app: &mut crate::FilmcraftApp, ctx: &egui::Context, url: &str) {
    if let Some(f) = app.hooks.open_url.as_mut() {
        match f(url) {
            Ok(()) => return,
            Err(e) => log::warn!("can't open {url} in the browser: {e}"),
        }
    }
    ctx.open_url(egui::OpenUrl::new_tab(url));
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn urls_follow_the_joose_scheme() {
        assert_eq!(APP_PAGE, format!("{WEBSITE}/{APP}"));
        assert_eq!(GITHUB, format!("https://github.com/JooseLabs/{APP}"));
        assert!(ALL.iter().all(|(id, _, u)| id.starts_with("help.") && u.starts_with("https://")));
        assert_eq!(url_for("help.discord"), Some(DISCORD));
        assert_eq!(url_for("help.nope"), None);
    }

    #[test]
    fn links_open_through_the_host_hook() {
        // #642: on the desktop every link (Help menu, About, Discord button, Import screen) must
        // reach the host's browser opener; `ctx.open_url` alone is a no-op natively.
        use std::cell::RefCell;
        use std::rc::Rc;
        let opened: Rc<RefCell<Vec<String>>> = Rc::default();
        let rec = opened.clone();
        let mut app = crate::FilmcraftApp::new(filmcraft_engine::Session::default());
        app.hooks.open_url = Some(Box::new(move |u: &str| {
            rec.borrow_mut().push(u.to_string());
            Ok(())
        }));
        let ctx = egui::Context::default();
        for (id, _, url) in ALL {
            let r = crate::menus::invoke(&mut app, &ctx, id, serde_json::json!({})).unwrap();
            assert_eq!(r["url"], url, "{id}");
            assert_eq!(opened.borrow().last().map(String::as_str), Some(url), "{id}");
        }
        open(&mut app, &ctx, DISCORD);
        assert_eq!(opened.borrow().last().map(String::as_str), Some(DISCORD));
        assert_eq!(opened.borrow().len(), ALL.len() + 1);

        // A failing opener falls back to `ctx.open_url` instead of losing the click.
        app.hooks.open_url = Some(Box::new(|_: &str| Err("no browser".into())));
        open(&mut app, &ctx, ISSUES);
        let fallback: Vec<String> =
            ctx.output(|o| o.commands.iter().filter_map(|c| if let egui::OutputCommand::OpenUrl(u) = c { Some(u.url.clone()) } else { None }).collect());
        assert_eq!(fallback, [ISSUES]);
    }
}
