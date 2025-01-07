#include <iostream>
#include <string>
#include <vector>
#include <assert.h>
#include <cstring>

#include "LocalFileSystem.h"
#include "ufs.h"

using namespace std;


LocalFileSystem::LocalFileSystem(Disk *disk) {
  this->disk = disk;
}

void LocalFileSystem::readSuperBlock(super_t *super) {
  // allocate a buffer to read from disk
  unsigned char buffer[UFS_BLOCK_SIZE];
  
  // read the super block
  disk->readBlock(0, buffer);

  // copy buffer data into super block
  memcpy(super, buffer, sizeof(super_t));
}

void LocalFileSystem::readInodeBitmap(super_t *super, unsigned char *inodeBitmap) {
  int startBlock = super->inode_bitmap_addr;
  int numBlocks = super->inode_bitmap_len;

  // allocate a buffer to read from disk
  unsigned char buffer[UFS_BLOCK_SIZE];

  // read each block from disk into buffer, copy data to inode bitmap
  for (int i = 0; i < numBlocks; i++) {
    disk->readBlock(startBlock + i, buffer);     
    memcpy(inodeBitmap + (i * UFS_BLOCK_SIZE), buffer, UFS_BLOCK_SIZE);
  }
}

void LocalFileSystem::writeInodeBitmap(super_t *super, unsigned char *inodeBitmap) {
  int startBlock = super->inode_bitmap_addr;
  int numBlocks = super->inode_bitmap_len;

  // allocate buffer to write to disk
  unsigned char buffer[UFS_BLOCK_SIZE];

  // copy each block from inode bitmap into buffer, write back to disk
  for (int i = 0; i < numBlocks; i++) {
    memcpy(buffer, inodeBitmap + (i * UFS_BLOCK_SIZE), UFS_BLOCK_SIZE);
    disk->writeBlock(startBlock + i, buffer);     
  }
}


void LocalFileSystem::readDataBitmap(super_t *super, unsigned char *dataBitmap) {
  int startBlock = super->data_bitmap_addr;
  int numBlocks = super->data_bitmap_len;

  // allocate buffer to read from disk
  unsigned char buffer[UFS_BLOCK_SIZE];

  // read each block from disk into buffer, copy data to data bitmap
  for (int i = 0; i < numBlocks; i++) {
    disk->readBlock(startBlock + i, buffer);
    memcpy(dataBitmap + (i * UFS_BLOCK_SIZE), buffer, UFS_BLOCK_SIZE);
  }
}


void LocalFileSystem::writeDataBitmap(super_t *super, unsigned char *dataBitmap) {
  int startBlock = super->data_bitmap_addr;
  int numBlocks = super->data_bitmap_len;

  // allocate buffer to write to disk
  unsigned char buffer[UFS_BLOCK_SIZE];

  // copy each block from inode bitmap to buffer, write back to disk
  for (int i = 0; i < numBlocks; i++) {
    memcpy(buffer, dataBitmap + (i * UFS_BLOCK_SIZE), UFS_BLOCK_SIZE);
    disk->writeBlock(startBlock + i, buffer);
  }
}


void LocalFileSystem::readInodeRegion(super_t *super, inode_t *inodes) {
  int startBlock = super->inode_region_addr;
  int numBlocks = super->inode_region_len;

  // allocate buffer to read from disk
  unsigned char buffer[UFS_BLOCK_SIZE];

  // read each block from disk into buffer, copy into inodes list
  for (int i = 0; i < numBlocks; i++) {
    disk->readBlock(startBlock + i, buffer);
    memcpy(((unsigned char *) inodes) + (i * UFS_BLOCK_SIZE), buffer, UFS_BLOCK_SIZE);
  }
}


void LocalFileSystem::writeInodeRegion(super_t *super, inode_t *inodes) {
  int startBlock = super->inode_region_addr;
  int numBlocks = super->inode_region_len;

  // allocate buffer to write to disk
  unsigned char buffer[UFS_BLOCK_SIZE];

  // copy each block from inode list into  buffer, write back to disk
  for (int i = 0; i < numBlocks; i++) {
    memcpy(buffer, ((unsigned char *) inodes) + (i * UFS_BLOCK_SIZE), UFS_BLOCK_SIZE);
    disk->writeBlock(startBlock + i, buffer);
  }
}


