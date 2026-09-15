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
PKeyPtr CertGen::generate_key () {
	// RSA-4096 for a long-lived root. For an EC root use:
	//   PKeyPtr key(EVP_EC_gen("P-384"));
	PKeyPtr key(EVP_RSA_gen(4096));
	if ( !key )
		fail("key generation failed");
	return key;
}

// ---- 2. A random, positive serial number ------------------------------------
void CertGen::set_random_serial ( X509* crt ) {
	const BNPtr bn(BN_new());
	if ( !bn || !BN_rand(bn.get(), 159, BN_RAND_TOP_ANY, BN_RAND_BOTTOM_ANY) )
		fail("serial generation failed");

	AsnIntPtr serial(BN_to_ASN1_INTEGER(bn.get(), nullptr));
	if ( !serial || !X509_set_serialNumber(crt, serial.get()) )
		fail("setting serial failed");
}

// ---- 3. Subject == issuer, because a root is self-signed --------------------
void CertGen::set_name ( X509* crt, const char* country, const char* org, const char* cn ) {
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
void CertGen::add_ext ( X509* crt, const int nid, const char* value ) {
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
X509Ptr CertGen::make_root_certificate ( EVP_PKEY* key, const long days ) {
	X509Ptr crt(X509_new());
	if ( !crt )
		fail("X509_new failed");

	// Version 3 is encoded as the integer 2.
	if ( !X509_set_version(crt.get(), X509_VERSION_3) )
		fail("setting version failed");

	set_random_serial(crt.get());
	set_name(crt.get(), "CZ", "Hikup", "Hikup Default Cert");

	if ( !X509_gmtime_adj(X509_getm_notBefore(crt.get()), 0) || !X509_gmtime_adj(
		     X509_getm_notAfter(crt.get()), 60L * 60 * 24 * days) )
		fail("setting validity failed");

	if ( !X509_set_pubkey(crt.get(), key) )
		fail("setting public key failed");

	add_ext(crt.get(), NID_basic_constraints, "critical,CA:TRUE");
	add_ext(crt.get(), NID_key_usage, "critical,keyCertSign,cRLSign,digitalSignature,keyEncipherment");
	add_ext(crt.get(), NID_subject_key_identifier, "hash");
	add_ext(crt.get(), NID_authority_key_identifier, "keyid:always");

	// Self-signature: signed with its own private key.
	if ( X509_sign(crt.get(), key, EVP_sha256()) == 0 )
		fail("signing failed");

	return crt;
}

// ---- 6. Write PEM files -----------------------------------------------------
void CertGen::write_key ( const EVP_PKEY* key, const char* path, const char* passphrase ) {
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

void CertGen::write_cert ( const X509* crt, const char* path ) {
	const BioPtr bio(BIO_new_file(path, "wb"));
	if ( !bio || !PEM_write_bio_X509(bio.get(), crt) )
		fail("writing certificate failed");
}

bool CertGen::generate () {
	try {
		const PKeyPtr key = generate_key();
		const X509Ptr crt = make_root_certificate(key.get(), 365 * 10); // 10 years

		write_key(key.get(), "ca_key.pem", nullptr);
		write_cert(crt.get(), "ca_cert.pem");

		std::cout << "Wrote ca_key.pem and ca_cert.pem\n";
		return false;
	}
	catch ( const std::exception& e ) {
		std::cerr << "Error: " << e.what() << '\n';
		return true;
	}
}
