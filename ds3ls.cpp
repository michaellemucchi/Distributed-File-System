#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include <cstring>

#include "StringUtils.h"
#include "LocalFileSystem.h"
#include "Disk.h"
#include "ufs.h"

using namespace std;


// Use this function with std::sort for directory entries
bool compareByName(const dir_ent_t& a, const dir_ent_t& b) {
    return std::strcmp(a.name, b.name) < 0;
}


int main(int argc, char *argv[]) {
    if (argc != 3) {
        cerr << argv[0] << ": diskImageFile directory" << endl;
        cerr << "For example:" << endl;
        cerr << "    $ " << argv[0] << " tests/disk_images/a.img /a/b" << endl;
        return 1;
    }

    // parse command line arguments
    Disk *disk = new Disk(argv[1], UFS_BLOCK_SIZE);
    LocalFileSystem *fileSystem = new LocalFileSystem(disk);
    string path = string(argv[2]);

    // split the path into components 
    vector<string> components = StringUtils::split(path, '/');

    // start at the root directory
    int inodeNumber = UFS_ROOT_DIRECTORY_INODE_NUMBER;

    // traverse the path
    for (const string &component : components) {
        // lookup the next component in the path
        inodeNumber = fileSystem->lookup(inodeNumber, component);
        if (inodeNumber < 0) {
            cerr << "Directory not found" << endl;
            delete fileSystem;
            delete disk;
            return 1;
        }
    }

    // fetch the inode for the final component
    inode_t inode;
    if (fileSystem->stat(inodeNumber, &inode) != 0) {
        cerr << "Directory not found" << endl;
        delete fileSystem;
        delete disk;
        return 1;
    }

    // check if the inode is a directory or a file
    if (inode.type == UFS_DIRECTORY) { // directory case
        char *buffer = new char[inode.size];
        int bytesRead = fileSystem->read(inodeNumber, buffer, inode.size);
        if (bytesRead < 0) {
            cerr << "Directory not found" << endl;
            delete[] buffer;
            delete fileSystem;
            delete disk;
            return 1;
        }

        // recast cast the buffer to dir_ent_t array
        dir_ent_t *entries = reinterpret_cast<dir_ent_t *>(buffer);
        int numEntries = bytesRead / sizeof(dir_ent_t);

        // collect entries into a vector and sort by name
        vector<dir_ent_t> dirEntries(entries, entries + numEntries);
        sort(dirEntries.begin(), dirEntries.end(), compareByName);

        // print out the sorted directory entries
        for (const auto &entry : dirEntries) {
            cout << entry.inum << "\t" << entry.name << endl;
        }
        
        delete[] buffer;
    } else if (inode.type == UFS_REGULAR_FILE) { // file case
        cout << inodeNumber << "\t" << components.back() << endl;
    } else {
        // invalid inode type
        cerr << "Directory not found" << endl;
        delete fileSystem;
        delete disk;
        return 1;
    }

    // free dynamically allocated space
    delete fileSystem;
    delete disk;
    return 0;
}