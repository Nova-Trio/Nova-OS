// this is a prototype for ELf plz delete this

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "inc/auto.h"
#include "inc/vga.h"

void print(const char* str)
{
    //uint16_t* test = (uint16_t*)"doujoeiwwewer";
    volatile uint16_t* const vg = (uint16_t*)(0xB8000);
    for (size_t i = 0; ; i++)
    {
        if (str[i] == '\0') return;
        else vg[i] = str[i] | (0x0F << 8);
    }
}

#pragma pack(push, 1)

typedef struct
{
    bool status;
    uint8_t startHSC[3];
    uint8_t type;
    uint8_t endHSC[3];
    uint32_t startLBA;
    uint32_t sectorCount;
} PartitionEntry;

typedef struct
{
    uint16_t SIZE;
    uint16_t infoFlags;
    uint32_t cylinderCount;
    uint32_t headCount;
    uint32_t sectorsPerTrack;
    uint64_t sectorCount;
    uint16_t bytesPerSector;
} DriveParams26;

typedef struct
{
    PartitionEntry* pPartitionTable;
    DriveParams26* pDriveParams;
    uint8_t bootDrive;
} S1t2Ball;

typedef struct
{
    uint64_t address;
    uint64_t length;
    uint32_t type;
    uint32_t ACPI3Info;
} MMapEntry24;

typedef enum
{
    MME24_TYPE_FREE = 1, MME24_TYPE_RESERVED,
    MME24_TYPE_ACPI_RECLAIM, MME24_TYPE_ACPI_NVS
} MME24Type;

typedef struct
{
    uint16_t entryCount;
    bool bytes24;
} MMapLayout;

// Pointer (non-page-sized) page table entry
// For PML4, PDPT, and PD entries
// NOTICE: "address" in the fields actually starts from bit 12 of the full address not 0
typedef struct
{
    /* Lower flags:
        0: P (Present) (set = available in physical memory, clear = not available (page fault when used))
        1: R/W (set = RW, clear = read-only)
        2: U/S (User/Supervisor) (set = anyone can access, clear = only supervisor can access)
        3: PWT (Page Write-Through) (set = write-through caching, clear = write-back caching)
        4: PCD (Page Cache-Disable) (set = disable cache, clear = enable cache)
        5: A (Access) (set = entry accessed, clear = entry not accessed) (set by CPU, but not cleared automatically)
        6: AVL low (Available) (AVL bits are ignored by the CPU, the OS can use them)
        7: Reserved/PS (Page size) (set = page sized (points to a memory page), clear = points to other entries)
            (must be clear for this struc's purpose)
    */
    uint8_t lowerFlags;
    
    /* AVL middle-low & address low:
        0-3: AVL middle-low
        4-31: address low (INCLUDES POSSIBLE RESERVED BITS)
    */
    uint32_t AVLMidlow_addressLow;
    
    /* Address high, AVL middle-high:
        0-11: address high (INCLUDES POSSIBLE RESERVED BITS)
        12-15: AVL middle-high
    */
   uint16_t addressHigh_AVLMidhigh;


   /* AVL high, XD flag:
        0-6: AVL high
        7: XD (Execute Disable) (set = execution disabled, set = execution enabled)
            (used if bit 11 (NXE) in the EFER registeris set, otherwise the XD bit is reserved)
   */
  uint8_t AVLHigh_XD;
} PageTablePtrEntry;

/*
Page (page-sized) table entry
For PDPT/PD entries pointing directly to a 2MB/1GB block of memory, or standard PT entries
NOTICE: "address" in the fields actually doesn't start from bit 0 but starts from:
    bit 30 of the full address for PDPT direct pages
    bit 21 of the full address for PD direct pages
    bit 12 of the full address for PT pages
*/
typedef struct
{
    /* Lower flags:
        0: P (Present) (set = available in physical memory, clear = not available (page fault when used))
        1: R/W (set = RW, clear = read-only)
        2: U/S (User/Supervisor) (set = anyone can access, clear = only supervisor can access)
        3: PWT (Page Write-Through) (set = write-through caching, clear = write-back caching)
        4: PCD (Page Cache-Disable) (set = disable cache, clear = enable cache)
        5: A (Access) (set = entry accessed, clear = entry not accessed) (set by CPU, but not cleared automatically)
        6: D (Dirty) (set = page has been written to, clear = page hasn't been written to)
        7: In PDPT and PD: PS (Page size) (set = page sized (points to a memory page), clear = points to other entries)
             (must be set for this struc's purpose)
           In standard PT: PAT (Page Attribute Table) (osdev.org didn't say what it does exactly. not my fault)
             (If PAT is supported, then PAT with PCD and PWT indicate the memory caching type, otherwise it's reserved)
    */
    uint8_t lowerFlags;

    /* G, AVL low, PAT, address low:
        0: G (Global) (set = CPU shouldn't invalidate the TLB (Translation lookaside buffer) entry corresponding to the page upon a MOV to CR3,
                         clear = oppsoite)
             (Bit 7 (PGE) of CR4 must be set to enable global pages)
        1-3: AVL low (Available) (AVL bits are ignored by the CPU, the OS can use them)
        4: In PDPT and PD: PAT (Page Attribute Table) (osdev.org didn't say what it does exactly. not my fault)
             (If PAT is supported, then PAT with PCD and PWT indicate the memory caching type, otherwise it's reserved)
           In standard PT: part of address low
        5-31: address low (INCLUDES POSSIBLE RESERVED BITS)
    */
    uint32_t flags_AVLLow_addressLow;

    /* Address high, AVL middle:
        0-11: address high (INCLUDES POSSIBLE RESERVED BITS)
        12-15: AVL middle
    */
    uint16_t addressHigh_AVLMid;

   /* AVL high, PK, XD flag:
        0-2: AVL high
        3-6: PK (Protection Key) (Allows the OS to enable/disable access rights for page entries)
        7: XD (Execute Disable) (set = execution disabled, set = execution enabled)
            (used if bit 11 (NXE) in the EFER registeris set, otherwise the XD bit is reserved)
   */
  uint8_t AVLHigh_PK_XD;
} PageTablePageEntry;

typedef uint64_t PageTableGenericEntry;

#pragma pack(pop)



void main(S1t2Ball* pS1t2Ball, MMapLayout* pMMapLayout, MMapEntry24* mMap, void* pEBDA)
{
    /*
    volatile uint16_t* const VGA = (uint16_t*)(0xB8000);
    VGA[0] = 'H' | (0x0F << 8);
    
    print("Hello from 64 BIT LONG MODE C!!!!!!!!"
        " ALSO FUCK THE CLANG LINKER IT KEPT RUINING MY CODE!!!!");

    vga_printf("Hello! Stage-1-to-2-ball address = #x\n", pS1t2Ball);
    */
    while (true)
    {
    }

    // Check commandline arguments

    // Check driver list

    // Load drivers

    // Call drivers
}
