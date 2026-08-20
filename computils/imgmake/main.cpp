#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stddef.h>
#include <stdio.h>

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

struct GUID16
{
    uint32_t g1 = 0;
    uint16_t g2 = 0;
    uint16_t g3 = 0;
    uint8_t g4g5[8] = {0, 0, 0, 0, 0, 0, 0, 0};

    GUID16(const char* str)
    {
        strncpy((char*)this, str, 16);
    }
};

#define UNDEFINED_GUID16 GUID16("\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0")
#define ESP_TYPE_GUID16 GUID16("C12A7328-F81F-11D2-BA4B-00A0C93EC93B")

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

enum class AttributeFLag : uint64_t
{
    SystemPartition = (1ULL << 0), // Critical system partition (Bit 0)
    IgnoreByEFI = (1ULL << 1), // Firmware skips booting (Bit 1)
    LegacyBootable = (1ULL << 2) // Legacy BIOS active flag (Bit 2)
};

struct PartitionEntry
{
    GUID16 partitionTypeGUID = UNDEFINED_GUID16; // OS-dependant, can be anything
    GUID16 uniquePartitionGUID = UNDEFINED_GUID16;
    uint64_t firstLBA = UNDEFINED_INT;
    uint64_t lastLBA = UNDEFINED_INT;
    uint64_t attributeFlags = UNDEFINED_INT;
    uint16_t partitionName16[36] = UNDEFINED_ARRAY;

    PartitionEntry() = default;
    PartitionEntry(const char16_t* partitionName16, uint64_t firstLBA, uint64_t lastLBA,
                    uint64_t attributeFlags, GUID16 partitionGUID)
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
    GUID16 diskGUID = UNDEFINED_GUID16; // Must be set to a random GUID
    uint64_t partitionEntryLBA = UNDEFINED_INT; // Must be set to parition entry array start
    uint32_t numberOfPartitionEntries = 128;
    uint32_t sizeOfPartitionEntry = 128;
    uint32_t partitionEntryArrayCRC32 = UNDEFINED_INT; // Must be set to partiton array CRC32 checksum
    uint8_t reservedBlock[420] = {};

    GPTHeader(uint64_t totalSectors, bool backupHeader,
                PartitionEntry* partitionEntryArray, GUID16 diskGUID)
    {
        memcpy(&signature, "EFI PART", 8);

        myLBA = (!backupHeader)? 1  : totalSectors -1;
        alternateLBA = (!backupHeader)? totalSectors -1  : 1;
        lastUsableLBA = totalSectors -34;

        this->diskGUID = diskGUID;
        partitionEntryLBA = (!backupHeader)? 2  : totalSectors -33;

        partitionEntryArrayCRC32 = crc32(partitionEntryArray, 128*128);
        headerCRC32 = crc32(this, 92);
    }
};

int main()
{
    uint64_t totalSectors = 80;
    uint32_t totalSectors32 = (uint32_t)totalSectors;
    if (uint64_t(totalSectors32) != totalSectors) totalSectors32 = 0xFFFFFFFF;

    ProtMBR MBR(totalSectors32);

    PartitionEntry partitionEntries[128] = {};
    partitionEntries[0] = PartitionEntry(u"ESP", 2048, 2048 +8, (uint64_t)AttributeFLag::SystemPartition,
                                        "ESP AAAAAAA EEEE");
    partitionEntries[1]= PartitionEntry(u"OS", 2048 +9, 2048 +9 +16, (uint64_t)AttributeFLag::SystemPartition,
                                        "OS AAAAAAAA EEEE");

    GPTHeader header(80, false, partitionEntries, "RANDOM SLOP IDK.");
    GPTHeader backupHeader(80, true, partitionEntries, "RANDOM SLOP IDK.");

    printf("%08X\n", crc32("123456789", 9));
    return EXIT_SUCCESS;
}
