#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sstream>
#include <iostream>
#include <map>
#include <string>
#include <algorithm>

#include "DistributedFileSystemService.h"
#include "ClientError.h"
#include "ufs.h"
#include "WwwFormEncodedDict.h"

using namespace std;

// constructor
DistributedFileSystemService::DistributedFileSystemService(string diskFile)
    : HttpService("/ds3/") {
  this->fileSystem = new LocalFileSystem(new Disk(diskFile, UFS_BLOCK_SIZE));
}

// GET Method - read files or list directory
void DistributedFileSystemService::get(HTTPRequest *request, HTTPResponse *response) {
  string path = request->getPath();
  path = path.substr(5); // remove /ds3/

  // remove trailing slash for consistency
  if (!path.empty() && path.back() == '/') {
    path.pop_back();
  }

  try {
    vector<string> tokens;
    stringstream ss(path);
    string item;

    // tokenize path into components
    while (getline(ss, item, '/')) {
      if (!item.empty()) tokens.push_back(item);
    }

    int parent = 0; // start at root inode

    // traverse path step-by-step
    for (size_t i = 0; i < tokens.size(); i++) {
      int inode = fileSystem->lookup(parent, tokens[i]);
      if (inode < 0) {
        throw ClientError::notFound();
      }

      // update parent inode for next lookup
      parent = inode;
    }

    // get metadata for final inode
    inode_t inodeData;
    fileSystem->stat(parent, &inodeData);

    // handle directories
    if (inodeData.type == UFS_DIRECTORY) {
      // read directory contents
      unsigned char buffer[inodeData.size];
      int bytesRead = fileSystem->read(parent, buffer, inodeData.size);
      if (bytesRead < 0) {
        throw ClientError::notFound();
      }

      // parse directory entries
      stringstream result;
      dir_ent_t *entries = reinterpret_cast<dir_ent_t *>(buffer);
      int numEntries = inodeData.size / sizeof(dir_ent_t);

      vector<string> dirEntries;
      for (int i = 0; i < numEntries; i++) {
        if (entries[i].inum >= 0) { // valid entry
          string name = string(entries[i].name);
          inode_t entryInode;
          fileSystem->stat(entries[i].inum, &entryInode);
          if (entryInode.type == UFS_DIRECTORY) {
            name += "/"; // append '/' for directories
          }
          dirEntries.push_back(name);
        }
      }

      // sort directory entries
      sort(dirEntries.begin(), dirEntries.end());
      for (const auto &entry : dirEntries) {
        result << entry << "\n";
      }

      response->setBody(result.str());
    } else {
      // handle files
      unsigned char buffer[inodeData.size];
      int bytesRead = fileSystem->read(parent, buffer, inodeData.size);
      if (bytesRead < 0) {
        throw ClientError::notFound();
      }

      // send file content
      response->setBody(string(reinterpret_cast<char *>(buffer), inodeData.size));
    }
  } catch (const ClientError &e) {
    throw; // rethrow known errors
  } catch (...) {
    throw ClientError::notFound(); // catch other errors
  }
}


// PUT Method - create/update files
void DistributedFileSystemService::put(HTTPRequest *request, HTTPResponse *response) {
  string path = request->getPath();
  path = path.substr(5); // remove /ds3/
  string body = request->getBody(); // file contents

  try {
    // begin transaction
    Disk *disk = fileSystem->disk; 
    disk->beginTransaction();

    // tokenize path
    vector<string> tokens;
    stringstream ss(path);
    string item;
    while (getline(ss, item, '/')) {
      if (!item.empty()) tokens.push_back(item);
    }

    int parent = 0; // start at root directory
    bool isDirectory = path.back() == '/'; // check if the target is a directory

    // traverse intermediate directories
    for (size_t i = 0; i < tokens.size() - 1; i++) {
      int inode = fileSystem->lookup(parent, tokens[i]);
      if (inode < 0) {
        parent = fileSystem->create(parent, UFS_DIRECTORY, tokens[i]);
      } else {
        inode_t temp;
        fileSystem->stat(inode, &temp);
        if (temp.type != UFS_DIRECTORY) {
          disk->rollback(); // rollback if conflict
          throw ClientError::conflict();
        }
        parent = inode;
      }
    }

    // final component in path
    string name = tokens.back();
    int fileInode = fileSystem->lookup(parent, name);

    if (fileInode < 0) {
      // create a new file if it doesn't exist
      if (isDirectory) {
        fileInode = fileSystem->create(parent, UFS_DIRECTORY, name);
      } else {
        fileInode = fileSystem->create(parent, UFS_REGULAR_FILE, name);
      }
    }

    // check if it's a file and overwrite its contents
    inode_t temp;
    fileSystem->stat(fileInode, &temp);

    if (temp.type == UFS_REGULAR_FILE) {
      // clear any existing content and write new data
      fileSystem->write(fileInode, body.c_str(), body.size());
    } else if (temp.type == UFS_DIRECTORY && !isDirectory) {
      disk->rollback(); // conflict: trying to write to a directory
      throw ClientError::conflict();
    }

    // commit transaction if successful
    disk->commit();
    response->setStatus(200);

  } catch (...) {
    // rollback on any failure
    fileSystem->disk->rollback();
    throw ClientError::badRequest();
  }
}



// DELETE Method - delete files or directories
void DistributedFileSystemService::del(HTTPRequest *request, HTTPResponse *response) {
  string path = request->getPath(); // get path from request
  path = path.substr(5);            // remove "/ds3/"

  try {
    // begin transaction
    Disk *disk = fileSystem->disk;
    disk->beginTransaction();

    // handle trailing slash in paths
    if (!path.empty() && path.back() == '/') {
      path.pop_back(); // remove trailing slash
    }

    // split path into parent and name
    size_t pos = path.find_last_of('/');
    string parentPath = (pos == string::npos) ? "" : path.substr(0, pos);
    string name = (pos == string::npos) ? path : path.substr(pos + 1);

    // lookup parent inode
    int parentInode = (parentPath.empty()) ? 0 : fileSystem->lookup(0, parentPath);
    if (parentInode < 0) {
      throw ClientError::notFound(); // parent directory not found
    }

    // lookup target inode
    int targetInode = fileSystem->lookup(parentInode, name);
    if (targetInode < 0) {
      throw ClientError::notFound(); // target not found
    }

    // check if directory is non-empty
    inode_t targetInodeData;
    fileSystem->stat(targetInode, &targetInodeData);

    if (targetInodeData.type == UFS_DIRECTORY && 
        targetInodeData.size > static_cast<int>(2 * sizeof(dir_ent_t))) {
          throw ClientError::conflict(); // directory not empty
    }


    // perform Unlink
    if (fileSystem->unlink(parentInode, name) < 0) {
      throw ClientError::notFound(); // unlink failed
    }

    // commit Transaction
    disk->commit();
    response->setStatus(200); // success
  }
  catch (const ClientError &e) {
    fileSystem->disk->rollback();
    throw; // rethrow known errors
  }
  catch (const exception &e) {
    fileSystem->disk->rollback();
    throw ClientError::badRequest(); // catch unexpected errors
  }
}
