/*************************************************************************
Copyright (c) 2012 Miroslav Andel, Link�ping University.
All rights reserved.
 
Original Authors:
Miroslav Andel, Alexander Fridlund

For any questions or information about the SGCT project please contact: miroslav.andel@liu.se

This work is licensed under the Creative Commons Attribution-ShareAlike 3.0 Unported License.
To view a copy of this license, visit http://creativecommons.org/licenses/by-sa/3.0/ or send a letter to
Creative Commons, 444 Castro Street, Suite 900, Mountain View, California, 94041, USA.
 
THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
OF THE POSSIBILITY OF SUCH DAMAGE.
*************************************************************************/

#include "../include/vrpn/vrpn_Tracker.h"
#include "../include/vrpn/vrpn_Button.h"
#include "../include/vrpn/vrpn_Analog.h"
#include <GL/glfw.h>
#include "../include/sgct/SGCTTrackingManager.h"
#include "../include/sgct/ClusterManager.h"
#include "../include/sgct/MessageHandler.h"
#include "../include/sgct/Engine.h"

#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/euler_angles.hpp>

std::vector<vrpn_Tracker_Remote *> mTrackers;
std::vector<vrpn_Analog_Remote *> mAnalogDevices;
std::vector<vrpn_Button_Remote *> mButtonDevices;

void VRPN_CALLBACK update_tracker_cb(void *userdata, const vrpn_TRACKERCB t );
void VRPN_CALLBACK update_button_cb(void *userdata, const vrpn_BUTTONCB b );
void VRPN_CALLBACK update_analog_cb(void * userdata, const vrpn_ANALOGCB a );

void GLFWCALL samplingLoop(void *arg);
GLFWmutex mTimingMutex = NULL;

core_sgct::SGCTTrackingManager::SGCTTrackingManager()
{
	mHeadSensorIndex = -1;
	mSamplingThreadId = -1;
	mXform = glm::mat4(1.0);
	mOffset = glm::vec3(0.0);
	mSamplingTime = 0.0;

	mTimingMutex = sgct::Engine::createMutex();
	if( mTimingMutex == NULL )
	{
		sgct::MessageHandler::Instance()->print("Tracking manager: Failed to create mutex!\n");
	}
}

core_sgct::SGCTTrackingManager::~SGCTTrackingManager()
{
	//destroy thread
	if( mSamplingThreadId != -1 )
	{
		glfwDestroyThread(mSamplingThreadId);
		mSamplingThreadId = -1;
	}

	//destroy mutex
	if( mTimingMutex != NULL )
	{
		sgct::Engine::destroyMutex(mTimingMutex);
		mTimingMutex = NULL;
	}
	
	//delete all instances
	for(size_t i=0; i<mTrackingDevices.size(); i++)
	{
		if( mTrackers[i] != NULL )
		{
			delete mTrackers[i];
			mTrackers[i] = NULL;
		}

		if( mAnalogDevices[i] != NULL )
		{
			delete mAnalogDevices[i];
			mAnalogDevices[i] = NULL;
		}

		if( mButtonDevices[i] != NULL )
		{
			delete mButtonDevices[i];
			mButtonDevices[i] = NULL;
		}
		
		if( mTrackingDevices[i] != NULL )
		{
			delete mTrackingDevices[i];
			mTrackingDevices[i] = NULL;
		}
	}

	mTrackers.clear();
	mAnalogDevices.clear();
	mButtonDevices.clear();
	mTrackingDevices.clear();
}

void core_sgct::SGCTTrackingManager::startSampling()
{
	if( !mTrackingDevices.empty() )
	{
		mSamplingThreadId = glfwCreateThread( samplingLoop, this );
		if( mSamplingThreadId < 0)
		{
			sgct::MessageHandler::Instance()->print("Tracking: Failed to start thread!\n");
		}
	}
}

void core_sgct::SGCTTrackingManager::updateTrackingDevices()
{
	for(size_t i=0; i<mTrackingDevices.size(); i++)
	{
		if( mTrackingDevices[i]->isEnabled() && mHeadSensorIndex == i)
		{
			core_sgct::ClusterManager * cm = core_sgct::ClusterManager::Instance();

			//set head rot & pos
			cm->getUserPtr()->setOrientationAndPosition( 
				glm::dmat3( mTrackingDevices[i]->getRotationMat() ),
				glm::vec3(mTrackingDevices[i]->getPosition()) );
		}
	}
}

