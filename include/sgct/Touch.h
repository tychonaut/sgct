/*************************************************************************
Copyright (c) 2017 Erik Sund�n
All rights reserved.

For conditions of distribution and use, see copyright notice in sgct.h 
*************************************************************************/

#ifndef __SGCT__TOUCH__H__
#define __SGCT__TOUCH__H__

#include <string>
#include <unordered_map>
#include <vector>
#include <glm/glm.hpp>

struct GLFWtouch;

namespace sgct_core {

/*!
    This class holds and manages touch points
*/
class Touch {
public:
    //! Touch point information
    struct TouchPoint {
        enum TouchAction {
            NoAction,
            Released,
            Pressed,
            Stationary,
            Moved
        };
        TouchPoint(int i, TouchAction a, glm::vec2 p, glm::vec2 np,
            glm::vec2 nd = glm::vec2(0.f, 0.f));

        int id;
        TouchAction action;
        glm::vec2 pixelCoords;
        glm::vec2 normPixelCoords;
        glm::vec2 normPixelDiff;
    };

    // Retrieve the lastest touch points to process them in an event
    std::vector<TouchPoint> getLatestTouchPoints() const;

    // Need to call this function after the latest touch points have been processed, to clear them
    void latestPointsHandled();

    // Adding touch points to the touch class
    // As an id is constant over the touch point, the order will be preserved
    void processPoint(int id, int action, double xpos, double ypos, int windowWidth,
        int windowHeight);
    void processPoints(GLFWtouch* touchPoints, int count, int windowWidth, int windowHeight);

    bool isAllPointsStationary() const;

    static std::string getTouchPointInfo(const TouchPoint*);

private:
    std::vector<TouchPoint> mTouchPoints;
    std::vector<TouchPoint> mPreviousTouchPoints;
    std::unordered_map<int, glm::vec2> mPreviousTouchPositions;
    std::vector<int> mPrevTouchIds;
    bool mAllPointsStationary;
};

} // sgct_core

#endif // __SGCT__TOUCH__H__
