/*************************************************************************
Copyright (c) 2012 Miroslav Andel
All rights reserved.

For conditions of distribution and use, see copyright notice in sgct.h 
*************************************************************************/

#ifndef _IMAGE_H_
#define _IMAGE_H_

namespace sgct_core
{

class Image
{
public:
	Image();
	bool load(const char * filename);
	bool loadPNG(const char * filename);
	bool savePNG(const char * filename, int compressionLevel = -1);
	bool savePNG(int compressionLevel = -1);
	void setFilename(const char * filename);
	void cleanup(bool releaseMemory = true);
	unsigned char * getData();
	int getChannels();
	int getSizeX();
	int getSizeY();
	void setDataPtr(unsigned char * dPtr);
	void setSize(int width, int height);
	void setChannels(int channels);
	void allocateOrResizeData();
	inline const char * getFilename() { return mFilename; }

private:
	int mChannels;
	int mSize_x;
	int mSize_y;
	char * mFilename;
	unsigned char * mData;
};

}

#endif

