#include <iostream>
#include <string>
#include <algorithm>
#include <cstring>

#include "LocalFileSystem.h"
#include "Disk.h"
#include "ufs.h"

using namespace std;


int main(int argc, char *argv[]) {
  if (argc != 2) {
    cerr << argv[0] << ": diskImageFile" << endl;
    return 1;
  }

  // Parse command line arguments
  Disk *disk = new Disk(argv[1], UFS_BLOCK_SIZE);
  LocalFileSystem *fileSystem = new LocalFileSystem(disk);
  
  // read the superblock
  super_t super;
  fileSystem->readSuperBlock(&super);

  // print out superblock information
  cout << "Super" << endl;
  cout << "inode_region_addr " << super.inode_region_addr << endl;
  cout << "inode_region_len " << super.inode_region_len << endl;
  cout << "num_inodes " << super.num_inodes << endl;
  cout << "data_region_addr " << super.data_region_addr << endl;
  cout << "data_region_len " << super.data_region_len << endl;
  cout << "num_data " << super.num_data << endl << endl;

  // read and print out the inode bitmap
  unsigned char *inodeBitmap = new unsigned char[super.inode_bitmap_len * UFS_BLOCK_SIZE];
  fileSystem->readInodeBitmap(&super, inodeBitmap);
  cout << "Inode bitmap" << endl;
  for (int i = 0; i < (super.num_inodes + 7)/ 8; i++) {
      cout << (unsigned int)inodeBitmap[i] << " ";
  }
  cout << endl << endl;

  // read and print out the data bitmap
  unsigned char *dataBitmap = new unsigned char[super.data_bitmap_len * UFS_BLOCK_SIZE];
  fileSystem->readDataBitmap(&super, dataBitmap);
  cout << "Data bitmap" << endl;
  for (int i = 0; i < (super.num_data + 7)/ 8; i++) {
      cout << (unsigned int)dataBitmap[i] << " ";
  }
  cout << endl;

  // clean up allocated memory
  delete[] inodeBitmap;
  delete[] dataBitmap;
  delete fileSystem;
  delete disk;
  
  return 0;
}
