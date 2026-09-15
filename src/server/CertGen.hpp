#pragma once
#include <memory>
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
	static bool generate();

private:
	[[noreturn]] static void fail(const std::string& what);
	static PKeyPtr generate_key();
	static void set_random_serial(X509* crt);
	static void set_name(X509* crt, const char* country, const char* org, const char* cn);
	static void add_ext(X509* crt, int nid, const char* value);
	static X509Ptr make_root_certificate(EVP_PKEY* key, long days);
	static void write_key( const EVP_PKEY* key, const char* path, const char* passphrase);
	static void write_cert( const X509* crt, const char* path);
};
