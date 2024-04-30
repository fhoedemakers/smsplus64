#include <stdio.h>
#include <string.h>
#include "libdragon.h"
#include "RomLister.h"
#include "FrensHelpers.h"

// class to listing directories and .NES files on sd card
namespace Frens
{
	// Buffer must have sufficient bytes to contain directory contents
	RomLister::RomLister(void *buffer, size_t buffersize)
	{
		entries = (RomEntry *)buffer;
		max_entries = buffersize / sizeof(RomEntry);
	}

	RomLister::~RomLister()
	{
	}

	RomLister::RomEntry *RomLister::GetEntries()
	{
		return entries;
	}

	char *RomLister::FolderName()
	{
		return directoryname;
	}

	size_t RomLister::Count()
	{
		return numberOfEntries;
	}

	void RomLister::list(const char *directoryName)
	{
		RomEntry tempEntry;
		dir_t dir;
		debugf("Listing %s\n", directoryName);
		int err = dir_findfirst(directoryName, &dir);
		numberOfEntries = 0;
		while (err == 0)
		{
			if (strlen(dir.d_name) < ROMLISTER_MAXPATH)
			{
				struct RomEntry romInfo;
				strcpy(romInfo.Path, dir.d_name);
				romInfo.IsDirectory = (dir.d_type == DT_DIR);
				if (!romInfo.IsDirectory && (Frens::cstr_endswith(romInfo.Path, ".sms") || Frens::cstr_endswith(romInfo.Path, ".gg")))
				{
					entries[numberOfEntries++] = romInfo;
				}
				else
				{
					if (romInfo.IsDirectory && strcmp(romInfo.Path, "System Volume Information") != 0 && strcmp(romInfo.Path, "SAVES") != 0 && strcmp(romInfo.Path, "EDFC") != 0)
					{
						entries[numberOfEntries++] = romInfo;
					}
				}
			}
			err = dir_findnext(directoryName, &dir);
		}
		if (numberOfEntries == 0)
		{
			debugf("No files in this dir...\n");
		}

		// (bubble) Sort
		if (numberOfEntries > 1)
		{
			debugf("Sorting %d entries\n", numberOfEntries);
			for (int pass = 0; pass < numberOfEntries - 1; ++pass)
			{
				for (int j = 0; j < numberOfEntries - 1 - pass; ++j)
				{
					int result = 0;
					// Directories first in the list
					if (entries[j].IsDirectory && entries[j + 1].IsDirectory == false)
					{
						continue;
					}
					bool swap = false;
					if (entries[j].IsDirectory == false && entries[j + 1].IsDirectory)
					{
						swap = true;
					}
					else
					{
						result = strcasecmp(entries[j].Path, entries[j + 1].Path);
					}
					if (swap || result > 0)
					{
						tempEntry = entries[j];
						entries[j] = entries[j + 1];
						entries[j + 1] = tempEntry;
					}
				}
			}
			debugf("Sort done\n");
		}
	}
}
