//! Optional TLS: `mbedtls-rs` with Mozilla CA chain verification.
//!
//! Roots come from the `mozilla-root-ca` crate (curl/`cacert.pem`), embedded at
//! build time into `OUT_DIR/mozilla_ca.bin` — no hand-maintained PEM in git.
//!
//! Remaining gaps (see `docs/RUST_ESP_HAL.md`):
//! - Cert `notBefore`/`notAfter` are skipped unless `hook-wall-clock` + RTC/NTP
//!   are installed (mbedtls-rs default).
//! - Full Mozilla bundle is ~190 KiB flash; parsing needs extra heap vs host-pin.
//! - Global `Tls` singleton: MQTT + OTA share one instance (mbedtls-rs limit).

use core::cell::RefCell;
use core::convert::Infallible;
use core::ffi::CStr;
use core::sync::atomic::{AtomicBool, Ordering};

use critical_section::Mutex;
use embedded_io_async::{Read, Write};
use mbedtls_rs::{
    Certificate, ClientSessionConfig, Session, SessionConfig, Tls, TlsReference, X509,
};
use rand_core10::{TryCryptoRng, TryRng};
use static_cell::StaticCell;
use tinyrlibc as _;

/// Opened TLS stream type for the mbedtls CA backend.
pub type TlsStream<'a, S> = Session<'a, S>;

pub fn backend_name() -> &'static str {
    "mbedtls-rs CA (mozilla-root-ca)"
}

const CA_PEM: &CStr = match CStr::from_bytes_with_nul(include_bytes!(concat!(
    env!("OUT_DIR"),
    "/mozilla_ca.bin"
))) {
    Ok(bundle) => bundle,
    Err(_) => panic!("mozilla_ca.bin must be NUL-terminated without interior NULs"),
};

struct HwRng(esp_hal::rng::Rng);

impl TryRng for HwRng {
    type Error = Infallible;

    fn try_next_u32(&mut self) -> Result<u32, Self::Error> {
        Ok(self.0.random())
    }

    fn try_next_u64(&mut self) -> Result<u64, Self::Error> {
        Ok(((self.0.random() as u64) << 32) | self.0.random() as u64)
    }

    fn try_fill_bytes(&mut self, dest: &mut [u8]) -> Result<(), Self::Error> {
        for chunk in dest.chunks_mut(4) {
            let r = self.0.random().to_le_bytes();
            chunk.copy_from_slice(&r[..chunk.len()]);
        }
        Ok(())
    }
}

impl TryCryptoRng for HwRng {}

static TIMER_HOOKED: AtomicBool = AtomicBool::new(false);
static TLS_SLOT: Mutex<RefCell<Option<&'static Tls<'static>>>> = Mutex::new(RefCell::new(None));

fn ensure_timer_hook() {
    if TIMER_HOOKED.swap(true, Ordering::AcqRel) {
        return;
    }
    use mbedtls_rs::sys::hook::backend::embassy::timer::EmbassyTimer;
    use mbedtls_rs::sys::hook::timer::hook_timer;

    static TIMER: StaticCell<EmbassyTimer> = StaticCell::new();
    let timer = TIMER.init(EmbassyTimer);
    // SAFETY: timer is `'static`; hooked once for process lifetime.
    unsafe {
        hook_timer(Some(timer));
    }
}

fn tls_reference() -> Result<TlsReference<'static>, ()> {
    ensure_timer_hook();
    critical_section::with(|cs| {
        let mut slot = TLS_SLOT.borrow_ref_mut(cs);
        if slot.is_none() {
            static RNG: StaticCell<HwRng> = StaticCell::new();
            static TLS: StaticCell<Tls<'static>> = StaticCell::new();
            let rng = RNG.init(HwRng(esp_hal::rng::Rng::new()));
            let tls = Tls::new(rng).map_err(|_| ())?;
            *slot = Some(TLS.init(tls));
        }
        Ok(slot.as_ref().unwrap().reference())
    })
}

/// Fresh parse per connection — `Certificate` is `!Send` (cannot cache in a Mutex),
/// and concurrent sessions must not share one MRc CA chain across awaits.
fn ca_certificate() -> Result<Certificate<'static>, ()> {
    Certificate::new(X509::PEM(CA_PEM)).map_err(|_| ())
}

/// TLS handshake on an already-connected TCP socket (CA + hostname verify).
///
/// `tls_rx` / `tls_tx` are unused (mbedtls allocates record buffers internally).
/// `rng` is unused; mbedtls uses a process-global HW RNG installed once.
pub async fn connect<'a, S, R>(
    socket: S,
    host: &str,
    _rng: &'a mut R,
    _tls_rx: &'a mut [u8],
    _tls_tx: &'a mut [u8],
) -> Result<TlsStream<'a, S>, ()>
where
    S: Read + Write + 'a,
{
    let tls = tls_reference()?;
    // Coerce `'static` CA into `'a` so `Session<'a, S>` matches the host-pin shape.
    let ca: Certificate<'a> = ca_certificate()?;

    let conf = ClientSessionConfig {
        ca_chain: Some(ca),
        server_name: None,
        ..ClientSessionConfig::new()
    };

    let mut session = Session::new(tls, socket, &SessionConfig::Client(conf)).map_err(|_| ())?;

    // Hostname is copied into the SSL context; stack CStr only needed here.
    let mut host_buf = [0u8; 65];
    if host.len() >= host_buf.len() {
        return Err(());
    }
    host_buf[..host.len()].copy_from_slice(host.as_bytes());
    host_buf[host.len()] = 0;
    let host_cstr = CStr::from_bytes_with_nul(&host_buf[..=host.len()]).map_err(|_| ())?;
    session.set_server_name(host_cstr).map_err(|_| ())?;
    session.connect().await.map_err(|_| ())?;
    Ok(session)
}
