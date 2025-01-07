#include <iostream>
#include <string>

#include <fcntl.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>

#include "LocalFileSystem.h"
#include "Disk.h"
#include "ufs.h"

using namespace std;

int main(int argc, char *argv[]) {
  if (argc != 4) {
    cerr << argv[0] << ": diskImageFile src_file dst_inode" << endl;
    cerr << "For example:" << endl;
    cerr << "    $ " << argv[0] << " tests/disk_images/a.img dthread.cpp 3" << endl;
    return 1;
  }

  // Parse command line arguments
  Disk *disk = new Disk(argv[1], UFS_BLOCK_SIZE);
  LocalFileSystem *fileSystem = new LocalFileSystem(disk);
  string srcFile = string(argv[2]);
  int dstInode = stoi(argv[3]);

  // first lets open the file
  int srcFd = open(srcFile.c_str(), O_RDONLY);
  if (srcFd < 0) {
      cerr << "Could not open source file" << endl;
      delete disk;
      delete fileSystem;
      return 1;
  }

  char buffer[MAX_FILE_SIZE];
  int bytesRead = read(srcFd, buffer, sizeof(buffer));
  if (bytesRead < 0) {
      cerr << "Could not read source file" << endl;
      delete disk;
      delete fileSystem;
      close(srcFd);
      return 1;
  }

  if (fileSystem->write(dstInode, buffer, bytesRead) < 0) {
      cerr << "Could not write to dst_file" << endl;
      delete disk;
      delete fileSystem;
      close(srcFd);
      return 1;
  } 
  
  delete disk;
  delete fileSystem;
  close(srcFd);
  return 0;
}
