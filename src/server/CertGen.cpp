#include "CertGen.hpp"

// make_root_ca.cpp — create a self-signed root CA certificate with OpenSSL 3.x
// !! DISCLAIMER: written by Claude

#include <openssl/bn.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rand.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

[[noreturn]] void CertGen::fail ( const std::string& what ) {
	ERR_print_errors_fp(stderr);
	throw std::runtime_error(what);
}

// ---- 1. Key pair ------------------------------------------------------------
PKeyPtr CertGen::generateKey () {
	// RSA-4096 for a long-lived root. For an EC root use:
	//   PKeyPtr key(EVP_EC_gen("P-384"));
	PKeyPtr key(EVP_RSA_gen(4096));
	if ( !key )
		fail("key generation failed");
	return key;
}

// ---- 2. A random, positive serial number ------------------------------------
void CertGen::setRandomSerial ( X509* crt ) {
	const BNPtr bn(BN_new());
	if ( !bn || !BN_rand(bn.get(), 159, BN_RAND_TOP_ANY, BN_RAND_BOTTOM_ANY) )
		fail("serial generation failed");

	const AsnIntPtr serial(BN_to_ASN1_INTEGER(bn.get(), nullptr));
	if ( !serial || !X509_set_serialNumber(crt, serial.get()) )
		fail("setting serial failed");
}

// ---- 3. Subject == issuer, because a root is self-signed --------------------
void CertGen::setName ( X509* crt, const char* country, const char* org, const char* cn ) {
	X509_NAME* name = X509_get_subject_name(crt);
	auto add = [&] ( const char* field, const char* value ) {
		if ( !X509_NAME_add_entry_by_txt(name, field, MBSTRING_ASC, reinterpret_cast<const unsigned char*>(value), -1,
		                                 -1, 0) )
			fail(std::string("setting ") + field + " failed");
	};
	add("C", country);
	add("O", org);
	add("CN", cn);

	if ( !X509_set_issuer_name(crt, name) )
		fail("setting issuer failed");
}

// ---- 4. The extensions that actually make it a CA ---------------------------
void CertGen::addExt ( X509* crt, const int nid, const char* value ) {
	X509V3_CTX ctx;
	X509V3_set_ctx_nodb(&ctx);
	// Issuer and subject are the same cert: self-signed.
	X509V3_set_ctx(&ctx, crt, crt, nullptr, nullptr, 0);

	X509_EXTENSION* ext = X509V3_EXT_conf_nid(nullptr, &ctx, nid, value);
	if ( !ext )
		fail(std::string("creating extension failed: ") + value);

	const int ok = X509_add_ext(crt, ext, -1);
	X509_EXTENSION_free(ext);
	if ( !ok )
		fail("adding extension failed");
}

// ---- 5. Put it all together -------------------------------------------------
X509Ptr CertGen::makeRootCertificate ( EVP_PKEY* key, const long days ) {
	X509Ptr crt(X509_new());
	if ( !crt )
		fail("X509_new failed");

	// Version 3 is encoded as the integer 2.
	if ( !X509_set_version(crt.get(), X509_VERSION_3) )
		fail("setting version failed");

	setRandomSerial(crt.get());
	setName(crt.get(), "CZ", "Hikup", ("Hikup Default Cert n." + std::to_string(rand())).c_str() );

	if ( !X509_gmtime_adj(X509_getm_notBefore(crt.get()), 0) || !X509_gmtime_adj(
		     X509_getm_notAfter(crt.get()), 60L * 60 * 24 * days) )
		fail("setting validity failed");

	if ( !X509_set_pubkey(crt.get(), key) )
		fail("setting public key failed");

	addExt(crt.get(), NID_basic_constraints, "critical,CA:TRUE");
	addExt(crt.get(), NID_key_usage, "critical,keyCertSign,cRLSign,digitalSignature,keyEncipherment");
	addExt(crt.get(), NID_subject_key_identifier, "hash");
	addExt(crt.get(), NID_authority_key_identifier, "keyid:always");

	// Self-signature: signed with its own private key.
	if ( X509_sign(crt.get(), key, EVP_sha256()) == 0 )
		fail("signing failed");

	return crt;
}

