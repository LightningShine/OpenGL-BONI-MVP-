#pragma once
#include "../network/Server.h"
#include <serialib/serialib.h>
#include <string>
#include <vector>

// ============================================================================
// COM PORT MANAGEMENT (existing - don't modify)
// ============================================================================
void testSerial();
bool openCOMPort(const std::string& port_name);

// ============================================================================
// COM PORT DISCOVERY + SELECTION (new)
// ============================================================================

struct ComPortInfo
{
    std::string port;        // e.g. "COM5"
    std::string description; // e.g. "Silicon Labs CP210x USB to UART Bridge (COM5)"
};

// Starts background scanning thread (no-op if already started)
void startComPortAutoDiscovery();

// Stops background scanning thread
void stopComPortAutoDiscovery();

// Thread-safe snapshot of currently available ports
std::vector<ComPortInfo> getAvailableComPorts();

// Selected COM port label ("COM5"), thread-safe read
std::string getSelectedComPort();

// Select a COM port and start capture on it (will stop previous capture if needed)
bool selectAndOpenComPort(const std::string& port);

// Stop capture and close port (safe to call multiple times)
void stopRealDataCapture();

// ============================================================================
// DATA SOURCES
// ============================================================================

// Start reading real data from COM port (runs in separate thread)
void startRealDataCapture(const std::string& com_port);