void GLFWCALL samplingLoop(void *arg)
{
	core_sgct::SGCTTrackingManager * tmPtr = (core_sgct::SGCTTrackingManager *)arg;

	double t;

	while(true)
	{
		t = sgct::Engine::getTime();
		for(size_t i=0; i<tmPtr->getNumberOfDevices(); i++)
		{
			if( tmPtr->getTrackingPtr(i)->isEnabled() )
			{
				if( mTrackers[i] != NULL )
					mTrackers[i]->mainloop();

				if( mAnalogDevices[i] != NULL )
					mAnalogDevices[i]->mainloop();

				if( mButtonDevices[i] != NULL )
					mButtonDevices[i]->mainloop();
			}
		}

		tmPtr->setSamplingTime(sgct::Engine::getTime() - t);
	}
}

void core_sgct::SGCTTrackingManager::setSamplingTime(double t)
{
	sgct::Engine::lockMutex( mTimingMutex );
		mSamplingTime = t;
	sgct::Engine::unlockMutex( mTimingMutex );
}

double core_sgct::SGCTTrackingManager::getSamplingTime()
{
	double tmpVal;
	sgct::Engine::lockMutex( mTimingMutex );
		tmpVal = mSamplingTime;
	sgct::Engine::unlockMutex( mTimingMutex );

	return tmpVal;
}

void core_sgct::SGCTTrackingManager::setEnabled(bool state)
{
	for(size_t i=0; i<mTrackingDevices.size(); i++)
	{
		mTrackingDevices[i]->setEnabled( state );
	}
}

void core_sgct::SGCTTrackingManager::addDevice(const char * name)
{
	SGCTTrackingDevice * td = new SGCTTrackingDevice( mTrackingDevices.size(), name );
	
	mTrackers.push_back( NULL );
	mAnalogDevices.push_back( NULL );
	mButtonDevices.push_back( NULL );
	mTrackingDevices.push_back( td );

	sgct::MessageHandler::Instance()->print("Tracking: Adding device '%s'...\n", name);
}

void core_sgct::SGCTTrackingManager::addTrackerToDevice(const char * address)
{
	if(mTrackingDevices.empty())
		return;
	
	size_t index = mTrackingDevices.size() - 1;

	if( mTrackers[index] == NULL )
	{
		mTrackers[index] = new vrpn_Tracker_Remote( address );
		
		if( mTrackers[index] != NULL )
		{
			mTrackers[index]->register_change_handler(mTrackingDevices[index], update_tracker_cb);
			mTrackingDevices[index]->setPositionalDevicePresent(true);
			sgct::MessageHandler::Instance()->print("Tracking: Connecting to tracker '%s' on device %u...\n", address, index);
		}
		else
			sgct::MessageHandler::Instance()->print("Tracking: Failed to connect to tracker '%s' on device %u!\n", address, index);
	}
}

void core_sgct::SGCTTrackingManager::addButtonsToDevice(const char * address, size_t numOfButtons)
{
	if(mTrackingDevices.empty())
		return;
	
	size_t index = mTrackingDevices.size() - 1;
	
	if( mButtonDevices[index] == NULL )
	{
		mButtonDevices[index] = new vrpn_Button_Remote( address );
		if( mButtonDevices[index] != NULL )
		{
			mButtonDevices[index]->register_change_handler(mTrackingDevices[index], update_button_cb);
			mTrackingDevices[index]->setNumberOfButtons( numOfButtons );
			sgct::MessageHandler::Instance()->print("Tracking: Connecting to buttons '%s' on device %u...\n", address, index);
		}
		else
			sgct::MessageHandler::Instance()->print("Tracking: Failed to connect to buttons '%s' on device %u!\n", address, index);
	}
}