// ---- 6. Write PEM files -----------------------------------------------------
void CertGen::writeKey ( const EVP_PKEY* key, const char* path, const char* passphrase ) {
	const BioPtr bio(BIO_new_file(path, "wb"));
	if ( !bio )
		fail("cannot open key file");

	int ok;
	if ( passphrase && *passphrase ) {
		ok = PEM_write_bio_PrivateKey(bio.get(), key, EVP_aes_256_cbc(),
		                              reinterpret_cast<const unsigned char*>(passphrase),
		                              static_cast<int>(std::string(passphrase).size()), nullptr, nullptr);
	}
	else { ok = PEM_write_bio_PrivateKey(bio.get(), key, nullptr, nullptr, 0, nullptr, nullptr); }
	if ( !ok )
		fail("writing private key failed");
}

void CertGen::writeCert ( const X509* crt, const char* path ) {
	const BioPtr bio(BIO_new_file(path, "wb"));
	if ( !bio || !PEM_write_bio_X509(bio.get(), crt) )
		fail("writing certificate failed");
}

bool CertGen::generateRoot () {
	try {
		const PKeyPtr key = generateKey();
		const X509Ptr crt = makeRootCertificate(key.get(), 365 * 10); // 10 years

		writeKey(key.get(), "auth/ca_key.pem", nullptr);
		writeCert(crt.get(), "auth/ca_cert.pem");

		std::cout << "Wrote ca_key.pem and ca_cert.pem\n";
		return false;
	}
	catch ( const std::exception& e ) {
		std::cerr << "Error: " << e.what() << '\n';
		return true;
	}
}

// ---- 7. Extension helper with an explicit issuer certificate ----------------
// Your original add_ext() always passes `crt` as both issuer and subject,
// which is correct for a self-signed root but WRONG for a leaf cert: the
// AuthorityKeyIdentifier extension needs to reference the CA's key, not
// the leaf's own key. This variant takes the issuer explicitly.
void CertGen::addExtEx ( X509* crt, X509* issuer_crt, const int nid, const char* value ) {
	X509V3_CTX ctx;
	X509V3_set_ctx_nodb(&ctx);
	X509V3_set_ctx(&ctx, issuer_crt, crt, nullptr, nullptr, 0);

	X509_EXTENSION* ext = X509V3_EXT_conf_nid(nullptr, &ctx, nid, value);
	if ( !ext )
		fail(std::string("creating extension failed: ") + value);

	const int ok = X509_add_ext(crt, ext, -1);
	X509_EXTENSION_free(ext);
	if ( !ok )
		fail("adding extension failed");
}

// ---- 8. Server (leaf) certificate, signed by the CA --------------------------
X509Ptr CertGen::makeServerCertificate (
	EVP_PKEY* server_key,
	X509* ca_crt,
	EVP_PKEY* ca_key,
	const char* country,
	const char* org,
	const char* common_name,
	const std::vector<std::string>& san_entries,
	const long days ) {

	X509Ptr crt(X509_new());
	if ( !crt )
		fail("X509_new failed");

	if ( !X509_set_version(crt.get(), X509_VERSION_3) )
		fail("setting version failed");

	setRandomSerial(crt.get());

	// Subject = the server, NOT the CA.
	X509_NAME* name = X509_get_subject_name(crt.get());
	auto add = [&] ( const char* field, const char* value ) {
		if ( !X509_NAME_add_entry_by_txt(name, field, MBSTRING_ASC,
		                                 reinterpret_cast<const unsigned char*>(value), -1, -1, 0) )
			fail(std::string("setting ") + field + " failed");
	};
	add("C", country);
	add("O", org);
	add("CN", common_name);

	// Issuer = the CA's subject.
	if ( !X509_set_issuer_name(crt.get(), X509_get_subject_name(ca_crt)) )
		fail("setting issuer failed");

	if ( !X509_gmtime_adj(X509_getm_notBefore(crt.get()), 0) || !X509_gmtime_adj(
		     X509_getm_notAfter(crt.get()), 60L * 60 * 24 * days) )
		fail("setting validity failed");

	if ( !X509_set_pubkey(crt.get(), server_key) )
		fail("setting public key failed");

	// Leaf-appropriate extensions. CA:FALSE and no cert-signing usage —
	// this cert must not itself be able to sign other certificates.
	addExtEx(crt.get(), ca_crt, NID_basic_constraints, "critical,CA:FALSE");
	addExtEx(crt.get(), ca_crt, NID_key_usage, "critical,digitalSignature,keyEncipherment");
	addExtEx(crt.get(), ca_crt, NID_ext_key_usage, "serverAuth");
	addExtEx(crt.get(), ca_crt, NID_subject_key_identifier, "hash");
	// "keyid,issuer" ties this cert to the CA's actual key identifier —
	// this is what lets clients build the chain back to your root.
	addExtEx(crt.get(), ca_crt, NID_authority_key_identifier, "keyid,issuer");

	// Modern browsers/clients require SAN even when CN is also set;
	// CN-only certs are rejected by current Chrome/Firefox/curl policy.
	if ( san_entries.empty() )
		fail("subjectAltName is required for a usable TLS server certificate");
	std::string san_value;
	for ( size_t i = 0; i < san_entries.size(); ++i ) {
		if ( i ) san_value += ',';
		san_value += san_entries[i];
	}
	addExtEx(crt.get(), ca_crt, NID_subject_alt_name, san_value.c_str());

	// Sign with the CA's PRIVATE key, not the server's own key.
	if ( X509_sign(crt.get(), ca_key, EVP_sha256()) == 0 )
		fail("signing failed");

	return crt;
}

