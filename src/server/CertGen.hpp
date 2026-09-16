#pragma once
#include <memory>
#include <vector>
#include <openssl/bn.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/x509.h>


// ---- RAII wrappers so nothing leaks on an error path ------------------------
template <class T, void (*F)(T*)>
struct Deleter {
	void operator()(T* p) const { if (p) F(p); }
};
using PKeyPtr   = std::unique_ptr<EVP_PKEY, Deleter<EVP_PKEY, EVP_PKEY_free>>;
using X509Ptr   = std::unique_ptr<X509, Deleter<X509, X509_free>>;
using BNPtr     = std::unique_ptr<BIGNUM, Deleter<BIGNUM, BN_free>>;
using AsnIntPtr = std::unique_ptr<ASN1_INTEGER, Deleter<ASN1_INTEGER, ASN1_INTEGER_free>>;
using BioPtr    = std::unique_ptr<BIO, Deleter<BIO, BIO_free_all>>;

class CertGen {
public:
	static bool generateRoot();
	static bool generateServerCert ( const char* ca_key_path,
									 const char* ca_cert_path,
									 const char* common_name,
									 const std::vector<std::string>& san_entries );

private:
	[[noreturn]] static void fail(const std::string& what);
	static PKeyPtr generateKey();
	static void setRandomSerial(X509* crt);
	static void setName(X509* crt, const char* country, const char* org, const char* cn);
	static void addExt(X509* crt, int nid, const char* value);
	static X509Ptr makeRootCertificate(EVP_PKEY* key, long days);
	static void writeKey( const EVP_PKEY* key, const char* path, const char* passphrase);
	static void writeCert( const X509* crt, const char* path);
	static void addExtEx ( X509* crt, X509* issuer_crt, int nid, const char* value );

	static X509Ptr makeServerCertificate ( EVP_PKEY* server_key,
	                                       X509* ca_crt,
	                                       EVP_PKEY* ca_key,
	                                       const char* country,
	                                       const char* org,
	                                       const char* common_name,
	                                       const std::vector<std::string>& san_entries,
	                                       long days );

	static void writeFullchain ( const X509* server_crt, const X509* ca_crt, const char* path );
};