void core_sgct::SGCTTrackingManager::addAnalogsToDevice(const char * address, size_t numOfAxes)
{
	if(mTrackingDevices.empty())
		return;
	
	size_t index = mTrackingDevices.size() - 1;
	
	if( mAnalogDevices[index] == NULL )
	{
		mAnalogDevices[index] = new vrpn_Analog_Remote( address );
		if( mAnalogDevices[index] != NULL )
		{
			mAnalogDevices[index]->register_change_handler(mTrackingDevices[index], update_analog_cb);
			mTrackingDevices[index]->setNumberOfAxes( numOfAxes );
			sgct::MessageHandler::Instance()->print("Tracking: Connecting to analogs '%s' on device %u...\n", address, index);
		}
		else
			sgct::MessageHandler::Instance()->print("Tracking: Failed to connect to analogs '%s' on device %u!\n", address, index);
	}
}

void core_sgct::SGCTTrackingManager::setHeadSensorIndex(int index)
{
	mHeadSensorIndex = index;
	sgct::MessageHandler::Instance()->print("Tracking: Setting head sensor index=%d.\n", index);
}

void core_sgct::SGCTTrackingManager::setOrientation(double xRot, double yRot, double zRot)
{
	mXrot = xRot;
	mYrot = yRot;
	mZrot = zRot;

	calculateTransform();
}

void core_sgct::SGCTTrackingManager::setOffset(double x, double y, double z)
{
	mOffset[0] = x;
	mOffset[1] = y;
	mOffset[2] = z;

	calculateTransform();
}

void core_sgct::SGCTTrackingManager::calculateTransform()
{
	//create rotation quaternion based on x, y, z rotations
	glm::dquat rotQuat;
	rotQuat = glm::rotate( rotQuat, mXrot, glm::dvec3(1.0, 0.0, 0.0) );
	rotQuat = glm::rotate( rotQuat, mYrot, glm::dvec3(0.0, 1.0, 0.0) );
	rotQuat = glm::rotate( rotQuat, mZrot, glm::dvec3(0.0, 0.0, 1.0) );
	
	//create inverse rotation matrix
	mOrientation = glm::inverse( glm::mat4_cast(rotQuat) );

	//create offset translation matrix
	glm::dmat4 transMat = glm::translate( glm::dmat4(1.0), mOffset );
	//calculate transform
	mXform = transMat * mOrientation;
}

core_sgct::SGCTTrackingDevice * core_sgct::SGCTTrackingManager::getTrackingPtr(size_t index)
{
	return index < mTrackingDevices.size() ? mTrackingDevices[index] : NULL;
}

core_sgct::SGCTTrackingDevice * core_sgct::SGCTTrackingManager::getTrackingPtr(const char * name)
{
	for(size_t i=0; i<mTrackingDevices.size(); i++)
	{
		if( strcmp(name, mTrackingDevices[i]->getName().c_str()) == 0 )
			return mTrackingDevices[i];
	}

	//if not found
	return NULL;
}

void VRPN_CALLBACK update_tracker_cb(void *userdata, const vrpn_TRACKERCB info)
{
	core_sgct::SGCTTrackingDevice * tdPtr =
		reinterpret_cast<core_sgct::SGCTTrackingDevice *>(userdata);

	core_sgct::SGCTTrackingManager * tm = core_sgct::ClusterManager::Instance()->getTrackingManagerPtr();
	
	glm::dvec4 tmpVec = glm::dvec4( info.pos[0], info.pos[1], info.pos[2], 1.0 );

	fprintf(stderr, "Sensor: %d\n", info.sensor);

	tdPtr->setTrackerTime();
	tdPtr->setPosition( tm->getTransform() * tmpVec );
	tdPtr->setRotation( info.quat[0], info.quat[1], info.quat[2], info.quat[3] );
}

void VRPN_CALLBACK update_button_cb(void *userdata, const vrpn_BUTTONCB b )
{
	core_sgct::SGCTTrackingDevice * tdPtr =
		reinterpret_cast<core_sgct::SGCTTrackingDevice *>(userdata);

	b.state == 0 ? 
		tdPtr->setButtonVal( false, b.button) :
		tdPtr->setButtonVal( true, b.button);
}

void VRPN_CALLBACK update_analog_cb(void* userdata, const vrpn_ANALOGCB a )
{
	core_sgct::SGCTTrackingDevice * tdPtr =
		reinterpret_cast<core_sgct::SGCTTrackingDevice *>(userdata);
	
	int numberOfAxes = a.num_channel;
	for( int i=0; i < a.num_channel; i++ )
		tdPtr->setAnalogVal(a.channel[i], i);
}
