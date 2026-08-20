#pragma once
#include <cstdio>
#include <cstddef>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <chrono>
#include <random>
#include "utils.hpp"
namespace GPT {

// Sector 0: Protective MBR
// Sector 1: Primary GPT Header
// Sector 2-33: GUID Partition Entry Array
// Sector 34-(N-34): Usable LBA space:
//   Sector 34-2047: Alignment Gap
//   Sector 2048+: ESP (Possible Stage 2 for BIOS environment)
//   Sector (After ESP End)+: OS Partition
// Sector (N-33)-(N-2): Backup GUID Partition Entry Array
// Sector (N-1): Backup GPT Header

#define UNDEFINED_INT 0
#define UNDEFINED_ARRAY {}

// Microsoft GUID
struct MSGUID
{
    uint32_t data1 = UNDEFINED_INT; // Little endian
    uint16_t data2 = UNDEFINED_INT; // Little endian
    uint16_t data3 = UNDEFINED_INT; // Little endian
    uint8_t data4[8] = UNDEFINED_ARRAY; // Big endian
    // Version after bit 48
    // Variation also after idk
    /*
    struct {
        uint32_t g1 = UNDEFINED_INT; // Little endian
        uint16_t g2 = UNDEFINED_INT; // Little endian
        uint16_t g3 = UNDEFINED_INT; // Little endian
        uint8_t g4[8] = UNDEFINED_ARRAY; // Big endian
    } MS;
    struct {
        uint32_t rand_a = UNDEFINED_INT;
        uint16_t rand_b = UNDEFINED_INT;
        uint16_t rand_c_ver;
        uint8_t rand_d_var;
        uint8_t rand_e[6] = UNDEFINED_ARRAY;
    } UUIDv4;
    struct {
        uint8_t timestamp[6] = UNDEFINED_ARRAY;
        uint16_t ver_rand_a;
        uint64_t var_rand_b;
    } UUIDv7;
    */
    
    MSGUID(std::string_view str)
    {
        if (str.length() != 36 || str[8] != '-' || str[13] != '-' || str[18] != '-' || str[23] != '-')
        {
            perror("invalid GUID string @ MSGUID::hexToByte");
            exit(EXIT_FAILURE);
        }

        // Data1: 32-bit unsigned integer (Little-Endian storage)
        data1 = parseHex<uint32_t>(str.substr(0, 8));

        // Data2: 16-bit unsigned integer (Little-Endian storage)
        data2 = parseHex<uint16_t>(str.substr(9, 4));

        // Data3: 16-bit unsigned integer (Little-Endian storage)
        data3 = parseHex<uint16_t>(str.substr(14, 4));

        // Data4: 8-byte array (Big-Endian byte order maintained)
        data4[0] = parseHex<uint8_t>(str.substr(19, 2));
        data4[1] = parseHex<uint8_t>(str.substr(21, 2));
        
        // Skip the fourth hyphen at index 23
        for (int i = 0; i < 6; i++)
        {
            data4[2 +i] = parseHex<uint8_t>(str.substr(24 + (i *2), 2));
        }
    }

private:
    static uint8_t hexToByte(char c)
    {
        if (c >= '0' && c < '9') return c -'0';
        else if (c >= 'a' && c < 'f') return c -'a' +10;
        else if (c >= 'A' && c < 'F') return c -'A' +10;
        else
        {
            perror("invalid hexadecimal character @ MSGUID::hexToByte");
            exit(EXIT_FAILURE);
        }
    }

    template <typename T>
    static T parseHex(std::string_view hex)
    {
        T result = 0;
        for (char c : hex) result = (result << 4) | hexToByte(c);
        return result;
    }

    /*
    GUID(uint32_t n1, uint16_t n2, uint16_t n3, uint16_t n4, uint64_t n5)
    {
        // Low endian
        microsoft.g1 = n1;
        microsoft.g2 = n2;
        microsoft.g3 = n3;
        // Big endian
        microsoft.g4[0] = (uint8_t)(n4 >> 8);
        microsoft.g4[1] = (uint8_t)(n4 & 0xFF);
        microsoft.g4[2] = (uint8_t)(n5 >> 40);
        microsoft.g4[3] = (uint8_t)(n5 >> 32);
        microsoft.g4[4] = (uint8_t)(n5 >> 24);
        microsoft.g4[5] = (uint8_t)(n5 >> 16);
        microsoft.g4[6] = (uint8_t)(n5 >> 8);
        microsoft.g4[7] = (uint8_t)(n5 & 0xFF0);
    }
    */
};

#define UNDEFINED_GUID GUID("\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0")
#define ESP_TYPE_GUID GUID("C12A7328-F81F-11D2-BA4B-00A0C93EC93B")
;-;

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
    GUID partitionTypeGUID = UNDEFINED_GUID; // OS-dependant, can be anything
    GUID uniquePartitionGUID = UNDEFINED_GUID;
    uint64_t firstLBA = UNDEFINED_INT;
    uint64_t lastLBA = UNDEFINED_INT;
    uint64_t attributeFlags = UNDEFINED_INT;
    uint16_t partitionName16[36] = UNDEFINED_ARRAY;

    PartitionEntry() = default;
    PartitionEntry(const char16_t* partitionName16, uint64_t firstLBA, uint64_t lastLBA,
                    uint64_t attributeFlags, GUID partitionGUID)
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

struct GPTHeader
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
    GUID diskGUID = UNDEFINED_GUID; // Must be set to a random GUID
    uint64_t partitionEntryLBA = UNDEFINED_INT; // Must be set to parition entry array start
    uint32_t numberOfPartitionEntries = 128;
    uint32_t sizeOfPartitionEntry = 128;
    uint32_t partitionEntryArrayCRC32 = UNDEFINED_INT; // Must be set to partiton array CRC32 checksum
    uint8_t reservedBlock[420] = {};

    GPTHeader(uint64_t totalSectors, bool backupHeader,
                PartitionEntry* partitionEntryArray, GUID diskGUID)
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
