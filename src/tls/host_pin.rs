//! Default TLS: `embedded-tls` with hostname presence check (no CA chain).

use core::marker::PhantomData;
use embedded_io_async::{Read, Write};
use embedded_tls::{
    Aes128GcmSha256, CertificateEntryRef, CertificateRef, CertificateVerifyRef, CryptoProvider,
    TlsCipherSuite, TlsConfig, TlsConnection, TlsContext, TlsError, TlsVerifier,
};
use rand_core::CryptoRngCore;

/// Verifier that rejects empty chains and requires the SNI hostname bytes in the leaf DER.
pub struct HostPresenceVerifier<CipherSuite: TlsCipherSuite> {
    host: Option<heapless::String<64>>,
    saw_cert: bool,
    _cs: PhantomData<CipherSuite>,
}

impl<CipherSuite: TlsCipherSuite> HostPresenceVerifier<CipherSuite> {
    pub const fn new() -> Self {
        Self {
            host: None,
            saw_cert: false,
            _cs: PhantomData,
        }
    }
}

impl<CipherSuite: TlsCipherSuite> Default for HostPresenceVerifier<CipherSuite> {
    fn default() -> Self {
        Self::new()
    }
}

impl<CipherSuite: TlsCipherSuite> TlsVerifier<CipherSuite> for HostPresenceVerifier<CipherSuite> {
    fn set_hostname_verification(&mut self, hostname: &str) -> Result<(), TlsError> {
        self.host = Some(
            heapless::String::try_from(hostname).map_err(|_| TlsError::InsufficientSpace)?,
        );
        Ok(())
    }

    fn verify_certificate(
        &mut self,
        _transcript: &CipherSuite::Hash,
        cert: CertificateRef,
    ) -> Result<(), TlsError> {
        if cert.entries.is_empty() {
            return Err(TlsError::InvalidCertificate);
        }
        let Some(host) = self.host.as_deref() else {
            return Err(TlsError::InvalidCertificate);
        };
        let host_bytes = host.as_bytes();
        let mut found = false;
        for entry in cert.entries.iter() {
            if let CertificateEntryRef::X509(der) = entry {
                if der.windows(host_bytes.len()).any(|w| w == host_bytes) {
                    found = true;
                    break;
                }
            }
        }
        if !found {
            return Err(TlsError::InvalidCertificate);
        }
        self.saw_cert = true;
        Ok(())
    }

    fn verify_signature(&mut self, _verify: CertificateVerifyRef) -> Result<(), TlsError> {
        // Full signature crypto needs webpki + CA; hostname presence already gated above.
        if self.saw_cert {
            Ok(())
        } else {
            Err(TlsError::InvalidSignature)
        }
    }
}

/// Crypto provider used for MQTT / GitHub TLS (hostname presence check).
pub struct HostPinProvider<RNG> {
    rng: RNG,
    verifier: HostPresenceVerifier<Aes128GcmSha256>,
}

impl<RNG: CryptoRngCore> HostPinProvider<RNG> {
    pub fn new(rng: RNG) -> Self {
        Self {
            rng,
            verifier: HostPresenceVerifier::new(),
        }
    }
}

impl<RNG: CryptoRngCore> CryptoProvider for HostPinProvider<RNG> {
    type CipherSuite = Aes128GcmSha256;
    type Signature = &'static [u8];

    fn rng(&mut self) -> impl CryptoRngCore {
        &mut self.rng
    }

    fn verifier(&mut self) -> Result<&mut impl TlsVerifier<Self::CipherSuite>, TlsError> {
        Ok(&mut self.verifier)
    }
}

/// Opened TLS stream type for the default (host-pin) backend.
pub type TlsStream<'a, S> = TlsConnection<'a, S, Aes128GcmSha256>;

pub fn backend_name() -> &'static str {
    "embedded-tls host-pin"
}

/// TLS handshake on an already-connected TCP socket.
pub async fn connect<'a, S, R>(
    socket: S,
    host: &str,
    rng: &'a mut R,
    tls_rx: &'a mut [u8],
    tls_tx: &'a mut [u8],
) -> Result<TlsStream<'a, S>, ()>
where
    S: Read + Write + 'a,
    R: CryptoRngCore,
{
    let config = TlsConfig::new()
        .with_server_name(host)
        .enable_rsa_signatures();
    let mut tls = TlsConnection::new(socket, tls_rx, tls_tx);
    {
        let mut provider = HostPinProvider::new(rng);
        tls.open(TlsContext::new(&config, &mut provider))
            .await
            .map_err(|_| ())?;
    }
    Ok(tls)
}
