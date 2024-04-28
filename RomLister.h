#ifndef ROMLISTER
#define ROMLISTER
#include <string>
#include <vector>
#include <libdragon.h>
#define ROMLISTER_MAXPATH 80
#define DIRECTORYDEPTH 10

namespace Frens {

	class RomLister
	{

	public:
		struct RomEntry {
			char Path[ROMLISTER_MAXPATH];  // Without dirname
			bool IsDirectory;
		};
		RomLister(  void *buffer, size_t buffersize);
		~RomLister();
		RomEntry* GetEntries();
		char  *FolderName();
		char *ParentFolderName();
		size_t Count();
		void list(const char *directoryName);

	private:
		char directoryname[MAX_FILENAME_LEN];
		char directories[DIRECTORYDEPTH][MAX_FILENAME_LEN];
		int directorydepth = 0;
		int length;
		size_t max_entries;
		RomEntry *entries;
		size_t numberOfEntries;

	};
}
#endif
