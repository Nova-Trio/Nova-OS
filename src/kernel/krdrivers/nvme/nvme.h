#pragma once
#include <stdint.h>
#include <stddef.h>
#include <pcie.h>

#define PCI_CLASS_STORAGE 0x01
#define PCI_SUBCLASS_NVM  0x08
#define PCI_PROGIF_NVME   0x02

#define NVME_CC_EN (1U << 0)
#define NVME_CC_CSS_NVM (0U << 4)
#define NVME_CC_MPS_4K (0U << 7)
#define NVME_CC_AMS_RR (0U << 11)
#define NVME_CC_IOSQES_64 (6U << 16)
#define NVME_CC_IOCQES_16 (4U << 20)

#define NVME_CSTS_RDY (1U << 0)
#define NVME_CSTS_CFS (1U << 1)

#define NVME_ADMIN_QUEUE_ENTRIES 64

#define NVME_ADMIN_OP_IDENTIFY 0x06

#define NVME_ADMIN_OP_CREATE_IOSQ 0x01
#define NVME_ADMIN_OP_CREATE_IOCQ 0x05
#define NVME_NVM_OP_READ 0x02

#define NVME_IO_QUEUE_ENTRIES 64

#define NVME_IRQ_VECTOR 0x30



typedef struct {
  volatile uint64_t cap;
  volatile uint32_t vs;
  volatile uint32_t intms;
  volatile uint32_t intmc;
  volatile uint32_t cc;
  volatile uint32_t reserved0;
  volatile uint32_t csts;
  volatile uint32_t nssr;
  volatile uint32_t aqa;
  volatile uint64_t asq;
  volatile uint64_t acq;
  volatile uint32_t cmbloc;
  volatile uint32_t cmbsz;
  volatile uint32_t bpinfo;
  volatile uint32_t bprsel;
  volatile uint64_t bpmbl;
  volatile uint64_t cmbmsc;
  volatile uint32_t cmbsts;
  volatile uint32_t reserved1[847];
} __attribute__((packed)) NvmeRegisters;

typedef struct {
  uint32_t cdw0;
  uint32_t nsid;
  uint64_t reserved0;
  uint64_t mptr;
  uint64_t dptr[2];
  uint32_t cdw10;
  uint32_t cdw11;
  uint32_t cdw12;
  uint32_t cdw13;
  uint32_t cdw14;
  uint32_t cdw15;
} __attribute__((packed)) NvmeSqe;

typedef struct {
  uint32_t cdw0;
  uint32_t reserved;
  uint16_t sq_head;
  uint16_t sq_id;
  uint16_t command_id;
  uint16_t status;
} __attribute__((packed)) NvmeCqe;

typedef struct {
  volatile uint8_t done;
  uint16_t status;
  NvmeCqe cqe;
} NvmeCommandContext;

typedef struct {
  NvmeSqe *sq;
  uint64_t sq_phys;
  NvmeCqe *cq;
  uint64_t cq_phys;
  volatile uint32_t *sq_doorbell;
  volatile uint32_t *cq_doorbell;
  uint16_t size;
  uint16_t sq_tail;
  uint16_t cq_head;
  uint16_t cq_phase;
  NvmeCommandContext *contexts;
} NvmeQueue;

typedef struct {
  uint16_t ms;
  uint8_t ds;
  uint8_t rp;
} __attribute__((packed)) NvmeLbaFormat;

typedef struct {
  uint16_t vid;
  uint16_t ssvid;
  char sn[20];
  char mn[40];
  char fr[8];
  uint8_t rab;
  uint8_t ieee[3];
  uint8_t cmic;
  uint8_t mdts;
  uint16_t cntlid;
  uint32_t ver;
  uint8_t reserved0[172];
  uint16_t oacs;
  uint8_t acl;
  uint8_t aerl;
  uint8_t frmw;
  uint8_t lpa;
  uint8_t elpe;
  uint8_t npss;
  uint8_t avscc;
  uint8_t apsta;
  uint16_t wctemp;
  uint16_t cctemp;
  uint8_t reserved1[242];
  uint8_t sqes;
  uint8_t cqes;
  uint16_t maxcmd;
  uint32_t nn;
  uint8_t reserved2[3576];
} __attribute__((packed)) NvmeIdentifyController;

typedef struct {
  uint64_t nsze;
  uint64_t ncap;
  uint64_t nuse;
  uint8_t nsfeat;
  uint8_t nlbaf;
  uint8_t flbas;
  uint8_t mc;
  uint8_t dpc;
  uint8_t dps;
  uint8_t nmic;
  uint8_t rescap;
  uint8_t fpi;
  uint8_t reserved0;
  uint16_t nawun;
  uint16_t nawupf;
  uint16_t nacwu;
  uint16_t nabsn;
  uint16_t nabo;
  uint16_t nabspf;
  uint16_t reserved1;
  uint64_t nvmcap[2];
  uint8_t reserved2[40];
  uint8_t nguid[16];
  uint8_t eui64[8];
  NvmeLbaFormat lbaf[16];
  uint8_t reserved3[192];
  uint8_t vendor[3712];
} __attribute__((packed)) NvmeIdentifyNamespace;

typedef struct NvmeNamespace {
  uint32_t nsid;
  uint64_t block_count;
  uint32_t block_size;
  struct NvmeNamespace *next;
} NvmeNamespace;

typedef struct NvmeController {
  const PciDevice *pci_dev;
  NvmeRegisters *regs;
  uint64_t bar_phys;
  uint64_t bar_size;
  uint64_t cap;
  uint32_t vs;
  uint32_t dstrd;
  uint32_t max_queue_entries;
  NvmeQueue admin_queue;
  NvmeQueue io_queue;
  uint32_t timeout_ms;
  uint32_t max_transfer_bytes;
  NvmeNamespace *namespaces;
  uint8_t use_msix;
  struct NvmeController *next;
} NvmeController;




void nvme_init(void);
NvmeController *nvme_get_controllers(void);
int nvme_read(NvmeController *ctrl, uint32_t nsid, uint64_t lba, uint32_t count, void *buf);


