/*************************************************************************
Copyright (c) 2012-2013 Miroslav Andel
All rights reserved.

For conditions of distribution and use, see copyright notice in sgct.h 
*************************************************************************/

#ifndef _SCREEN_CAPTURE_H_
#define _SCREEN_CAPTURE_H_

#include "Image.h"
#include <string>

namespace sgct_core
{

/*!
	This class is used internally by SGCT and is called when using the takeScreenshot function from the Engine.
	Screenshots are saved as PNG-files and and can also be used for movie recording.
*/
class ScreenCapture
{
public:
	//! The different capture enums used by the SaveScreenCapture function
	enum CaptureMode { FBO_Texture = 0, FBO_Left_Texture, FBO_Right_Texture, Front_Buffer, Left_Front_Buffer, Right_Front_Buffer };
	//! The different file formats supported
	enum CaptureFormat { NOT_SET = -1, PNG = 0, TGA };

	ScreenCapture();
	~ScreenCapture();

	void init();
	void initOrResize(int x, int y, int channels=4);
	void setFormat(CaptureFormat cf);
	CaptureFormat getFormat();
	void SaveScreenCapture(unsigned int textureId, int frameNumber, CaptureMode cm = FBO_Texture);
	void setUsePBO(bool state);

private: 
	void addFrameNumberToFilename( int frameNumber, CaptureMode cm = FBO_Texture );
	int getAvailibleCaptureThread(); 

	sgct_core::Image ** mframeBufferImagePtrs;
	int * mFrameCaptureThreads;
	unsigned int mNumberOfThreads;
	unsigned int mPBO;
	int mDataSize;
	int mX;
	int mY;
	int mChannels;

	std::string mScreenShotFilename;
	bool mUsePBO;
	CaptureFormat mFormat;
};

}

#endif