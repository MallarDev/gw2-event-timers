//! Version-gated native waypoint requests, dispatched through the game window.
//! No game detours, packet capture, diagnostic logging or manual warm-up.
use once_cell::sync::Lazy;
use parking_lot::Mutex;
use std::{ffi::c_void, mem::MaybeUninit, time::{Duration, Instant, SystemTime, UNIX_EPOCH}};
use windows::{core::{w, PCWSTR}, Win32::{
    Foundation::{HWND, LPARAM, WPARAM},
    System::{Diagnostics::Debug::ReadProcessMemory, LibraryLoader::GetModuleHandleW,
        Threading::{GetCurrentProcess, GetCurrentProcessId, GetCurrentThreadId}},
    UI::WindowsAndMessaging::{GetWindowThreadProcessId, PostMessageW, RegisterWindowMessageW, WM_NCDESTROY},
}};

const TIMESTAMP: u32 = 0x6ad7acf4;
const IMAGE_SIZE: usize = 0x02c49000;
const SENDER_RVA: usize = 0x01263c70;
const CONNECTION_RVA: usize = 0x0266c1d0;
const SIGNATURE: [u8; 16] = [0x40,0x53,0x48,0x83,0xec,0x20,0x8b,0xd9,0x85,0xc9,0x75,0x19,0x41,0xb8,0x12,0x39];
const TTL: Duration = Duration::from_secs(5);
const COOLDOWN: Duration = Duration::from_secs(2);

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum TravelError { Unsupported, Conflict, NotPlaying, NoWindow, NotConnected, Busy, Cooldown, Expired, Changed, Stopped, PostFailed }
impl TravelError {
    pub fn message(self) -> &'static str {
        match self {
            Self::Unsupported => "Direct travel is unavailable for this GW2 version.",
            Self::Conflict => "Direct travel is unavailable because another addon changed the game function. Remove Waypoint Probe and restart GW2.",
            Self::NotPlaying => "Wait until your character has finished loading before travelling.",
            Self::NoWindow => "The game window is not ready for direct travel. Interact with GW2 and try again.",
            Self::NotConnected => "Direct travel is unavailable while the game is disconnected or loading.",
            Self::Busy => "A waypoint request is already pending.",
            Self::Cooldown => "Wait a moment before travelling again.",
            Self::Expired => "The waypoint request expired. Click the event again to retry.",
            Self::Changed => "The game connection or window changed. Click the event again after loading.",
            Self::Stopped => "The waypoint request was cancelled because the addon stopped.",
            Self::PostFailed => "Could not deliver the waypoint request to the game window.",
        }
    }
}
#[derive(Clone, Copy, Debug)]
struct Image { base: usize }
#[derive(Clone, Copy, Debug)]
struct Request {
    id: u32, ticket: usize, generation: usize, window: usize, thread: u32,
    connection: usize, expires: Instant,
}
#[derive(Default)]
struct Runtime {
    image: Option<Image>, init_error: Option<TravelError>, alive: bool, registered: bool,
    message: u32, generation: usize, ticket: usize, window: usize, thread: u32,
    pending: Option<Request>, in_flight: bool, next_at: Option<Instant>, feedback: Option<TravelError>,
}
static RUNTIME: Lazy<Mutex<Runtime>> = Lazy::new(|| Mutex::new(Runtime::default()));