// ---- 9. Fullchain output ------------------------------------------------------
// Most TLS servers (nginx, HAProxy, etc.) want the leaf cert followed by
// the CA cert in one file, so clients that don't already trust your root
// can still build the chain.
void CertGen::writeFullchain ( const X509* server_crt, const X509* ca_crt, const char* path ) {
	const BioPtr bio(BIO_new_file(path, "wb"));
	if ( !bio )
		fail("cannot open fullchain file");
	if ( !PEM_write_bio_X509(bio.get(), const_cast<X509*>(server_crt)) )
		fail("writing server certificate to fullchain failed");
	if ( !PEM_write_bio_X509(bio.get(), const_cast<X509*>(ca_crt)) )
		fail("writing CA certificate to fullchain failed");
}

// ---- 10. Driver: load an existing CA and issue a server cert from it ---------
// CertGen::generateServerCert(
//     "ca_key.pem", "ca_cert.pem",
//     "hikup.example.com",
//     { "DNS:hikup.example.com", "DNS:www.hikup.example.com", "IP:127.0.0.1" }
// );
bool CertGen::generateServerCert (
	const char* ca_key_path,
	const char* ca_cert_path,
	const char* common_name,
	const std::vector<std::string>& san_entries ) {

	try {
		// Load the CA's private key.
		const BioPtr key_bio(BIO_new_file(ca_key_path, "rb"));
		if ( !key_bio )
			fail("cannot open CA key file");
		PKeyPtr ca_key(PEM_read_bio_PrivateKey(key_bio.get(), nullptr, nullptr, nullptr));
		if ( !ca_key )
			fail("reading CA private key failed");

		// Load the CA's certificate.
		const BioPtr crt_bio(BIO_new_file(ca_cert_path, "rb"));
		if ( !crt_bio )
			fail("cannot open CA cert file");
		const X509Ptr ca_crt(PEM_read_bio_X509(crt_bio.get(), nullptr, nullptr, nullptr));
		if ( !ca_crt )
			fail("reading CA certificate failed");

		// Server key. EC P-256 is the common, fast choice for a leaf cert;
		// swap for EVP_RSA_gen(2048) if you need RSA compatibility.
		const PKeyPtr server_key(EVP_EC_gen("P-256"));
		if ( !server_key )
			fail("server key generation failed");

		const X509Ptr server_crt = makeServerCertificate(
			server_key.get(), ca_crt.get(), ca_key.get(),
			"CZ", "Hikup", common_name, san_entries,
			365 * 2 ); // 2 years — keep leaf lifetimes short

		writeKey(server_key.get(), "auth/server_key.pem", nullptr);
		writeCert(server_crt.get(), "auth/server_cert.pem");
		writeFullchain(server_crt.get(), ca_crt.get(), "auth/server_fullchain.pem");

		std::cout << "Wrote server_key.pem, server_cert.pem, server_fullchain.pem\n";
		return false;
	}
	catch ( const std::exception& e ) {
		std::cerr << "Error: " << e.what() << '\n';
		return true;
	}
}

// ---- Example usage ------------------------------------------------------------
// CertGen gen;
// gen.generate();  // creates ca_key.pem / ca_cert.pem (your existing root)
// gen.generate_server_cert(
//     "ca_key.pem", "ca_cert.pem",
//     "hikup.example.com",
//     { "DNS:hikup.example.com", "DNS:www.hikup.example.com", "IP:127.0.0.1" }
// );
//
// Configure your TLS server (nginx, etc.) with:
//   ssl_certificate     server_fullchain.pem;
//   ssl_certificate_key server_key.pem;
//
// Clients will only trust this without warnings if ca_cert.pem is
// installed in their trust store (or added as a custom CA), since it's
// a private root, not one from a public CA.
