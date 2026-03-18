#pragma once
#include "../network/Server.h"
#include <serialib/serialib.h>

// ============================================================================
// COM PORT MANAGEMENT (existing - don't modify)
// ============================================================================
void testSerial();
bool openCOMPort(const std::string& port_name);

// ============================================================================
// DATA SOURCES
// ============================================================================

// Start reading real data from COM port (runs in separate thread)
void startRealDataCapture(const std::string& com_port);