int LocalFileSystem::lookup(int parentInodeNumber, std::string name) {
    // load superblock
    super_t super;
    readSuperBlock(&super);

    // validate parent inode number
    if (parentInodeNumber < 0 || parentInodeNumber >= super.num_inodes) {
        return -EINVALIDINODE;
    }

    // read in parent inode
    inode_t parentInode;
    stat(parentInodeNumber, &parentInode);

    // check if parent inode is a directory
    if (parentInode.type != UFS_DIRECTORY) {
        return -EINVALIDINODE;
    }

    // allocate buffer to read directory data
    unsigned char buffer[parentInode.size];

    // read in directory data to buffer
    int bytesRead = read(parentInodeNumber, buffer, parentInode.size);
    if (bytesRead < 0) {
      return -EINVALIDINODE; 
    }

    // recast buffer as an array of directory entries
    dir_ent_t *entries = reinterpret_cast<dir_ent_t *>(buffer);

    // count number of entries to loop through
    int numEntries = bytesRead / sizeof(dir_ent_t);

    // search for given name in directory entries
    for (int i = 0; i < numEntries; i++) {
      if (entries[i].inum >= 0 && strcmp(entries[i].name, name.c_str()) == 0) {
        return entries[i].inum; // inode name found, return number
      }
    }

    // inode name not found in directory
    return -ENOTFOUND;
}


int LocalFileSystem::stat(int inodeNumber, inode_t *inode) {
    // load superblock
    super_t super;
    readSuperBlock(&super);

    // validate inode number
    if (inodeNumber < 0 || inodeNumber >= super.num_inodes) {
        return -EINVALIDINODE;
    }

    // read inode region into a table
    inode_t *inodeTable = new inode_t[super.inode_region_len * UFS_BLOCK_SIZE];
    readInodeRegion(&super, inodeTable);

    // access requested inode and copy it over to pointer
    *inode = inodeTable[inodeNumber];

    // free dynamically allocated memory
    delete[] inodeTable; 

    return 0; 
}



int LocalFileSystem::read(int inodeNumber, void *buffer, int size) {
    // load superblock
    super_t super;
    readSuperBlock(&super);

    // validate inode number
    if (inodeNumber < 0 || inodeNumber >= super.num_inodes) {
      cerr << "Error reading file" << endl;
      return -EINVALIDINODE;
    }

    // read inode metadata
    inode_t inode;
    stat(inodeNumber, &inode);

    // validate inode size
    if (size < 0 || size > inode.size) {
      return -EINVALIDSIZE;
    }

    // read out block data
    int bytesRead = 0;
    for (int i = 0; i < DIRECT_PTRS && bytesRead < size; i++) {

      // read each block into intermediate buffer
      unsigned char intermed_buffer[UFS_BLOCK_SIZE];
      disk->readBlock(inode.direct[i], intermed_buffer);
      
      // calculate how much data to copy, if the current amount of bytes is less than the block size
      // only copy over that many bytes, else copy over the block size (4096)
      int bytesToCopy = (size - bytesRead < UFS_BLOCK_SIZE) ? (size - bytesRead) : UFS_BLOCK_SIZE;

      // copy data into buffer
      memcpy((unsigned char *)buffer + bytesRead, intermed_buffer, bytesToCopy);
      bytesRead += bytesToCopy;
    }
    
    // return number of bytes read
    return bytesRead; 
}



