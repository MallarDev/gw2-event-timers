//! Optional Pie UI bridge for opening waypoint chat links in the game map.
use nexus::event::{event_raise, event_raise_notification, event_subscribe};
use std::ffi::{c_char, CString};
use std::sync::atomic::{AtomicBool, Ordering};

static READY: AtomicBool = AtomicBool::new(false);

pub fn load() {
    unsafe {
        event_subscribe!("EV_PIE_UI_READY" => (), |_| {
            READY.store(true, Ordering::Release);
        })
    }
    .revert_on_unload();
    event_raise_notification("EV_PIE_UI_PING");
}

pub fn unload() {
    READY.store(false, Ordering::Release);
}

pub fn available() -> bool {
    READY.load(Ordering::Acquire)
}

pub fn open_waypoint(chat_link: &str) -> bool {
    if !available() || !is_waypoint_link(chat_link) {
        return false;
    }
    let Ok(link) = CString::new(chat_link) else { return false };
    // Nexus dispatches synchronously; Pie UI copies/consumes this C string during the call.
    unsafe { event_raise("EV_PIEUI_OPEN_CHATLINK", &*(link.as_ptr() as *const c_char)) };
    true
}

fn is_waypoint_link(link: &str) -> bool {
    crate::waypoint_link::waypoint_id(link).is_some()
}

#[cfg(test)]
mod tests {
    use super::is_waypoint_link;

    #[test]
    fn only_map_chat_links_can_be_sent_to_pie_ui() {
        assert!(is_waypoint_link("[&BBAEAAA=]"));
        assert!(!is_waypoint_link("[&AgAAAAA=]"));
        assert!(!is_waypoint_link("[&BBAEAAA=] extra"));
        assert!(!is_waypoint_link("[&BA\0AAA=]"));
    }
}
