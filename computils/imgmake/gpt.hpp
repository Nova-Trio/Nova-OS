#pragma once
#include <cstddef>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <chrono>
#include <random>
#include "utils.hpp"
namespace GPT {

// Basic GPT disk architecture:
// Sector 0: Protective MBR
// Sector 1: Primary GPT Header
// Sector 2-33: GPT Partition Entry Array
// Sector 34-(N-34): Usable LBA space:
//   Sector 34-2047: Alignment Gap
//   Sector 2048+: ESP (Possible Stage 2 for BIOS environment)
//   Sector (After ESP End)+: OS Partition
// Sector (N-33)-(N-2): Backup GPT Partition Entry Array
// Sector (N-1): Backup GPT Header

#define UNDEFINED_INT 0
#define UNDEFINED_ARRAY {}

// Microsoft GUID using UUIDv7 technique (timestamp/random bits, minus big endian)
// Fields are normal little endian except data4,
//   unlike UUIDv4/7 which is big endian
struct MSGUIDv7
{
    uint32_t data1 = 0;
    uint16_t data2 = 0;
    uint16_t data3 = 0; // 4 version bits high 12-15
    uint8_t data4[8] = {}; // 2-4 (variable length) variant bits high 4-7 (variable length) of data4[0]
    
    MSGUIDv7(uint32_t fill)
    {
        // Data
        memset(this, fill, sizeof(MSGUIDv7));
        // Set UUID version 7
        data3 &= 0x0FFF;
        data3 |= (0x7 << 12);
        // Set UUID variant 1 (its pattern is 10b = 2)
        data4[0] &= 0b00111111;
        data4[0] |= (0b10 << 6);
    }
    MSGUIDv7()
    {
        /*
        struct
        {
            uint8_t timestamp[6] = UNDEFINED_ARRAY; // 48 bits
            uint16_t ver_rand_a;
            uint64_t var_rand_b;
        } UUIDv7;
        */
        uint64_t time = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::system_clock::now().time_since_epoch()
        ).count();
        uint64_t random1 = 0, random2 = 0;
        {
            thread_local std::random_device randomDevice;
            thread_local std::mt19937_64 generator(randomDevice());
            thread_local std::uniform_int_distribution<uint64_t> distributor;
            random1 = distributor(generator);
            random2 = distributor(generator);
        }
        // 48 bit timestamp
        data1 = (uint32_t)(time >> 16);
        data2 = (uint16_t)(time & 0xFFFF);
        // Random bits
        data3 = (uint16_t)(random1);
        data4[0] = (uint8_t)(random2 >> 56);
        data4[1] = (uint8_t)(random2 >> 48);
        data4[2] = (uint8_t)(random2 >> 40);
        data4[3] = (uint8_t)(random2 >> 32);
        data4[4] = (uint8_t)(random2 >> 24);
        data4[5] = (uint8_t)(random2 >> 16);
        data4[6] = (uint8_t)(random2 >> 8);
        data4[7] = (uint8_t)(random2);
        // Set UUID version 7
        data3 &= 0x0FFF;
        data3 |= (0x7 << 12);
        // Set UUID variant 1 (its pattern is 10b = 2)
        data4[0] &= 0b00111111;
        data4[0] |= (0b10 << 6);
    }
    MSGUIDv7(std::string_view str)
    {
        if (str.length() != 36 || str[8] != '-' || str[13] != '-' || str[18] != '-' || str[23] != '-')
        {
            perror("invalid GUID string @ MSGUID::hexToByte");
            exit(EXIT_FAILURE);
        }
        // Example UUIDv7: 00000000-0000-7000-8000-000000000000
        // First segment (8 digits = 4 bytes)
        data1 = parseHex<uint32_t>(str.substr(0, 8));
        // Second segment (4 digits = 2 bytes)
        data2 = parseHex<uint16_t>(str.substr(9, 4));
        // Third segment (4 digits = 2 bytes)
        data3 = parseHex<uint16_t>(str.substr(14, 4));
        // Fourth segment (4 digits = 2 bytes)
        data4[0] = parseHex<uint8_t>(str.substr(19, 2));
        data4[1] = parseHex<uint8_t>(str.substr(21, 2));
        // Fifth segment (12 digits = 6 bytes)
        for (size_t i = 0; i < 6; i++)
            data4[2 +i] = parseHex<uint8_t>(str.substr(24 + (i *2), 2));
    }

    std::string toStr() const
    {
        std::string str(36, '?');
        std::snprintf(str.data(), str.length() +1,
                    "%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X",
                    data1, data2, data3, data4[0], data4[1],
                    data4[2], data4[3], data4[4],
                    data4[5], data4[6], data4[7]);
        return str;
    }

private:
    template <typename T>
    static T parseHex(std::string_view hex)
    {
        auto getDigit = [](char c) -> T
        {
            if (c >= '0' && c <= '9') return c -'0';
            else if (c >= 'a' && c <= 'f') return c -'a' +10;
            else if (c >= 'A' && c <= 'F') return c -'A' +10;
            else
            {
                perror((std::string("invalid hexadecimal character \'") +c +"\' @MSGUID::parseHex").c_str());
                exit(EXIT_FAILURE);
            }
        };
        T result = 0;
        for (char c : hex) result = (result << 4) | getDigit(c);
        return result;
    }
};

