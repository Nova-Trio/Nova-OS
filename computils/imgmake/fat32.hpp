#pragma once
namespace FAT32 {

// Basic FAT32 partition architecture:
// 1. Reserved Region:
//   Sector 0: VBR
//   Sector 1: FSInfo
//   Sector 6: Backup VBR
//   Sector 7: Backup FSInfo
// FAT Region:
//   Sector IDK: FAT1 (File Allocation Table 1)
//   Sector IDK: FAT2 (optional backup of FAT1)
// Data Region (divided into clusters):
//   Cluster 2 (first available after 0 and 1 which aren't mapped): Root Directory
//   Other clusters: file data & subdirectories

}
