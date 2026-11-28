// Read-only HDFS calls for single-host MPI benchmarks on generated local files.
#pragma once
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
using tSize = int;
using tOffset = long long;
struct hdfsBuilder {};
using hdfsFS = hdfsBuilder *;
using hdfsFile = FILE *;
inline hdfsBuilder *hdfsNewBuilder() {
  static hdfsBuilder b;
  return &b;
}
inline void hdfsBuilderSetNameNode(...) {}
inline void hdfsBuilderSetNameNodePort(...) {}
inline hdfsFS hdfsBuilderConnect(hdfsBuilder *b) { return b; }
inline int hdfsDisconnect(...) { return 0; }
inline hdfsFile hdfsOpenFile(hdfsFS, const char *p, int flags, int, int, int) {
  if (flags & O_WRONLY)
    std::abort();
  return fopen(p, "rb");
}
inline int hdfsRead(hdfsFS, FILE *f, void *b, int n) { return fread(b, 1, n, f); }
inline int hdfsCloseFile(hdfsFS, FILE *f) { return fclose(f); }
inline int hdfsWrite(...) { std::abort(); }
inline int hdfsFlush(...) { std::abort(); }
inline int hdfsExists(hdfsFS, const char *p) { return access(p, F_OK); }
inline int hdfsDelete(...) { std::abort(); }
inline int hdfsCreateDirectory(...) { std::abort(); }
enum { kObjectKindFile };
struct hdfsFileInfo {
  char *mName;
  tOffset mSize;
  int mKind;
};

#include <dirent.h>
#include <string>
#include <vector>
inline hdfsFileInfo *hdfsListDirectory(hdfsFS, const char *p, int *n) {
  struct stat st;
  if (stat(p, &st))
    return nullptr;
  std::vector<std::string> files;
  if (S_ISDIR(st.st_mode)) {
    DIR *d = opendir(p);
    if (!d)
      return nullptr;
    while (auto *e = readdir(d)) {
      if (e->d_name[0] != '.')
        files.push_back(std::string(p) + "/" + e->d_name);
    }
    closedir(d);
  } else
    files.push_back(p);
  *n = files.size();
  auto *out = new hdfsFileInfo[*n];
  for (int i = 0; i < *n; ++i) {
    stat(files[i].c_str(), &st);
    out[i] = {strdup(files[i].c_str()), st.st_size, kObjectKindFile};
  }
  return out;
}
inline void hdfsFreeFileInfo(hdfsFileInfo *f, int n) {
  for (int i = 0; i < n; ++i)
    free(f[i].mName);
  delete[] f;
}
