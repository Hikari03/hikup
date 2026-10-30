#include "E2E.hpp"

#include <chrono>
#include <cstdio>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <vector>

#include <openssl/evp.h>
#include <openssl/rand.h>

#include "Style.hpp"
#include "../shared/utils.hpp"

namespace {

	constexpr size_t KEY_SIZE   = 32;   // AES-256
	constexpr size_t NONCE_SIZE = 12;   // GCM standard
	constexpr size_t TAG_SIZE    = 16;   // GCM standard
	constexpr size_t CHUNK       = 4 * 1024 * 1024;

	[[noreturn]] void die ( const std::string& what ) {
		throw std::runtime_error("e2e: " + what);
	}

	struct CipherCtxGuard {
		EVP_CIPHER_CTX* ctx;
		explicit CipherCtxGuard ( EVP_CIPHER_CTX* c ) : ctx(c) {}
		~CipherCtxGuard () { if ( ctx ) EVP_CIPHER_CTX_free(ctx); }
		CipherCtxGuard ( const CipherCtxGuard& ) = delete;
		CipherCtxGuard& operator= ( const CipherCtxGuard& ) = delete;
	};

	// removes the temporary file unless told to keep it (RAII, also on throw)
	struct TempFileGuard {
		std::filesystem::path path;
		bool keep = false;
		~TempFileGuard () {
			if ( !keep ) {
				std::error_code ec;
				std::filesystem::remove(path, ec);
			}
		}
	};

	int hexNibble ( const char c ) {
		if ( c >= '0' && c <= '9' ) return c - '0';
		if ( c >= 'a' && c <= 'f' ) return c - 'a' + 10;
		if ( c >= 'A' && c <= 'F' ) return c - 'A' + 10;
		return -1;
	}

	bool parseKey ( const std::string& keyHex, unsigned char ( &key )[KEY_SIZE] ) {
		if ( keyHex.size() != KEY_SIZE * 2 )
			return false;

		for ( size_t i = 0; i < KEY_SIZE; ++i ) {
			const int hi = hexNibble(keyHex[2 * i]);
			const int lo = hexNibble(keyHex[2 * i + 1]);
			if ( hi < 0 || lo < 0 )
				return false;
			key[i] = static_cast<unsigned char>((hi << 4) | lo);
		}

		return true;
	}

	// progress bar in the same style as hashing / upload / download
	struct Progress {
		bool show;
		size_t termW;
		std::chrono::steady_clock::time_point start, lastDraw;

		explicit Progress ( const bool quiet )
			: show(!quiet && ui::g_tty)
			  , termW(ui::terminalWidth())
			  , start(std::chrono::steady_clock::now())
			  , lastDraw(start - std::chrono::seconds(1)) {}

		void draw ( const char* verb, const unsigned long long done, const unsigned long long total ) {
			if ( !show )
				return;

			using clock = std::chrono::steady_clock;
			if ( clock::now() - lastDraw < std::chrono::milliseconds(80) )
				return;

			const std::chrono::duration<double> el = clock::now() - start;
			const double speed = el.count() > 0 ? double(done) / el.count() : 0.0;

			ui::drawBar(verb,
			            total ? double(done) / double(total) : 1.0,
			            humanReadableSize(done) + "/" + humanReadableSize(total),
			            humanReadableSpeed(speed),
			            speed > 0 ? "eta " + ui::formatDuration(double(total - done) / speed) : "eta --",
			            termW);
			lastDraw = clock::now();
		}
	};

	std::filesystem::path makeTempPath ( const unsigned char seed[], const size_t seedLen ) {
		return std::filesystem::temp_directory_path()
		       / ( "hikup-e2e-" + bytesToHex(seed, seedLen) + ".tmp" );
	}
}

namespace E2E {