#define UNDEFINED_GUID GPT::MSGUIDv7(0)
#define RANDOM_GUID GPT::MSGUIDv7()
#define ESP_TYPE_GUID GPT::MSGUIDv7("C12A7328-F81F-11D2-BA4B-00A0C93EC93B")

struct ProtMBR
{
    uint8_t nullFirst[446] = {};
    struct
    {
        uint8_t bootIndicator = 0; // Non-Bootable
        uint8_t startingCHS[3] = {0, 2, 0}; // 0x200
        uint8_t partitionType = 0xEE; // GPT Protective Partition
        uint8_t endingCHS[3] = {0xFF, 0xFF, 0xFF}; // 0xFFFFFF
        uint32_t startingLBA = 1;
        uint32_t sizeInSectors = UNDEFINED_INT; // Must be set to (Total -1)  (Max 0xFFFFFFFF)
    } partitionEntry1;
    uint8_t nullSecond[48] = {};
    uint16_t signature = 0xAA55;

    ProtMBR(uint32_t totalSectors32)
    {
        partitionEntry1.sizeInSectors = totalSectors32 -1;
    }
};

enum class PEAttributeFLag : uint64_t
{
    SystemPartition = (1ULL << 0), // Critical system partition (Bit 0)
    IgnoreByEFI = (1ULL << 1), // Firmware skips booting (Bit 1)
    LegacyBootable = (1ULL << 2) // Legacy BIOS active flag (Bit 2)
};

struct PartitionEntry
{
    MSGUIDv7 partitionTypeGUID = UNDEFINED_GUID; // OS-dependant, can be anything
    MSGUIDv7 uniquePartitionGUID = UNDEFINED_GUID;
    uint64_t firstLBA = UNDEFINED_INT;
    uint64_t lastLBA = UNDEFINED_INT;
    uint64_t attributeFlags = UNDEFINED_INT;
    uint16_t partitionName16[36] = UNDEFINED_ARRAY;

    PartitionEntry() = default;
    PartitionEntry(const char16_t* partitionName16, uint64_t firstLBA, uint64_t lastLBA,
                    uint64_t attributeFlags, MSGUIDv7 partitionGUID)
    {
        this->uniquePartitionGUID = partitionGUID;
        this->firstLBA = firstLBA;
        this->lastLBA = lastLBA;
        this->attributeFlags = attributeFlags;
        for (size_t i = 0; i < 36; i++)
        {
            if (partitionName16[i] == '\0') break;
            else this->partitionName16[i] = partitionName16[i];
        }
    }
};

struct Header
{
    uint64_t signature = UNDEFINED_INT; // Must be set to "EFI PART"
    uint32_t revision = 0x10000; // GPT version 1.0
    uint32_t headerSize = 92;
    uint32_t headerCRC32 = UNDEFINED_INT; // Must be set to CRC32 checksum (HAS TO BE 0 INITIALLY)
    uint32_t reserved = 0;
    uint64_t myLBA = UNDEFINED_INT; // Must be set to header LBA
    uint64_t alternateLBA = UNDEFINED_INT; // Must be set to header opposite LBA (backup for normal, normal for backup)
    uint64_t firstUsableLBA = 34;
    uint64_t lastUsableLBA = UNDEFINED_INT; // Must be set to (Total -34)
    MSGUIDv7 diskGUID = UNDEFINED_GUID; // Must be set to a random GUID
    uint64_t partitionEntryLBA = UNDEFINED_INT; // Must be set to parition entry array start
    uint32_t numberOfPartitionEntries = 128;
    uint32_t sizeOfPartitionEntry = 128;
    uint32_t partitionEntryArrayCRC32 = UNDEFINED_INT; // Must be set to partiton array CRC32 checksum
    uint8_t reservedBlock[420] = {};

    Header(uint64_t totalSectors, bool backupHeader,
                PartitionEntry* partitionEntryArray, MSGUIDv7 diskGUID)
    {
        memcpy(&signature, "EFI PART", 8);

        myLBA = (!backupHeader)? 1  : totalSectors -1;
        alternateLBA = (!backupHeader)? totalSectors -1  : 1;
        lastUsableLBA = totalSectors -34;

        this->diskGUID = diskGUID;
        partitionEntryLBA = (!backupHeader)? 2  : totalSectors -33;

        partitionEntryArrayCRC32 = utils::crc32(partitionEntryArray, 128*128);
        headerCRC32 = utils::crc32(this, 92);
    }
};
    
}