fn read<T: Copy>(address: usize) -> Option<T> {
    if address == 0 { return None; }
    let mut value = MaybeUninit::<T>::uninit();
    let mut copied = 0;
    unsafe {
        ReadProcessMemory(GetCurrentProcess(), address as *const c_void, value.as_mut_ptr().cast(),
            std::mem::size_of::<T>(), Some(&mut copied)).ok()?;
        (copied == std::mem::size_of::<T>()).then(|| value.assume_init())
    }
}
fn image_at(base: usize) -> Result<Image, TravelError> {
    let invalid = TravelError::Unsupported;
    if read::<u16>(base) != Some(0x5a4d) { return Err(invalid); }
    let pe = read::<u32>(base + 0x3c).filter(|n| (64..=4096).contains(n)).ok_or(invalid)? as usize;
    if read::<u32>(base + pe) != Some(0x4550)
        || read::<u16>(base + pe + 4) != Some(0x8664)
        || read::<u32>(base + pe + 8) != Some(TIMESTAMP)
        || read::<u16>(base + pe + 24) != Some(0x20b)
        || read::<u32>(base + pe + 24 + 56) != Some(IMAGE_SIZE as u32) { return Err(invalid); }
    let image = Image { base };
    check_function(image)?;
    Ok(image)
}
fn check_function(image: Image) -> Result<(), TravelError> {
    if read::<[u8; 16]>(image.base + SENDER_RVA) == Some(SIGNATURE) { Ok(()) }
    else { Err(TravelError::Conflict) }
}
fn connection(image: Image) -> Result<usize, TravelError> {
    let pointer = read::<usize>(image.base + CONNECTION_RVA).filter(|p| *p != 0).ok_or(TravelError::NotConnected)?;
    if read::<u32>(pointer + 0x108) != Some(3)
        || !read::<u8>(pointer).is_some_and(|flags| flags & 4 != 0) { return Err(TravelError::NotConnected); }
    Ok(pointer)
}
fn gameplay() -> bool { nexus::data_link::read_nexus_link().is_some_and(|link| link.is_gameplay) }
fn window_owner(window: usize) -> Option<u32> {
    let mut process = 0;
    let thread = unsafe { GetWindowThreadProcessId(HWND(window as *mut c_void), Some(&mut process)) };
    (thread != 0 && process == unsafe { GetCurrentProcessId() }).then_some(thread)
}
fn expire(runtime: &mut Runtime, now: Instant) {
    if runtime.pending.is_some_and(|request| now >= request.expires) {
        runtime.pending = None; runtime.feedback = Some(TravelError::Expired);
    }
}
pub fn load() {
    let base = unsafe { GetModuleHandleW(PCWSTR::null()) }.map(|h| h.0 as usize).unwrap_or(0);
    let image = image_at(base);
    let message = unsafe { RegisterWindowMessageW(w!("Mallard.EventTimers.DirectWaypoint.v1")) };
    {
        let mut runtime = RUNTIME.lock();
        *runtime = Runtime::default();
        runtime.image = image.ok(); runtime.init_error = image.err();
        runtime.generation = SystemTime::now().duration_since(UNIX_EPOCH).unwrap_or_default().as_nanos() as usize;
        runtime.message = message; runtime.alive = true;
        if message == 0 { runtime.init_error = Some(TravelError::NoWindow); }
    }
    // Explicitly deregister in unload so an executing callback is joined before teardown.
    nexus::wnd_proc::register_wnd_proc(window_message).leak();
    RUNTIME.lock().registered = true;
}
pub fn unload() {
    let registered = {
        let mut runtime = RUNTIME.lock();
        runtime.alive = false; runtime.pending = None;
        runtime.generation = runtime.generation.wrapping_add(1);
        let registered = runtime.registered; runtime.registered = false; registered
    };
    if registered { nexus::wnd_proc::deregister_wnd_proc(window_message); }
}
pub fn unavailable_reason() -> Option<&'static str> {
    RUNTIME.lock().init_error.map(TravelError::message)
}
pub fn poll_feedback() {
    let feedback = {
        let mut runtime = RUNTIME.lock(); expire(&mut runtime, Instant::now()); runtime.feedback.take()
    };
    if let Some(error) = feedback { nexus::alert::send_alert(error.message()); }
}
pub fn request(id: u32) -> Result<(), TravelError> {
    if id == 0 { return Err(TravelError::Changed); }
    let now = Instant::now();
    let (image, window, thread, message, generation) = {
        let mut runtime = RUNTIME.lock(); expire(&mut runtime, now);
        if !runtime.alive { return Err(TravelError::Stopped); }
        if let Some(error) = runtime.init_error { return Err(error); }
        if runtime.pending.is_some() || runtime.in_flight { return Err(TravelError::Busy); }
        if runtime.next_at.is_some_and(|until| now < until) { return Err(TravelError::Cooldown); }
        (runtime.image.ok_or(TravelError::Unsupported)?, runtime.window, runtime.thread, runtime.message, runtime.generation)
    };
    if window == 0 || window_owner(window) != Some(thread) { return Err(TravelError::NoWindow); }
    if !gameplay() { return Err(TravelError::NotPlaying); }
    check_function(image)?;
    let connection = connection(image)?;
    let request = {
        let mut runtime = RUNTIME.lock();
        if !runtime.alive || runtime.generation != generation { return Err(TravelError::Stopped); }
        if runtime.pending.is_some() || runtime.in_flight { return Err(TravelError::Busy); }
        if runtime.window != window || runtime.thread != thread { return Err(TravelError::Changed); }
        runtime.ticket = runtime.ticket.wrapping_add(1).max(1);
        let request = Request { id, ticket: runtime.ticket, generation, window, thread, connection, expires: now + TTL };
        runtime.pending = Some(request); request
    };
    if unsafe { PostMessageW(Some(HWND(window as *mut c_void)), message, WPARAM(request.ticket), LPARAM(generation as isize)) }.is_err() {
        let mut runtime = RUNTIME.lock();
        if runtime.pending.is_some_and(|p| p.ticket == request.ticket && p.generation == generation) { runtime.pending = None; }
        return Err(TravelError::PostFailed);
    }
    Ok(())
}
fn validate_delivery(request: Request, generation: usize, thread: u32, owner: Option<u32>, current_connection: usize, now: Instant) -> Result<(), TravelError> {
    if request.generation != generation { return Err(TravelError::Stopped); }
    if now >= request.expires { return Err(TravelError::Expired); }
    if request.thread != thread || owner != Some(thread) || request.connection != current_connection { return Err(TravelError::Changed); }
    Ok(())
}
fn consume_request(runtime: &mut Runtime, window: usize, ticket: usize, generation: usize) -> Option<Request> {
    let request = runtime.pending?;
    if request.ticket != ticket || request.generation != generation || request.window != window { return None; }
    runtime.pending = None; runtime.in_flight = true;
    Some(request)
}
extern "C-unwind" fn window_message(window: HWND, message: u32, wparam: WPARAM, lparam: LPARAM) -> u32 {
    let window = window.0 as usize;
    let current_thread = unsafe { GetCurrentThreadId() };
    let owner = window_owner(window);
    let (request, image, generation) = {
        let mut runtime = RUNTIME.lock();
        if !runtime.alive { return 1; }
        if owner == Some(current_thread) {
            if message == WM_NCDESTROY {
                if runtime.window == window { runtime.window = 0; runtime.thread = 0; runtime.pending = None; }
            } else { runtime.window = window; runtime.thread = current_thread; }
        }
        if message != runtime.message || message == 0 { return 1; }
        let Some(request) = consume_request(&mut runtime, window, wparam.0, lparam.0 as usize) else { return 0; };
        (request, runtime.image, runtime.generation)
    };
    let result = (|| {
        let image = image.ok_or(TravelError::Unsupported)?;
        if !gameplay() { return Err(TravelError::NotPlaying); }
        check_function(image)?;
        let connection = connection(image)?;
        validate_delivery(request, generation, current_thread, owner, connection, Instant::now())?;
        {
            let mut runtime = RUNTIME.lock();
            if !runtime.alive || runtime.generation != request.generation { return Err(TravelError::Stopped); }
            let now = Instant::now();
            if runtime.next_at.is_some_and(|until| now < until) { return Err(TravelError::Cooldown); }
            runtime.next_at = Some(now + COOLDOWN);
        }
        // This pointer is only constructed after exact PE and code-byte verification.
        let sender: unsafe extern "C" fn(u32) = unsafe { std::mem::transmute(image.base + SENDER_RVA) };
        unsafe { sender(request.id) };
        Ok(())
    })();
    let mut runtime = RUNTIME.lock();
    runtime.in_flight = false;
    if let Err(error) = result { if runtime.alive { runtime.feedback = Some(error); } }
    0 // consume only the addon's registered message
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn rejects_stale_expired_moved_or_disconnected_requests() {
        let now = Instant::now();
        let request = Request { id: 1072, ticket: 1, generation: 7, window: 1, thread: 480, connection: 10, expires: now + TTL };
        assert_eq!(validate_delivery(request, 7, 480, Some(480), 10, now), Ok(()));
        assert_eq!(validate_delivery(request, 8, 480, Some(480), 10, now), Err(TravelError::Stopped));
        assert_eq!(validate_delivery(request, 7, 1080, Some(480), 10, now), Err(TravelError::Changed));
        assert_eq!(validate_delivery(request, 7, 480, None, 10, now), Err(TravelError::Changed));
        assert_eq!(validate_delivery(request, 7, 480, Some(480), 11, now), Err(TravelError::Changed));
        assert_eq!(validate_delivery(request, 7, 480, Some(480), 10, now + TTL), Err(TravelError::Expired));
    }
    #[test]
    fn dispatch_tickets_are_single_use_and_bound_to_window_and_generation() {
        let request = Request { id: 1072, ticket: 3, generation: 7, window: 9, thread: 480, connection: 10, expires: Instant::now() + TTL };
        let mut runtime = Runtime { pending: Some(request), ..Runtime::default() };
        assert!(consume_request(&mut runtime, 9, 2, 7).is_none());
        assert!(consume_request(&mut runtime, 8, 3, 7).is_none());
        assert!(consume_request(&mut runtime, 9, 3, 8).is_none());
        assert!(runtime.pending.is_some());
        assert_eq!(consume_request(&mut runtime, 9, 3, 7).unwrap().id, 1072);
        assert!(runtime.in_flight && runtime.pending.is_none());
        assert!(consume_request(&mut runtime, 9, 3, 7).is_none());
    }
    #[test]
    fn only_accepts_exact_supported_pe_and_sender_bytes() {
        let mut image = vec![0u8; IMAGE_SIZE];
        let base = image.as_ptr() as usize;
        let pe = 128usize;
        image[0..2].copy_from_slice(&0x5a4du16.to_le_bytes());
        image[0x3c..0x40].copy_from_slice(&(pe as u32).to_le_bytes());
        image[pe..pe+4].copy_from_slice(&0x4550u32.to_le_bytes());
        image[pe+4..pe+6].copy_from_slice(&0x8664u16.to_le_bytes());
        image[pe+8..pe+12].copy_from_slice(&TIMESTAMP.to_le_bytes());
        image[pe+24..pe+26].copy_from_slice(&0x20bu16.to_le_bytes());
        image[pe+24+56..pe+24+60].copy_from_slice(&(IMAGE_SIZE as u32).to_le_bytes());
        image[SENDER_RVA..SENDER_RVA+16].copy_from_slice(&SIGNATURE);
        assert!(image_at(base).is_ok());
        image[pe+8] ^= 1; assert_eq!(image_at(base).unwrap_err(), TravelError::Unsupported);
        image[pe+8] ^= 1; image[SENDER_RVA] ^= 1;
        assert_eq!(image_at(base).unwrap_err(), TravelError::Conflict);
        assert!(image_at(1).is_err());
    }
}
