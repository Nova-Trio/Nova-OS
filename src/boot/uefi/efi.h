#pragma once
#include <stdint.h>
#include <stddef.h>

#define EFIAPI __attribute__((ms_abi))
#define EFI_SUCCESS 0

#define EFI_LOADED_IMAGE_PROTOCOL_GUID { 0x5B1B31A1, 0x9562, 0x11D2, { 0x8E, 0x3F, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B } }
#define EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID { 0x964E5B22, 0x6459, 0x11D2, { 0x8E, 0x39, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B } }
#define EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID { 0x9042A9DE, 0x23DC, 0x4A38, { 0x96, 0xFB, 0x7A, 0xDE, 0xD0, 0x80, 0x51, 0x6A } }
#define EFI_ACPI_20_TABLE_GUID { 0x8868E871, 0xE4F1, 0x11D3, { 0xBC, 0x22, 0x00, 0x80, 0xC7, 0x3C, 0x88, 0x81 } }
#define EFI_ACPI_10_TABLE_GUID { 0xEB9D2D30, 0x2D88, 0x11D3, { 0x9A, 0x16, 0x00, 0x90, 0x27, 0x3F, 0xC1, 0x4D } }
#define EFI_FILE_INFO_ID { 0x09576E92, 0x6D3F, 0x11D2, { 0x8E, 0x39, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B } }


#define EFI_FILE_MODE_READ 0x0000000000000001ULL
#define EFI_FILE_MODE_WRITE 0x0000000000000002ULL
#define EFI_FILE_MODE_CREATE 0x8000000000000000ULL

#define EFI_FILE_READ_ONLY 0x0000000000000001ULL
#define EFI_FILE_HIDDEN 0x0000000000000002ULL
#define EFI_FILE_SYSTEM 0x0000000000000004ULL
#define EFI_FILE_RESERVED 0x0000000000000008ULL
#define EFI_FILE_DIRECTORY 0x0000000000000010ULL
#define EFI_FILE_ARCHIVE 0x0000000000000020ULL
#define EFI_FILE_VALID_ATTR 0x0000000000000037ULL

typedef void VOID;
typedef uint64_t UINTN;
typedef int64_t INTN;
typedef int32_t INT32;
typedef uint16_t CHAR16;
typedef UINTN EFI_STATUS;
typedef VOID *EFI_HANDLE;
typedef uint64_t EFI_PHYSICAL_ADDRESS;
typedef uint64_t EFI_VIRTUAL_ADDRESS;

typedef struct {
  uint64_t Signature;
  uint32_t Revision;
  uint32_t HeaderSize;
  uint32_t CRC32;
  uint32_t Reserved;
} EFI_TABLE_HEADER;

typedef struct {
  uint32_t Data1;
  uint16_t Data2;
  uint16_t Data3;
  uint8_t  Data4[8];
} EFI_GUID;

typedef struct {
  uint16_t Year;
  uint8_t  Month;
  uint8_t  Day;
  uint8_t  Hour;
  uint8_t  Minute;
  uint8_t  Second;
  uint8_t  Pad1;
  uint32_t Nanosecond;
  int16_t  TimeZone;
  uint8_t  Daylight;
  uint8_t  Pad2;
} EFI_TIME;

typedef struct {
  uint64_t Size;
  uint64_t FileSize;
  uint64_t PhysicalSize;
  EFI_TIME CreateTime;
  EFI_TIME LastAccessTime;
  EFI_TIME ModificationTime;
  uint64_t Attribute;
  CHAR16   FileName[];
} EFI_FILE_INFO;

struct _EFI_FILE_PROTOCOL;
typedef struct _EFI_FILE_PROTOCOL EFI_FILE_PROTOCOL;

typedef EFI_STATUS (EFIAPI *EFI_FILE_OPEN)(
  EFI_FILE_PROTOCOL *This,
  EFI_FILE_PROTOCOL **NewHandle,
  CHAR16 *FileName,
  uint64_t OpenMode,
  uint64_t Attributes
);

typedef EFI_STATUS (EFIAPI *EFI_FILE_CLOSE)(
  EFI_FILE_PROTOCOL *This
);

typedef EFI_STATUS (EFIAPI *EFI_FILE_DELETE)(
  EFI_FILE_PROTOCOL *This
);

typedef EFI_STATUS (EFIAPI *EFI_FILE_READ)(
  EFI_FILE_PROTOCOL *This,
  UINTN *BufferSize,
  VOID *Buffer
);

typedef EFI_STATUS (EFIAPI *EFI_FILE_WRITE)(
  EFI_FILE_PROTOCOL *This,
  UINTN *BufferSize,
  VOID *Buffer
);