	std::pair<std::filesystem::path, std::string> encryptFile ( const std::filesystem::path& source, const bool quiet ) {
		if ( !std::filesystem::exists(source) )
			die("input file does not exist: " + source.string());
		if ( !std::filesystem::is_regular_file(source) )
			die("input is not a regular file: " + source.string());

		unsigned char key[KEY_SIZE];
		unsigned char nonce[NONCE_SIZE];
		if ( RAND_bytes(key, sizeof key) != 1 || RAND_bytes(nonce, sizeof nonce) != 1 )
			die("could not generate random key/nonce");

		const auto total = std::filesystem::file_size(source);
		const auto temp   = makeTempPath(key, 4);   // 8 hex chars: unique per key, no pid races

		std::ifstream in(source, std::ios::binary);
		if ( !in ) die("could not open " + source.string());

		std::ofstream out(temp, std::ios::binary | std::ios::trunc);
		if ( !out ) die("could not create temporary file " + temp.string());

		TempFileGuard guard{temp};

		const auto raw = EVP_CIPHER_CTX_new();
		if ( !raw ) die("could not create cipher context");
		CipherCtxGuard ctx(raw);

		if ( EVP_EncryptInit_ex(ctx.ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1 )
			die("cipher init failed");
		if ( EVP_CIPHER_CTX_ctrl(ctx.ctx, EVP_CTRL_GCM_SET_IVLEN, NONCE_SIZE, nullptr) != 1 )
			die("could not set nonce length");
		if ( EVP_EncryptInit_ex(ctx.ctx, nullptr, nullptr, key, nonce) != 1 )
			die("key init failed");

		out.write(reinterpret_cast<const char*>(nonce), NONCE_SIZE);
		if ( !out ) die("write failed");

		Progress progress(quiet);
		std::vector<char> inBuf(CHUNK);
		std::vector<char> outBuf(CHUNK + 16);
		unsigned long long processed = 0;

		while ( in ) {
			in.read(inBuf.data(), CHUNK);
			const auto got = static_cast<size_t>(in.gcount());
			if ( got == 0 ) break;

			int outLen = 0;
			if ( EVP_EncryptUpdate(ctx.ctx, reinterpret_cast<unsigned char*>(outBuf.data()), &outLen,
			                       reinterpret_cast<const unsigned char*>(inBuf.data()), got) != 1 )
				die("encryption failed");
			out.write(outBuf.data(), outLen);
			if ( !out ) die("write failed");

			processed += got;
			progress.draw("encrypting", processed, total);
		}

		// eof is expected; any other stream error is a real read failure
		if ( in.bad() || ( in.fail() && !in.eof() ) )
			die("error reading " + source.string());

		int outLen = 0;
		if ( EVP_EncryptFinal_ex(ctx.ctx, reinterpret_cast<unsigned char*>(outBuf.data()), &outLen) != 1 )
			die("encryption finalisation failed");
		out.write(outBuf.data(), outLen);

		unsigned char tag[TAG_SIZE];
		if ( EVP_CIPHER_CTX_ctrl(ctx.ctx, EVP_CTRL_GCM_GET_TAG, TAG_SIZE, tag) != 1 )
			die("could not get authentication tag");
		out.write(reinterpret_cast<const char*>(tag), TAG_SIZE);

		out.flush();
		if ( !out ) die("write failed");
		out.close();

		if ( progress.show ) ui::clearLine();

		guard.keep = true;
		return { temp, bytesToHex(key, KEY_SIZE) };
	}

	void decryptFile ( const std::filesystem::path& source, const std::string& keyHex,
	                   const std::filesystem::path& destination, const bool quiet ) {

		unsigned char key[KEY_SIZE];
		if ( !parseKey(keyHex, key) )
			die("invalid key, expected " + std::to_string(KEY_SIZE * 2) + " hex characters");

		const auto total = std::filesystem::file_size(source);
		if ( total < NONCE_SIZE + TAG_SIZE )
			die("file is too small to be encrypted data");

		std::ifstream in(source, std::ios::binary);
		if ( !in ) die("could not open " + source.string());

		unsigned char nonce[NONCE_SIZE];
		in.read(reinterpret_cast<char*>(nonce), NONCE_SIZE);
		if ( static_cast<size_t>(in.gcount()) != NONCE_SIZE )
			die("could not read nonce");

		std::ofstream out(destination, std::ios::binary | std::ios::trunc);
		if ( !out ) die("could not create " + destination.string());

		const auto raw = EVP_CIPHER_CTX_new();
		if ( !raw ) die("could not create cipher context");
		CipherCtxGuard ctx(raw);

		if ( EVP_DecryptInit_ex(ctx.ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1 )
			die("cipher init failed");
		if ( EVP_CIPHER_CTX_ctrl(ctx.ctx, EVP_CTRL_GCM_SET_IVLEN, NONCE_SIZE, nullptr) != 1 )
			die("could not set nonce length");
		if ( EVP_DecryptInit_ex(ctx.ctx, nullptr, nullptr, key, nonce) != 1 )
			die("key init failed");

		Progress progress(quiet);
		std::vector<char> inBuf(CHUNK);
		std::vector<char> outBuf(CHUNK + 16);

		const auto cipherLen = total - NONCE_SIZE - TAG_SIZE;
		unsigned long long remaining = cipherLen;
		unsigned long long processed  = 0;

		while ( remaining > 0 ) {
			const auto toRead = static_cast<std::streamsize>(std::min<size_t>(CHUNK, remaining));
			in.read(inBuf.data(), toRead);
			const auto got = static_cast<size_t>(in.gcount());
			if ( got == 0 )
				die("unexpected end of encrypted data");

			int outLen = 0;
			if ( EVP_DecryptUpdate(ctx.ctx, reinterpret_cast<unsigned char*>(outBuf.data()), &outLen,
			                        reinterpret_cast<const unsigned char*>(inBuf.data()), got) != 1 )
				die("decryption failed");
			out.write(outBuf.data(), outLen);
			if ( !out ) die("write failed");

			remaining -= got;
			processed += got;
			progress.draw("decrypting", processed, cipherLen);
		}

		unsigned char tag[TAG_SIZE];
		in.read(reinterpret_cast<char*>(tag), TAG_SIZE);
		if ( static_cast<size_t>(in.gcount()) != TAG_SIZE )
			die("could not read authentication tag");

		if ( EVP_CIPHER_CTX_ctrl(ctx.ctx, EVP_CTRL_GCM_SET_TAG, TAG_SIZE, tag) != 1 )
			die("could not set authentication tag");

		int outLen = 0;
		if ( EVP_DecryptFinal_ex(ctx.ctx, reinterpret_cast<unsigned char*>(outBuf.data()), &outLen) != 1 )
			die("decryption failed: wrong key or corrupted data");
		out.write(outBuf.data(), outLen);

		out.flush();
		if ( !out ) die("write failed");

		if ( progress.show ) ui::clearLine();
	}
}