int LocalFileSystem::create(int parentInodeNumber, int type, string name) {
  // load super block
  super_t super;
  readSuperBlock(&super);


  // validate parent inode
  inode_t parentInode;
  if (stat(parentInodeNumber, &parentInode) != 0 || parentInode.type != UFS_DIRECTORY) {
    return -EINVALIDINODE;
  }
  

  // validate name
  if (name.size() > DIR_ENT_NAME_SIZE) {
    return -EINVALIDNAME;
  }


  // check if name already exists, make sure it has correct type
  int existing_inode_num = lookup(parentInodeNumber, name);
  if (existing_inode_num > 0) {
    inode_t inode_entry;
    stat(existing_inode_num, &inode_entry);
    if (inode_entry.type == type) {
        return existing_inode_num; // already exists with the same type
    } else {
        return -EINVALIDTYPE;
    }
  }

  // read through inode bitmap, check for free space
  unsigned char inodeBitmap[super.inode_bitmap_len * UFS_BLOCK_SIZE];
  readInodeBitmap(&super, inodeBitmap);

  // read through data bitmap, see if there is space (directories)
  unsigned char dataBitmap[super.data_bitmap_len * UFS_BLOCK_SIZE];
  readDataBitmap(&super, dataBitmap);
  

  // initialize variable to store new inode number
  int newInodeNum = -1;
  // iterate through inode bitmap
  for (int inodeIndex = 0; inodeIndex < super.num_inodes; inodeIndex++) {
      int byteIndex = inodeIndex / 8;
      int bitIndex = inodeIndex % 8; 

      // check if this index is free in our inode bitmap
      if (!(inodeBitmap[byteIndex] & (1 << bitIndex))) { 
          // free inode found
          newInodeNum = inodeIndex; 
          // update inode bitmap
          inodeBitmap[newInodeNum / 8] |= (1 << (newInodeNum % 8));
          break;
      }
  }
  // no free inode available
  if (newInodeNum == -1) {
      return -ENOTENOUGHSPACE; 
  }

  
  // ensure enough space in parent directory for new entry
  int entriesPerBlock = UFS_BLOCK_SIZE / sizeof(dir_ent_t);
  int currentEntries = parentInode.size / sizeof(dir_ent_t);

  // initialize variable to store new block number
  int newBlockNum = -1;

  // check if parent directory has space in existing blocks
  if (currentEntries % entriesPerBlock == 0) { // need to allocate a new block
    // find free block in data bitmap
    for (int blockIndex = 0; blockIndex < super.num_data; blockIndex++) {
        int byteIndex = blockIndex / 8;
        int bitIndex = blockIndex % 8;
        if (!(dataBitmap[byteIndex] & (1 << bitIndex))) { 
            newBlockNum = blockIndex; // free block found
            dataBitmap[newBlockNum / 8] |= (1 << newBlockNum % 8);
            break;
        }
    }
    // no free block available
    if (newBlockNum == -1) {
        return -ENOTENOUGHSPACE; // no space in data region
    }

    // update parent inode with new block
    int parent_blocks = parentInode.size / UFS_BLOCK_SIZE;
    if ((parentInode.size % UFS_BLOCK_SIZE) != 0) {
      parent_blocks += 1;
    }
    if (parent_blocks >= DIRECT_PTRS) {
        return -ENOTENOUGHSPACE;
    }
    parentInode.direct[parent_blocks] = newBlockNum + super.data_region_addr;

  }
  


  // create the new inode
  inode_t newInode;
  newInode.type = type;
  newInode.size = 0; // default inode size

  // if inode is a directory, allocate a data block for entries "." and ".."
  int dirBlockNum = -1;
  if (type == UFS_DIRECTORY) {
      for (int blockIndex = 0; blockIndex < super.num_data; blockIndex++) {
          int byteIndex = blockIndex / 8;
          int bitIndex = blockIndex % 8;
          if (!(dataBitmap[byteIndex] & (1 << bitIndex))) {
              dirBlockNum = blockIndex;
              newInode.direct[0] = dirBlockNum + super.data_region_addr;
              dataBitmap[dirBlockNum / 8] |= (1 << (dirBlockNum % 8));
              break;
          }
      }
      if (dirBlockNum == -1) {
          return -ENOTENOUGHSPACE; // no space for new directory block
      }


      // fil out new directory metadata
      newInode.size = 2 * sizeof(dir_ent_t); // "." and ".."

      // copy over both entries to its new datablock
      dir_ent_t dirEntries[2] = {};
      strncpy(dirEntries[0].name, ".", DIR_ENT_NAME_SIZE);
      dirEntries[0].inum = newInodeNum;

      strncpy(dirEntries[1].name, "..", DIR_ENT_NAME_SIZE);
      dirEntries[1].inum = parentInodeNumber;

      // fill in buffer and write this block to disk
      unsigned char dirBlock[UFS_BLOCK_SIZE];
      memcpy(dirBlock, dirEntries, sizeof(dirEntries));
      disk->writeBlock(newInode.direct[0] , dirBlock);

  }


  // update parent directory with new entry
  dir_ent_t newEntry;
  strncpy(newEntry.name, name.c_str(), DIR_ENT_NAME_SIZE);
  newEntry.inum = newInodeNum;

  // write new entry to parent directory
  unsigned char buffer[parentInode.size];
  int bytesRead = read(parentInodeNumber, buffer, parentInode.size);

  unsigned char write_buffer[bytesRead + sizeof(dir_ent_t)];
  // copy over the initial contents of parentInode
  memcpy(write_buffer, buffer, bytesRead);
  // copy over new entry
  memcpy(write_buffer+bytesRead, &newEntry, sizeof(dir_ent_t));

  // increment parent inode byte size
  parentInode.size += sizeof(dir_ent_t);

  // refresh all data in parent inodes data blocks
  int blocks = parentInode.size / UFS_BLOCK_SIZE;
  if ((parentInode.size % UFS_BLOCK_SIZE) != 0) {
    blocks += 1;
  }
  for (int i = 0; i < blocks; i++) {
    unsigned char tempBuffer[UFS_BLOCK_SIZE];
    int startOffset = i * UFS_BLOCK_SIZE;
    int bytesToWrite = min(UFS_BLOCK_SIZE, parentInode.size - startOffset);
    memcpy(tempBuffer, write_buffer + startOffset, bytesToWrite);

    disk->writeBlock(parentInode.direct[i], tempBuffer);
  }
  
  // write new inode, updated parent inode meta data back to disk
  inode_t *inodeRegion = new inode_t[super.inode_region_len * UFS_BLOCK_SIZE];
  readInodeRegion(&super, inodeRegion);
  inodeRegion[newInodeNum] = newInode;
  inodeRegion[parentInodeNumber] = parentInode;
  writeInodeRegion(&super, inodeRegion);
  delete[] inodeRegion;

  // after all updates, writeback to both bitmaps to preserve state
  writeInodeBitmap(&super, inodeBitmap);
  writeDataBitmap(&super, dataBitmap);

  return newInodeNum; // return inode number of new entry
}


