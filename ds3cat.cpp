#include <iostream>
#include <string>
#include <algorithm>
#include <cstring>
#include <unistd.h>

#include "LocalFileSystem.h"
#include "Disk.h"
#include "ufs.h"

using namespace std;


int main(int argc, char *argv[]) {
  if (argc != 3) {
    cerr << argv[0] << ": diskImageFile inodeNumber" << endl;
    return 1;
  }

  // parse command line arguments
  Disk *disk = new Disk(argv[1], UFS_BLOCK_SIZE);
  LocalFileSystem *fileSystem = new LocalFileSystem(disk);
  int inodeNumber = stoi(argv[2]);

  // read the superblock
  super_t super;
  fileSystem->readSuperBlock(&super);

  // fetch inode information
  inode_t inode;
  if (fileSystem->stat(inodeNumber, &inode) != 0) {
      cerr << "Error reading file" << endl;
      delete fileSystem;
      delete disk;
      return 1;
  }

  // check if given inode is a directory
  if (inode.type == UFS_DIRECTORY) {
      cerr << "Error reading file" << endl;
      delete fileSystem;
      delete disk;
      return 1;
  }

  // print out file blocks
  int blocks = inode.size / UFS_BLOCK_SIZE;
  if ((inode.size % UFS_BLOCK_SIZE) != 0) {
    blocks += 1;
  }

  cout << "File blocks" << endl;
  for (int i = 0; i < blocks; ++i) {
    // stop if we reach 0 or past
    cout << inode.direct[i] << endl;
  }
  cout << endl;

  // print out file data
  cout << "File data" << endl;
  char *buffer = new char[inode.size];
  int bytesRead = fileSystem->read(inodeNumber, buffer, inode.size);

  // check if file read was successful
  if (bytesRead < 0) {
      cerr << "Error reading file" << endl;
      delete[] buffer;
      delete fileSystem;
      delete disk;
      return 1;
  }
  
  // write to standard out
  write(STDOUT_FILENO, buffer, bytesRead);

  // free dynamically allocated memory
  delete[] buffer;
  delete fileSystem;
  delete disk;
  
  return 0;
}
