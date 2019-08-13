/*************************************************************************
Copyright (c) 2012-2015 Miroslav Andel
All rights reserved.

For conditions of distribution and use, see copyright notice in sgct.h
*************************************************************************/

#include <sgct/SGCTProjection.h>

#include <sgct/SGCTProjectionPlane.h>
#include <glm/gtc/matrix_transform.hpp>

namespace sgct_core {

void SGCTProjection::calculateProjection(glm::vec3 base,
                                         SGCTProjectionPlane* projectionPlanePtr,
                                         float nearClippingPlane,
                                         float farClippingPlane, glm::vec3 viewOffset)
{
    glm::vec3 lowerLeft = projectionPlanePtr->getCoordinate(
        SGCTProjectionPlane::LowerLeft
    );
    glm::vec3 upperLeft = projectionPlanePtr->getCoordinate(
        SGCTProjectionPlane::UpperLeft
    );
    glm::vec3 upperRight = projectionPlanePtr->getCoordinate(
        SGCTProjectionPlane::UpperRight
    );
    
    //calculate viewplane's internal coordinate system bases
    glm::vec3 plane_x = upperRight - upperLeft;
    glm::vec3 plane_y = upperLeft - lowerLeft;
    glm::vec3 plane_z = glm::cross(plane_x, plane_y);

    //normalize
    plane_x = glm::normalize(plane_x);
    plane_y = glm::normalize(plane_y);
    plane_z = glm::normalize(plane_z);

    const glm::vec3 world_x(1.f, 0.f, 0.f);
    const glm::vec3 world_y(0.f, 1.f, 0.f);
    const glm::vec3 world_z(0.f, 0.f, 1.f);

    //calculate plane rotation using
    //Direction Cosine Matrix (DCM)
    glm::mat3 DCM(1.0f); //init as identity matrix
    DCM[0][0] = glm::dot(plane_x, world_x);
    DCM[0][1] = glm::dot(plane_x, world_y);
    DCM[0][2] = glm::dot(plane_x, world_z);

    DCM[1][0] = glm::dot(plane_y, world_x);
    DCM[1][1] = glm::dot(plane_y, world_y);
    DCM[1][2] = glm::dot(plane_y, world_z);

    DCM[2][0] = glm::dot(plane_z, world_x);
    DCM[2][1] = glm::dot(plane_z, world_y);
    DCM[2][2] = glm::dot(plane_z, world_z);

    //invert & transform
    glm::mat3 DCM_inv = glm::inverse(DCM);
    glm::vec3 viewPlane[3];
    viewPlane[SGCTProjectionPlane::LowerLeft] = DCM_inv * lowerLeft;
    viewPlane[SGCTProjectionPlane::UpperLeft] = DCM_inv * upperLeft;
    viewPlane[SGCTProjectionPlane::UpperRight] = DCM_inv * upperRight;
    glm::vec3 eyePos = DCM_inv * base;

    //nearFactor = near clipping plane / focus plane dist
    float nearF = abs(
        nearClippingPlane / (viewPlane[SGCTProjectionPlane::LowerLeft].z - eyePos.z)
    );

    mFrustum.left = (viewPlane[SGCTProjectionPlane::LowerLeft].x - eyePos.x) * nearF;
    mFrustum.right = (viewPlane[SGCTProjectionPlane::UpperRight].x - eyePos.x) * nearF;
    mFrustum.bottom = (viewPlane[SGCTProjectionPlane::LowerLeft].y - eyePos.y) * nearF;
    mFrustum.top = (viewPlane[SGCTProjectionPlane::UpperRight].y - eyePos.y) * nearF;
    mFrustum.nearPlane = nearClippingPlane;
    mFrustum.farPlane = farClippingPlane;

    mViewMatrix = glm::mat4(DCM_inv) *
                  glm::translate(glm::mat4(1.f), -(base + viewOffset));

    //calc frustum matrix
    mProjectionMatrix = glm::frustum(
        mFrustum.left,
        mFrustum.right,
        mFrustum.bottom,
        mFrustum.top,
        mFrustum.nearPlane,
        mFrustum.farPlane
    );

    mViewProjectionMatrix = mProjectionMatrix * mViewMatrix;
}

Frustum& SGCTProjection::getFrustum() {
    return mFrustum;
}

const glm::mat4& SGCTProjection::getViewProjectionMatrix() {
    return mViewProjectionMatrix;
}

const glm::mat4& SGCTProjection::getViewMatrix() {
    return mViewMatrix;
}

const glm::mat4& SGCTProjection::getProjectionMatrix() {
    return mProjectionMatrix;
}

} // namespace sgct_core
