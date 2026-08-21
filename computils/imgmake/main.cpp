#include <cstdio>
#include "args.hpp"
#include "gpt.hpp"
#include "fat32.hpp"

int main()
{
    uint64_t totalSectors = 80;
    uint32_t totalSectors32 = (uint32_t)totalSectors;
    if (uint64_t(totalSectors32) != totalSectors) totalSectors32 = 0xFFFFFFFF;

    GPT::ProtMBR MBR(totalSectors32);

    GPT::PartitionEntry partitionEntries[128] = {};
    partitionEntries[0] = GPT::PartitionEntry(u"ESP", 2048, 2048 +8,
                                                (uint64_t)GPT::PEAttributeFLag::SystemPartition,
                                                RANDOM_GUID);
    partitionEntries[1]= GPT::PartitionEntry(u"OS", 2048 +9, 2048 +9 +16,
                                                (uint64_t)GPT::PEAttributeFLag::SystemPartition,
                                                RANDOM_GUID);

    const GPT::MSGUIDv7 headerGUID = RANDOM_GUID;
    GPT::Header header(80, false, partitionEntries, headerGUID);
    GPT::Header backupHeader(80, true, partitionEntries, headerGUID);

    return EXIT_SUCCESS;
}
