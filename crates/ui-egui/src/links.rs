//! Community and project links (Help menu, About dialog, header Discord button, Home screen).
//!
//! Joose Clip is an independent fork of FilmCraft by the ArtCraft team: the fork lives on Joose
//! Labs' own site and repository, and credits the upstream project.

/// The app's short name, used in the website and repository URLs.
pub const APP: &str = "joose-clip";

/// The Joose Labs community chat (placeholder until the fork has its own room).
pub const DISCORD: &str = "https://jooselabs.com/";
/// The fork's website.
pub const WEBSITE: &str = "https://jooselabs.com/";
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

/// Open `url` in the system browser (a new tab on the web).
pub fn open(ctx: &egui::Context, url: &str) {
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
}
