use crate::config::WaypointClickAction;
use nexus::imgui::Ui;

pub fn hint(action: WaypointClickAction, link: &str) -> &'static str {
    if action != WaypointClickAction::Copy && crate::waypoint_link::first_waypoint_link(link).is_none() {
        return "No waypoint destination is listed for this event";
    }
    match action {
        WaypointClickAction::Teleport => "Click to teleport directly to the waypoint",
        WaypointClickAction::OpenMap => "Click to open the map at the waypoint (Pie UI)",
        WaypointClickAction::Copy => "Click to copy waypoint",
    }
}
pub fn perform(ui: &Ui, action: WaypointClickAction, name: &str, link: &str, include_name: bool) {
    match action {
        WaypointClickAction::Copy => {
            let text = if include_name { format!("{name}: {link}") } else { link.to_owned() };
            ui.set_clipboard_text(text);
        }
        WaypointClickAction::OpenMap => {
            let destination = crate::waypoint_link::first_waypoint_link(link);
            if !destination.is_some_and(crate::pie_ui::open_waypoint) {
                nexus::alert::send_alert(if crate::pie_ui::available() {
                    "This event does not contain a valid waypoint link."
                } else { "Opening the waypoint map requires the Pie UI addon." });
            }
        }
        WaypointClickAction::Teleport => {
            let Some(id) = crate::waypoint_link::first_waypoint_link(link).and_then(crate::waypoint_link::waypoint_id) else {
                nexus::alert::send_alert("This event does not contain a valid waypoint link."); return;
            };
            if let Err(error) = crate::waypoint_travel::request(id) {
                nexus::alert::send_alert(error.message());
            }
        }
    }
}
