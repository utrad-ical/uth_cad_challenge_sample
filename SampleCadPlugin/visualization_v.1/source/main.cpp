#pragma warning(disable:4996)

#include<stdio.h>

#include "LibCircusCS.h"

#include "visualization.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


int main(int argc, char *argv[])
{
	// Check the number of arguments
	if(argc < 4 || argc > 5) 
	{
		fprintf(stderr, "[Usage] visualization_v.1.exe jobRootPath seriesNum coreNum (environment)\n");	
		return -1;
	}

	char jobRootPath[1024];

	// Set job root path
	strcpy(jobRootPath, argv[1]);

	// Get the number of cores
	int coreNum = atoi(argv[3]);

	// Visualization (create MIP image)
	visualizationMain(jobRootPath, coreNum);

	return 0;
}