int LocalFileSystem::write(int inodeNumber, const void *buffer, int size) {
  // load super block
  super_t super;
  readSuperBlock(&super);

  // validate parent inode
  inode_t inode;
  if (stat(inodeNumber, &inode) != 0) {
    return -EINVALIDINODE;
  }

  // make sure inode is a file
  if (inode.type != UFS_REGULAR_FILE) {
    return -EINVALIDTYPE;
  }

  // validate size of write
  if (size < 0 || size > MAX_FILE_SIZE) {
    return -EINVALIDSIZE;
  }

  // blocks in inode.size
  int curr_inode_blocks = inode.size / UFS_BLOCK_SIZE;
  if ((inode.size % UFS_BLOCK_SIZE) != 0) {
    curr_inode_blocks += 1;
  }

  // blocks needed for write
  int blocks_to_write = size / UFS_BLOCK_SIZE;
  if ((size % UFS_BLOCK_SIZE) != 0) {
    blocks_to_write += 1;
  }

  // read through data bitmap, see if there is space
  unsigned char dataBitmap[super.data_bitmap_len * UFS_BLOCK_SIZE];
  readDataBitmap(&super, dataBitmap);

  // free data blocks that are no longer needed
  if(blocks_to_write < curr_inode_blocks){
    for(int i = 0; i < curr_inode_blocks; i++){
      if(i < blocks_to_write){
        continue;
      }
      int current_block = inode.direct[i] - super.data_region_addr;
      dataBitmap[current_block / 8] &= ~(1 << (current_block % 8));
    }
    writeDataBitmap(&super, dataBitmap);
  }

  // check if can add blocks to our inode
  if (blocks_to_write > curr_inode_blocks) {
    for (int i = curr_inode_blocks; i < blocks_to_write; i++) {
      int newDataBlock = -1;
      for (int dataIndex = 0; dataIndex < super.num_data; dataIndex++) {
          int byteIndex = dataIndex / 8;
          int bitIndex = dataIndex % 8; 

          // check if the dataIndex is free in databitmap
          if (!(dataBitmap[byteIndex] & (1 << bitIndex))) { 
              // free datablock found
              newDataBlock = dataIndex; 
              // update databitmap
              dataBitmap[newDataBlock / 8] |= (1 << (newDataBlock % 8));
              inode.direct[i] = newDataBlock + super.data_region_addr;
              break;
          }
      }
      // breakout and relabel copy size
      if (newDataBlock == -1) {
        size = i * UFS_BLOCK_SIZE;
        blocks_to_write = i;
        break;
      }
    }
    // write back data bitmap 
    writeDataBitmap(&super, dataBitmap);
  }
  
  // update our inode size
  inode.size = size;

  // rewrite all necessary data, update inodes direct pointers
  for (int i = 0; i < blocks_to_write; i++) {
    // buffer to write back
    unsigned char write_buffer[UFS_BLOCK_SIZE];
    memcpy(write_buffer, (const unsigned char*)buffer + (i * UFS_BLOCK_SIZE), UFS_BLOCK_SIZE);
    disk->writeBlock(inode.direct[i], write_buffer);
  }

  // writeback inode to inode region
  inode_t *inodeRegion = new inode_t[super.inode_region_len * UFS_BLOCK_SIZE];
  readInodeRegion(&super, inodeRegion);
  inodeRegion[inodeNumber] = inode;
  writeInodeRegion(&super, inodeRegion);
  delete[] inodeRegion;

  return inode.size;
}




