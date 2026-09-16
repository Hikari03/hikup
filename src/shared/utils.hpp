#pragma once

#include <iomanip>
#include <ios>
#include <memory>
#include <string>
#include <sys/sysinfo.h>

std::string humanReadableSize ( size_t size );

std::string humanReadableSpeed ( double speed );

unsigned long getFreeMemory ();

std::string padStringToSize ( const std::string& str, unsigned totalLength );

std::string bytesToHex(const unsigned char* data, size_t length);