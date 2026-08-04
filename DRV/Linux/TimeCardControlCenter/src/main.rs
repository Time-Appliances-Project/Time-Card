use clap::Parser;
use relm4::RelmApp;
use relm4::adw;
use timecard_control_center::AppConfig;
use timecard_control_center::app::{APP_CSS, App};

fn main() {
    let config = AppConfig::parse();
    let application = RelmApp::new("org.opentimeserver.TimeCardControlCenter");
    adw::StyleManager::default().set_color_scheme(adw::ColorScheme::PreferDark);
    relm4::set_global_css(APP_CSS);

    let program_name = std::env::args()
        .next()
        .unwrap_or_else(|| "timecard-control-center".to_owned());
    application.with_args(vec![program_name]).run::<App>(config);
}