int LocalFileSystem::unlink(int parentInodeNumber, string name) {
  // load in superblock
  super_t super;
  readSuperBlock(&super);

  // validate parent inode
  inode_t parentInode;
  if (stat(parentInodeNumber, &parentInode) != 0) {
    return -EINVALIDINODE;
  }
  
  // parent inode must be a directory
  if (parentInode.type != UFS_DIRECTORY) {
    return -EINVALIDINODE;
  }
  
  // check for invalid name
  if (name.size() > DIR_ENT_NAME_SIZE) {
    return -EINVALIDNAME;
  }

  // check if name == "." or ".."
  if (name == "." || name == "..") {
    return -EUNLINKNOTALLOWED;
  }

  int entry_to_delete = lookup(parentInodeNumber, name);
  if (entry_to_delete < 0) { // not an error by definition
    return 0;
  }
  
  // find entry, delete its contents from file system
  inode_t inode_to_del;
  stat(entry_to_delete, &inode_to_del);
  

  // check for size of directory
  int empty_dir_size = 2 * sizeof(dir_ent_t);
  if (inode_to_del.type == UFS_DIRECTORY && (inode_to_del.size > empty_dir_size)) {
    return -EDIRNOTEMPTY;
  }

  // clear corresponding bit from inode bitmap
  unsigned char inodeBitmap[super.inode_bitmap_len * UFS_BLOCK_SIZE];
  readInodeBitmap(&super, inodeBitmap);
  inodeBitmap[entry_to_delete / 8] &= ~(1 << (entry_to_delete % 8));

  // original blocks to write
  int origBlockCount = parentInode.size / UFS_BLOCK_SIZE;
  if (parentInode.size % UFS_BLOCK_SIZE != 0) {
      origBlockCount += 1;
  }

  // remove entry from parent directory
  unsigned char buffer[parentInode.size];
  read(parentInodeNumber, buffer, parentInode.size);

  int numEntries = parentInode.size / sizeof(dir_ent_t);
  dir_ent_t entries[numEntries];
  memcpy(entries, buffer, parentInode.size);

  // remove entry from the list of entries
  for (int i = 2; i < numEntries; i++) {
      if (entries[i].name == name) {
          swap(entries[i], entries[numEntries - 1]);
          break;
      }
  }
  parentInode.size -= sizeof(dir_ent_t);

  memcpy(buffer, entries, parentInode.size);
  

  // write back to parentInode data blocks
  int num_blocks = parentInode.size / UFS_BLOCK_SIZE;
  int offset = parentInode.size % UFS_BLOCK_SIZE;
  if(offset > 0){
    num_blocks += 1;
  }

  for (int i = 0; i < num_blocks; i++) {
    unsigned char tempBuffer[UFS_BLOCK_SIZE];
    int startOffset = i * UFS_BLOCK_SIZE;
    int bytesToWrite = min(UFS_BLOCK_SIZE, parentInode.size - startOffset);

    memcpy(tempBuffer, buffer + startOffset, bytesToWrite);

    disk->writeBlock(parentInode.direct[i], tempBuffer);
  }


  // delete data blocks allocated
  unsigned char dataBitmap[super.data_bitmap_len * UFS_BLOCK_SIZE];
  readDataBitmap(&super, dataBitmap);

  // remove extra allocated data block for parent
  if (num_blocks < origBlockCount) {
      int blockNumber = parentInode.direct[num_blocks] - super.data_region_addr;
      dataBitmap[blockNumber / 8] &= ~(1 << (blockNumber % 8));
  }

  
  // free all data blocks originally allocated
  int blocks = inode_to_del.size / UFS_BLOCK_SIZE;
  if ((inode_to_del.size % UFS_BLOCK_SIZE) != 0) {
    blocks += 1;
  }    
  for (int i = 0; i < blocks; i++) {
      int blockNumber = inode_to_del.direct[i] - super.data_region_addr;
      dataBitmap[blockNumber / 8] &= ~(1 << (blockNumber % 8));
  }

  writeDataBitmap(&super, dataBitmap);
  writeInodeBitmap(&super, inodeBitmap);

  inode_to_del.type = 0;
  inode_to_del.size = 0;
  // write updated parent inode meta data to inodeRegion
  inode_t *inodeRegion = new inode_t[super.inode_region_len * UFS_BLOCK_SIZE];
  readInodeRegion(&super, inodeRegion);
  inodeRegion[entry_to_delete] = inode_to_del;
  inodeRegion[parentInodeNumber] = parentInode;
  writeInodeRegion(&super, inodeRegion);
  delete[] inodeRegion;

  return 0;
}
