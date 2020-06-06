#ifndef __FILEBROWSER__
#define __FILEBROWSER__


typedef struct
{
	char**	files;
	int	fileNum;

	char**	directories;
	int	dirNum;

	char*	currentDir;
} dirList_t;

int cmpstringp(const void *p1, const void *p2);

int getNumberFiles(void);

int getDir(dirList_t* dirList);

int listDir(int from, int max, int select, dirList_t dirList);

#endif