typedef EFI_STATUS (EFIAPI *EFI_FILE_GET_POSITION)(
  EFI_FILE_PROTOCOL *This,
  uint64_t *Position
);

typedef EFI_STATUS (EFIAPI *EFI_FILE_SET_POSITION)(
  EFI_FILE_PROTOCOL *This,
  uint64_t Position
);

typedef EFI_STATUS (EFIAPI *EFI_FILE_GET_INFO)(
  EFI_FILE_PROTOCOL *This,
  EFI_GUID *InformationType,
  UINTN *BufferSize,
  VOID *Buffer
);

typedef EFI_STATUS (EFIAPI *EFI_FILE_SET_INFO)(
  EFI_FILE_PROTOCOL *This,
  EFI_GUID *InformationType,
  UINTN BufferSize,
  VOID *Buffer
);

typedef EFI_STATUS (EFIAPI *EFI_FILE_FLUSH)(
  EFI_FILE_PROTOCOL *This
);

struct _EFI_FILE_PROTOCOL {
  uint64_t Revision;
  EFI_FILE_OPEN Open;
  EFI_FILE_CLOSE Close;
  EFI_FILE_DELETE Delete;
  EFI_FILE_READ Read;
  EFI_FILE_WRITE Write;
  EFI_FILE_GET_POSITION GetPosition;
  EFI_FILE_SET_POSITION SetPosition;
  EFI_FILE_GET_INFO GetInfo;
  EFI_FILE_SET_INFO SetInfo;
  EFI_FILE_FLUSH Flush;
};

struct _EFI_SIMPLE_FILE_SYSTEM_PROTOCOL;
typedef struct _EFI_SIMPLE_FILE_SYSTEM_PROTOCOL EFI_SIMPLE_FILE_SYSTEM_PROTOCOL;

typedef EFI_STATUS (EFIAPI *EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_OPEN_VOLUME)(
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *This,
  EFI_FILE_PROTOCOL **Root
);

struct _EFI_SIMPLE_FILE_SYSTEM_PROTOCOL {
  uint64_t Revision;
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_OPEN_VOLUME OpenVolume;
};


typedef enum {
  AllocateAnyPages,
  AllocateMaxAddress,
  AllocateAddress,
  MaxAllocateType
} EFI_ALLOCATE_TYPE;

typedef struct {
  uint32_t Type;
  uint32_t Pad;
  EFI_PHYSICAL_ADDRESS PhysicalStart;
  EFI_VIRTUAL_ADDRESS VirtualStart;
  uint64_t NumberOfPages;
  uint64_t Attribute;
} EFI_MEMORY_DESCRIPTOR;

typedef enum {
  EfiReservedMemoryType,
  EfiLoaderCode,
  EfiLoaderData,
  EfiBootServicesCode,
  EfiBootServicesData,
  EfiRuntimeServicesCode,
  EfiRuntimeServicesData,
  EfiConventionalMemory,
  EfiUnusableMemory,
  EfiACPIReclaimMemory,
  EfiACPIMemoryNVS,
  EfiMemoryMappedIO,
  EfiMemoryMappedIOPortSpace,
  EfiPalCode,
  EfiPersistentMemory,
  EfiMaxMemoryType
} EFI_MEMORY_TYPE;

typedef enum {
  PixelRedGreenBlueReserved8BitPerColor,
  PixelBlueGreenRedReserved8BitPerColor,
  PixelBitMask,
  PixelBltOnly,
  PixelFormatMax
} EFI_GRAPHICS_PIXEL_FORMAT;

typedef struct {
  uint32_t RedMask;
  uint32_t GreenMask;
  uint32_t BlueMask;
  uint32_t ReservedMask;
} EFI_PIXEL_BITMASK;

typedef struct {
  uint32_t Version;
  uint32_t HorizontalResolution;
  uint32_t VerticalResolution;
  EFI_GRAPHICS_PIXEL_FORMAT PixelFormat;
  EFI_PIXEL_BITMASK PixelInformation;
  uint32_t PixelsPerScanLine;
} EFI_GRAPHICS_OUTPUT_MODE_INFORMATION;

typedef struct {
  uint32_t MaxMode;
  uint32_t Mode;
  EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *Info;
  UINTN SizeOfInfo;
  EFI_PHYSICAL_ADDRESS FrameBufferBase;
  UINTN FrameBufferSize;
} EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE;

struct _EFI_GRAPHICS_OUTPUT_PROTOCOL;
typedef struct _EFI_GRAPHICS_OUTPUT_PROTOCOL EFI_GRAPHICS_OUTPUT_PROTOCOL;

