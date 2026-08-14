#include "TelemetryIngest.h"

#include "SimulationServer.h"
#include "../Config.h"

#include <atomic>
#include <memory>
#include <mutex>

#include <rajagp/RajaParser.h>

// Режим сервера: определён в сетевом слое, здесь только читается.
extern std::atomic<bool> g_is_server_mode;
extern std::atomic<bool> g_is_client_mode;

namespace telemetry
{
namespace
{
    std::mutex g_writer_mutex;
    std::unique_ptr<logging::TelemetryLogWriter> g_writer;

    std::mutex g_last_packet_mutex;
    TelemetryPacket g_last_packet{};
    std::atomic<bool> g_has_last_packet{ false };
}

bool ingest_start(logging::TelemetryLogSource source)
{
    std::lock_guard<std::mutex> lock(g_writer_mutex);
    if (g_writer)
        return false;

    g_writer = std::make_unique<logging::TelemetryLogWriter>(
        LoggingConstants::LOG_DIRECTORY, source);
    return true;
}

void ingest_stop()
{
    std::lock_guard<std::mutex> lock(g_writer_mutex);
    g_writer.reset();  // деструктор дописывает заголовок и проверяет файл
}

bool ingest_wire_packet(const uint8_t* payload_after_magic, TelemetryPacket* out_packet)
{
    if (payload_after_magic == nullptr)
        return false;

    // CRC проверяем ДО всего остального: битый пакет не должен ни попасть в
    // журнал, ни доехать до машин.
    TelemetryPacket packet{};
    if (!rajagp::parseRajaPayload(payload_after_magic, packet))
        return false;

    {
        std::lock_guard<std::mutex> lock(g_writer_mutex);
        if (g_writer)
            g_writer->write(payload_after_magic);
    }

    {
        std::lock_guard<std::mutex> lock(g_last_packet_mutex);
        g_last_packet = packet;
        g_has_last_packet.store(true, std::memory_order_relaxed);
    }

    processIncomingTelemetry(packet);

    // Ретрансляция клиентам — часть смысла «пакет принят», поэтому живёт здесь
    // же, а не у отдельного источника.
    if (g_is_server_mode && !g_is_client_mode)
        BroadcastTelemetryToClients(packet);

    if (out_packet != nullptr)
        *out_packet = packet;

    return true;
}

bool last_received_packet(TelemetryPacket& out_packet)
{
    if (!g_has_last_packet.load(std::memory_order_relaxed))
        return false;

    std::lock_guard<std::mutex> lock(g_last_packet_mutex);
    out_packet = g_last_packet;
    return true;
}

}
