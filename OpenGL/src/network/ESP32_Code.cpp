#include "../network/ESP32_Code.h"


serialib serial;

bool openCOMPort(const std::string& port_name)
{
    int result = serial.openDevice(port_name.c_str(), 115200);

    if (result == 1) {
        std::cout << "Successfully opened " << port_name << std::endl;

        serial.flushReceiver();

        return true;
    }

    switch (result) {
    case -1: std::cerr << "Error: Device not found: " << port_name << std::endl; break;
    case -2: std::cerr << "Error: Error while setting port parameters." << std::endl; break;
    case -3: std::cerr << "Error: Another program is already using this port!" << std::endl; break;
    default: std::cerr << "Error: Unknown error opening port." << std::endl; break;
    }
    return false;
}


void readFromCOM()
{
    uint32_t header = 0;
    uint8_t byte;
    // Читаем по байту, пока не соберем 4 байта
    if (serial.readBytes(&byte, 1) > 0)
    {
        header = (header << 8) | byte;

        // Проверяем, является ли текущий header одним из наших типов
        if (header == (uint32_t)PacketType::TELEMETRY) {
            TelemetryPacket Tpacket;
            serial.readBytes((char*)&Tpacket + 4, sizeof(Tpacket) - 4);
        }
        
    }
}


void testSerial()
{
    if (openCOMPort("COM1"))
    {
        std::cout << "Connection established. Ready to receive data." << std::endl;

        std::this_thread::sleep_for(std::chrono::seconds(2));

        serial.closeDevice();
        std::cout << "Port closed." << std::endl;
    }
}