typedef EFI_STATUS (EFIAPI *EFI_GRAPHICS_OUTPUT_PROTOCOL_QUERY_MODE)(
  EFI_GRAPHICS_OUTPUT_PROTOCOL *This,
  uint32_t ModeNumber,
  UINTN *SizeOfInfo,
  EFI_GRAPHICS_OUTPUT_MODE_INFORMATION **Info
);

typedef EFI_STATUS (EFIAPI *EFI_GRAPHICS_OUTPUT_PROTOCOL_SET_MODE)(
  EFI_GRAPHICS_OUTPUT_PROTOCOL *This,
  uint32_t ModeNumber
);

typedef EFI_STATUS (EFIAPI *EFI_GRAPHICS_OUTPUT_PROTOCOL_BLT)(
  EFI_GRAPHICS_OUTPUT_PROTOCOL *This,
  VOID *BltBuffer,
  UINTN BltOperation,
  UINTN SourceX,
  UINTN SourceY,
  UINTN DestinationX,
  UINTN DestinationY,
  UINTN Width,
  UINTN Height,
  UINTN Delta
);

struct _EFI_GRAPHICS_OUTPUT_PROTOCOL {
  EFI_GRAPHICS_OUTPUT_PROTOCOL_QUERY_MODE QueryMode;
  EFI_GRAPHICS_OUTPUT_PROTOCOL_SET_MODE SetMode;
  EFI_GRAPHICS_OUTPUT_PROTOCOL_BLT Blt;
  EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE *Mode;
};

struct _EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL;
typedef struct _EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL;

struct _EFI_SIMPLE_TEXT_INPUT_PROTOCOL;
typedef struct _EFI_SIMPLE_TEXT_INPUT_PROTOCOL EFI_SIMPLE_TEXT_INPUT_PROTOCOL;

typedef EFI_STATUS (EFIAPI *EFI_TEXT_RESET)(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This, uint8_t ExtendedVerification);
typedef EFI_STATUS (EFIAPI *EFI_TEXT_STRING)(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This, const CHAR16 *String);
typedef EFI_STATUS (EFIAPI *EFI_TEXT_TEST_STRING)(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This, const CHAR16 *String);
typedef EFI_STATUS (EFIAPI *EFI_TEXT_QUERY_MODE)(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This, UINTN ModeNumber, UINTN *Columns, UINTN *Rows);
typedef EFI_STATUS (EFIAPI *EFI_TEXT_SET_MODE)(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This, UINTN ModeNumber);
typedef EFI_STATUS (EFIAPI *EFI_TEXT_SET_ATTRIBUTE)(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This, UINTN Attribute);
typedef EFI_STATUS (EFIAPI *EFI_TEXT_CLEAR_SCREEN)(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This);
typedef EFI_STATUS (EFIAPI *EFI_TEXT_SET_CURSOR_POSITION)(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This, UINTN Column, UINTN Row);
typedef EFI_STATUS (EFIAPI *EFI_TEXT_ENABLE_CURSOR)(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This, uint8_t Visible);


typedef struct {
  INT32 MaxMode;
  INT32 Mode;
  INT32 Attribute;
  INT32 CursorColumn;
  INT32 CursorRow;
  uint8_t CursorVisible;
} SIMPLE_TEXT_OUTPUT_MODE;

struct _EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL {
  EFI_TEXT_RESET Reset;
  EFI_TEXT_STRING OutputString;
  EFI_TEXT_TEST_STRING TestString;
  EFI_TEXT_QUERY_MODE QueryMode;
  EFI_TEXT_SET_MODE SetMode;
  EFI_TEXT_SET_ATTRIBUTE SetAttribute;
  EFI_TEXT_CLEAR_SCREEN ClearScreen;
  EFI_TEXT_SET_CURSOR_POSITION SetCursorPosition;
  EFI_TEXT_ENABLE_CURSOR EnableCursor;
  SIMPLE_TEXT_OUTPUT_MODE *Mode;
};

