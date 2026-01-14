#pragma once

#include <iomanip>
#include <ios>
#include <memory>
#include <string>
#include <sodium/utils.h>
#include <sys/sysinfo.h>

std::string humanReadableSize ( size_t size );

std::string humanReadableSpeed ( double speed );

std::string binToHex ( const unsigned char* bin, size_t size );

std::pair<unsigned char*, size_t> hexToBin ( const std::string& hex );

unsigned long getFreeMemory ();

std::string padStringToSize ( const std::string& str, const unsigned totalLength );