#pragma once

#include <filesystem>
#include <string>
#include <utility>

/**
 * @brief Client-side end-to-end encryption (AES-256-GCM)
 *
 * Ciphertext layout: [12 B nonce][encrypted data][16 GCM tag]
 * A fresh random key is generated for every encryption; the server never sees it.
 */
namespace E2E {

	/**
	 * @brief Encrypts source into a temporary file
	 *
	 * @param source file to encrypt
	 * @param quiet suppress the progress bar
	 * @return path of the temporary ciphertext file and the key as 64 hex characters
	 */
	std::pair<std::filesystem::path, std::string> encryptFile ( const std::filesystem::path& source, bool quiet = false );

	/**
	 * @brief Decrypts a file created by encryptFile
	 *
	 * @param source the ciphertext file
	 * @param keyHex the key as 64 hex characters
	 * @param destination where the plaintext is written
	 * @param quiet suppress the progress bar
	 *
	 * @throws std::runtime_error on a wrong key, tampered data or I/O failure
	 */
	void decryptFile ( const std::filesystem::path& source, const std::string& keyHex,
	                   const std::filesystem::path& destination, bool quiet = false );
}
