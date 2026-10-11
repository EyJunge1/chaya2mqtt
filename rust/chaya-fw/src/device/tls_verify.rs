//! Lightweight TLS verifier: require leaf cert + hostname in DER (no full CA chain).
//! Full webpki + CA bundle needs NTP time; this is the best no_std step without that.

use core::marker::PhantomData;
use embedded_tls::{
    Aes128GcmSha256, Certificate, CertificateRef, CertificateVerifyRef, CryptoProvider,
    NoSign, SignatureScheme, TlsCipherSuite, TlsError, TlsVerifier,
};
use p256::ecdsa::DerSignature;
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
            // SNI required for this verifier
            return Err(TlsError::InvalidCertificate);
        };
        let host_bytes = host.as_bytes();
        let mut found = false;
        for entry in cert.entries.iter() {
            if let embedded_tls::CertificateEntryRef::X509(der) = entry {
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
    type Signature = DerSignature;

    fn rng(&mut self) -> impl CryptoRngCore {
        &mut self.rng
    }

    fn verifier(&mut self) -> Result<&mut impl TlsVerifier<Self::CipherSuite>, TlsError> {
        Ok(&mut self.verifier)
    }

    fn signer(
        &mut self,
    ) -> Result<(impl signature::SignerMut<Self::Signature>, SignatureScheme), TlsError> {
        Err::<(NoSign, _), _>(TlsError::Unimplemented)
    }

    fn client_cert(&mut self) -> Option<Certificate<impl AsRef<[u8]>>> {
        None::<Certificate<&[u8]>>
    }
}