typedef struct {
  EFI_TABLE_HEADER Hdr;

  VOID *RaiseTPL;
  VOID *RestoreTPL;

  EFI_STATUS (EFIAPI *AllocatePages)(
    EFI_ALLOCATE_TYPE Type,
    EFI_MEMORY_TYPE MemoryType,
    UINTN Pages,
    EFI_PHYSICAL_ADDRESS *Memory
  );
  EFI_STATUS (EFIAPI *FreePages)(
    EFI_PHYSICAL_ADDRESS Memory,
    UINTN Pages
  );
  EFI_STATUS (EFIAPI *GetMemoryMap)(
    UINTN *MemoryMapSize,
    VOID *MemoryMap,
    UINTN *MapKey,
    UINTN *DescriptorSize,
    uint32_t *DescriptorVersion
  );
  EFI_STATUS (EFIAPI *AllocatePool)(
    EFI_MEMORY_TYPE PoolType,
    UINTN Size,
    VOID **Buffer
  );
  EFI_STATUS (EFIAPI *FreePool)(
    VOID *Buffer
  );

  VOID *CreateEvent;
  VOID *SetTimer;
  VOID *WaitForEvent;
  VOID *SignalEvent;
  VOID *CloseEvent;
  VOID *CheckEvent;

  VOID *InstallProtocolInterface;
  VOID *ReinstallProtocolInterface;
  VOID *UninstallProtocolInterface;
  EFI_STATUS (EFIAPI *HandleProtocol)(
    EFI_HANDLE Handle,
    EFI_GUID *Protocol,
    VOID **Interface
  );
  VOID *Reserved;
  VOID *RegisterProtocolNotify;
  VOID *LocateHandle;
  VOID *LocateDevicePath;
  VOID *InstallConfigurationTable;

  VOID *LoadImage;
  VOID *StartImage;
  VOID *Exit;
  VOID *UnloadImage;
  EFI_STATUS (EFIAPI *ExitBootServices)(
    EFI_HANDLE ImageHandle,
    UINTN MapKey
  );

  VOID *GetNextMonotonicCount;
  VOID *Stall;
  VOID *SetWatchdogTimer;

  VOID *ConnectController;
  VOID *DisconnectController;

  EFI_STATUS (EFIAPI *OpenProtocol)(
    EFI_HANDLE Handle,
    EFI_GUID *Protocol,
    VOID **Interface,
    EFI_HANDLE AgentHandle,
    EFI_HANDLE ControllerHandle,
    uint32_t Attributes
  );
  EFI_STATUS (EFIAPI *CloseProtocol)(
    EFI_HANDLE Handle,
    EFI_GUID *Protocol,
    EFI_HANDLE AgentHandle,
    EFI_HANDLE ControllerHandle
  );
  VOID *OpenProtocolInformation;

  VOID *ProtocolsPerHandle;
  VOID *LocateHandleBuffer;
  EFI_STATUS (EFIAPI *LocateProtocol)(
    EFI_GUID *Protocol,
    VOID *Registration,
    VOID **Interface
  );
  VOID *InstallMultipleProtocolInterfaces;
  VOID *UninstallMultipleProtocolInterfaces;

  VOID *CalculateCrc32;

  VOID *CopyMem;
  VOID *SetMem;
  VOID *CreateEventEx;
} EFI_BOOT_SERVICES;

typedef struct EFI_CONFIGURATION_TABLE {
  EFI_GUID VendorGuid;
  VOID *VendorTable;
}EFI_CONFIGURATION_TABLE;

typedef struct {
  EFI_TABLE_HEADER Hdr;
  CHAR16 *FirmwareVendor;
  uint32_t FirmwareRevision;
  EFI_HANDLE ConsoleInHandle;
  EFI_SIMPLE_TEXT_INPUT_PROTOCOL *ConIn;
  EFI_HANDLE ConsoleOutHandle;
  EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *ConOut;
  EFI_HANDLE StandardErrorHandle;
  EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *StdErr;
  VOID *RuntimeServices;
  EFI_BOOT_SERVICES *BootServices;
  UINTN NumberOfTableEntries;
  EFI_CONFIGURATION_TABLE *ConfigurationTable;
} EFI_SYSTEM_TABLE;

typedef struct {
  uint16_t ScanCode;
  CHAR16 UnicodeChar;
} EFI_INPUT_KEY;

typedef struct {
  uint32_t Revision;
  EFI_HANDLE ParentHandle;
  EFI_SYSTEM_TABLE *SystemTable;
  EFI_HANDLE DeviceHandle;
  VOID *FilePath;
  VOID *Reserved;
  uint32_t ImageDataSize;
  VOID *ImageBase;
  uint64_t ImageSize;
  EFI_MEMORY_TYPE ImageCodeType;
  EFI_MEMORY_TYPE ImageDataType;
  EFI_STATUS (EFIAPI *Unload)(EFI_HANDLE ImageHandle);
} EFI_LOADED_IMAGE_PROTOCOL;



typedef EFI_STATUS (EFIAPI *EFI_INPUT_RESET)(EFI_SIMPLE_TEXT_INPUT_PROTOCOL *This, uint8_t ExtendedVerification);
typedef EFI_STATUS (EFIAPI *EFI_INPUT_READ_KEY)(EFI_SIMPLE_TEXT_INPUT_PROTOCOL *This, EFI_INPUT_KEY *Key);

struct _EFI_SIMPLE_TEXT_INPUT_PROTOCOL {
  EFI_INPUT_RESET Reset;
  EFI_INPUT_READ_KEY ReadKeyStroke;
  VOID *WaitForKey;
};

