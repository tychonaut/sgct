﻿#include <stdlib.h>
#include <stdio.h>
#include "sgct.h"
#include "sgct/ClusterManager.h"
#include "sgct/Engine.h"
#include "sgct/Font.h"
#include "sgct/FontManager.h"
#include "sgct/NetworkManager.h"
#include "sgct/SGCTWindow.h"
#include <glm/gtc/matrix_transform.hpp>

namespace {
    constexpr const int ExtendedSize = 10000;
} // namespace

sgct::Engine* gEngine;

void drawFun();
void myDraw2DFun();
void preSyncFun();
void postSyncPreDrawFun();
void myPostDrawFun();
void initOGLFun();
void encodeFun();
void decodeFun();

void keyCallback(int key, int scancode, int action, int mods);
void externalControlCallback(const char * receivedChars, int size);

//variables to share across cluster
sgct::SharedDouble dt(0.0);
sgct::SharedDouble currentTime(0.0);
sgct::SharedWString sTimeOfDay;
sgct::SharedBool showFPS(false);
sgct::SharedBool extraPackages(false);
sgct::SharedBool barrier(false);
sgct::SharedBool resetCounter(false);
sgct::SharedBool stats(false);
sgct::SharedBool takeScreenshot(false);
sgct::SharedBool slowRendering(false);
sgct::SharedBool frametest(false);
sgct::SharedFloat speed(5.f);
sgct::SharedVector<float> extraData;

void drawGrid(float size, int steps);

int main(int argc, char* argv[]) {
    std::vector<std::string> arg(argv + 1, argv + argc);
    gEngine = new sgct::Engine(arg);

    gEngine->setClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    gEngine->setInitOGLFunction( initOGLFun );
    gEngine->setExternalControlCallback(externalControlCallback);
    gEngine->setKeyboardCallbackFunction(keyCallback);
    gEngine->setDraw2DFunction(myDraw2DFun);

    if( !gEngine->init() )
    {
        delete gEngine;
        return EXIT_FAILURE;
    }

    if (gEngine->isMaster()) {
        for (int i = 0; i < ExtendedSize; i++) {
            extraData.addVal(static_cast<float>(rand() % 500) / 500.0f);
        }
    }

    sgct::SharedData::instance()->setEncodeFunction(encodeFun);
    sgct::SharedData::instance()->setDecodeFunction(decodeFun);

    gEngine->setDrawFunction( drawFun );
    gEngine->setPreSyncFunction( preSyncFun );
    gEngine->setPostSyncPreDrawFunction( postSyncPreDrawFun );
    gEngine->setPostDrawFunction( myPostDrawFun );

    const std::vector<std::string>& addresses =
        sgct_core::NetworkManager::instance()->getLocalAddresses();
    for (unsigned int i = 0; i < addresses.size(); i++) {
        fprintf(stderr, "Address %u: %s\n", i, addresses[i].c_str());
    }

    gEngine->render();
    delete gEngine;
    exit(EXIT_SUCCESS);
}

void myDraw2DFun() {
#if INCLUDE_SGCT_TEXT
    sgct_text::print(
        sgct_text::FontManager::instance()->getFont("SGCTFont", 24),
        sgct_text::TextAlignMode::TopLeft,
        50,
        700, 
        glm::vec4(1.f, 0.f, 0.f, 1.f),
        "Focused: %s", gEngine->getCurrentWindow().isFocused() ? "true" : "false"
    );
    sgct_text::print(
        sgct_text::FontManager::instance()->getFont("SGCTFont", 24),
        sgct_text::TextAlignMode::TopLeft,
        100,
        500,
        glm::vec4(0.f, 1.f, 0.f, 1.f),
        L"Time: %ls", sTimeOfDay.getVal().c_str()
    );
    if (extraPackages.getVal() && extraData.getSize() == ExtendedSize) {
        float xPos =
            gEngine->getCurrentWindow().getFramebufferResolution().x / 2.f - 150.f;
        sgct_text::print(
            sgct_text::FontManager::instance()->getFont("SGCTFont", 16),
            sgct_text::TextAlignMode::TopLeft,
            xPos,
            150.f,
            glm::vec4(0.f, 1.f, 0.5f, 1.f),
            "Vector val: %f, size: %u",
            extraData.getValAt(ExtendedSize / 2), extraData.getSize()
        );
    }
#endif
}

void drawFun() {
    if (slowRendering.getVal()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    // test quadbuffer
    if (frametest.getVal()) {
        if (gEngine->getCurrentFrameNumber() % 2 == 0) {
            // even
            if (gEngine->getCurrentFrustumMode() == sgct_core::Frustum::StereoRightEye) {
                // left eye or mono since clear color is one step behind  -> red
                gEngine->setClearColor(0.f, 0.f, 1.f, 1.f);
            }
            else {
                // right -> blue
                gEngine->setClearColor(1.f, 0.f, 0.f, 1.f);
            }
        }
        else {
            // odd
            if (gEngine->getCurrentFrustumMode() == sgct_core::Frustum::StereoRightEye) {
                // left eye or mono since clear color is one step behind
                gEngine->setClearColor(0.5f, 0.5f, 0.5f, 1.f);
            }
            else {
                //right
                gEngine->setClearColor(0.f, 1.f, 0.f, 1.f);
            }
        }
    }
    else {
        gEngine->setClearColor(0.f, 0.f, 0.f, 0.f);
    }

    glPushMatrix();

    glRotatef(static_cast<float>(currentTime.getVal()) * speed.getVal(), 0.f, 1.f, 0.f);
    glScalef(1.f, 0.5f, 1.f);
    glColor3f(1.f, 1.f, 1.f);
    glLineWidth(3.0);

    // draw a cube
    // bottom
    glBegin(GL_LINE_STRIP);
    glVertex3f(-1.f, -1.f, -1.f);
    glVertex3f( 1.f, -1.f, -1.f);
    glVertex3f( 1.f, -1.f,  1.f);
    glVertex3f(-1.f, -1.f,  1.f);
    glVertex3f(-1.f, -1.f, -1.f);
    glEnd();

    // top
    glBegin(GL_LINE_STRIP);
    glVertex3f(-1.f, 1.f, -1.f);
    glVertex3f( 1.f, 1.f, -1.f);
    glVertex3f( 1.f, 1.f,  1.f);
    glVertex3f(-1.f, 1.f,  1.f);
    glVertex3f(-1.f, 1.f, -1.f);
    glEnd();

    // sides
    glBegin(GL_LINES);
    glVertex3f(-1.f, -1.f, -1.f);
    glVertex3f(-1.f,  1.f, -1.f);

    glVertex3f( 1.f, -1.f, -1.f);
    glVertex3f( 1.f,  1.f, -1.f);

    glVertex3f( 1.f, -1.f,  1.f);
    glVertex3f( 1.f,  1.f,  1.f);

    glVertex3f(-1.f, -1.f,  1.f);
    glVertex3f(-1.f,  1.f,  1.f);
    glEnd();

    glPopMatrix();

#if INCLUDE_SGCT_TEXT
    float xPos = gEngine->getCurrentWindow().getFramebufferResolution().x / 2.f;

    glColor3f(1.f, 1.f, 0.f);
    if (gEngine->getCurrentFrustumMode() == sgct_core::Frustum::StereoLeftEye) {
        sgct_text::print(
            sgct_text::FontManager::instance()->getFont("SGCTFont", 32),
            sgct_text::TextAlignMode::TopRight,
            xPos,
            200,
            "Left"
        );
    }
    else if (gEngine->getCurrentFrustumMode() == sgct_core::Frustum::StereoRightEye) {
        sgct_text::print(
            sgct_text::FontManager::instance()->getFont("SGCTFont", 32),
            sgct_text::TextAlignMode::TopLeft,
            xPos,
            150,
            "Right"
        );
    }
    else if (gEngine->getCurrentFrustumMode() == sgct_core::Frustum::MonoEye) {
        sgct_text::print(
            sgct_text::FontManager::instance()->getFont("SGCTFont", 32),
            sgct_text::TextAlignMode::TopLeft,
            xPos,
            200,
            "Mono"
        );
    }

    wchar_t str0[] = L"åäö"; // Swedish string
    wchar_t str1[] = L"лдощдффыкйцн"; // Russian string
    wchar_t str2[] = L"かんじ"; // Japanese string
    wchar_t str3[] = L"汉字"; // Chinese string
    wchar_t str4[] = L"mương"; // Vietnamese string
    wchar_t str5[] = L"한자"; // Korean string
    wchar_t str6[] = L"बईबईसई"; // Hindi string

    sgct_text::FontManager::instance()->getFont("SGCTFont", 32)->setStrokeSize(2);
    sgct_text::FontManager::instance()->setStrokeColor(glm::vec4(1.0f, 0.0f, 0.0f, 0.5f));
    
    // test
    glm::mat4 texMVP = glm::ortho(-1.f, 1.f, -1.f, 1.f);
    texMVP = glm::scale(texMVP, glm::vec3(0.1f));
    texMVP = glm::rotate(
        texMVP,
        static_cast<float>(currentTime.getVal()),
        glm::vec3(0.f, 1.f, 0.f)
    );

    sgct_text::print(
        sgct_text::FontManager::instance()->getFont("SGCTFont", 32),
        sgct_text::TextAlignMode::TopRight,
        500,
        500,
        L"%ls\n%ls\n%ls\n%ls\n%ls\n%ls\n%ls",
        str0, str1, str2, str3, str4, str5, str6
    );

    if (gEngine->getCurrentWindow().isUsingSwapGroups()) {
        sgct_text::print(
            sgct_text::FontManager::instance()->getFont("SGCTFont", 18),
            sgct_text::TextAlignMode::TopLeft,
            xPos - xPos / 2.f,
            450,
            "Swap group: Active"
        );

        sgct_text::print(
            sgct_text::FontManager::instance()->getFont("SGCTFont", 18),
            sgct_text::TextAlignMode::TopLeft,
            xPos - xPos / 2.f,
            500,
            "Press B to toggle barrier and R to reset counter"
        );

        if (gEngine->getCurrentWindow().isBarrierActive()) {
            sgct_text::print(
                sgct_text::FontManager::instance()->getFont("SGCTFont", 18),
                sgct_text::TextAlignMode::TopLeft,
                xPos - xPos / 2.f,
                400,
                "Swap barrier: Active"
            );
        }
        else {
            sgct_text::print(
                sgct_text::FontManager::instance()->getFont("SGCTFont", 18),
                sgct_text::TextAlignMode::TopLeft,
                xPos - xPos / 2.f,
                400,
                "Swap barrier: Inactive"
            );
        }

        if (gEngine->getCurrentWindow().isSwapGroupMaster()) {
            sgct_text::print(
                sgct_text::FontManager::instance()->getFont("SGCTFont", 18),
                sgct_text::TextAlignMode::TopLeft,
                xPos - xPos / 2.f,
                350,
                "Swap group master: True"
            );
        }
        else {
            sgct_text::print(
                sgct_text::FontManager::instance()->getFont("SGCTFont", 18),
                sgct_text::TextAlignMode::TopLeft,
                xPos - xPos / 2.f,
                350,
                "Swap group master: False"
            );
        }

        unsigned int iFrame = gEngine->getCurrentWindow().getSwapGroupFrameNumber();
        sgct_text::print(
            sgct_text::FontManager::instance()->getFont("SGCTFont", 18),
            sgct_text::TextAlignMode::TopLeft,
            xPos - xPos / 2.f,
            300,
            "Nvidia frame counter: %u", iFrame
        );
        sgct_text::print(
            sgct_text::FontManager::instance()->getFont("SGCTFont", 18),
            sgct_text::TextAlignMode::TopLeft,
            xPos - xPos / 2.f,
            250,
            "Framerate: %.3lf", 1.0 / gEngine->getDt()
        );
    }
    else {
        sgct_text::print(
            sgct_text::FontManager::instance()->getFont("SGCTFont", 18),
            sgct_text::TextAlignMode::TopLeft,
            xPos - xPos / 2.f,
            450,
            "Swap group: Inactive"
        );
    }
#endif
}

void preSyncFun() {
    if (gEngine->isMaster()) {
        dt.setVal(gEngine->getDt());
        currentTime.setVal(gEngine->getTime());

        time_t now = time(nullptr);
        constexpr const int TimeBufferSize = 256;
        char TimeBuffer[TimeBufferSize];
#if (_MSC_VER >= 1400) //visual studio 2005 or later
        tm timeInfo;
        errno_t err = localtime_s(&timeInfo, &now);
        if (err == 0) {
            strftime(TimeBuffer, TimeBufferSize, "%X", &timeInfo);
        }
#else
        tm* timeInfoPtr;
        timeInfoPtr = localtime(&now);
        strftime(TimeBuffer, TIME_BUFFER_SIZE, "%X", timeInfoPtr);
#endif
        const std::string time = TimeBuffer;
        const std::wstring wTime(time.begin(), time.end());
        sTimeOfDay.setVal(wTime);
    }
}

void postSyncPreDrawFun() {
    gEngine->setDisplayInfoVisibility(showFPS.getVal());

    // barrier is set by swap group not window both windows has the same HDC
    sgct::SGCTWindow::setBarrier(barrier.getVal());
    if (resetCounter.getVal()) {
        sgct::SGCTWindow::resetSwapGroupFrameNumber();
    }
    gEngine->setStatsGraphVisibility(stats.getVal());

    if (takeScreenshot.getVal()) {
        gEngine->takeScreenshot();
        takeScreenshot.setVal(false);
    }
}

void myPostDrawFun() {
    if (gEngine->isMaster()) {
        resetCounter.setVal(false);
    }
}

void initOGLFun() {
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_NORMALIZE);
    glDisable(GL_LIGHTING);
    glEnable(GL_COLOR_MATERIAL);

    std::size_t numberOfActiveViewports = 0;
    sgct_core::SGCTNode* thisNode = sgct_core::ClusterManager::instance()->getThisNode();
    for(std::size_t i=0; i < thisNode->getNumberOfWindows(); i++)
        for(std::size_t j=0; j < thisNode->getWindow(i).getNumberOfViewports(); j++)
            if (thisNode->getWindow(i).getViewport(j).isEnabled()) {
                numberOfActiveViewports++;
            }

    sgct::MessageHandler::instance()->print(
        "Number of active viewports: %d\n",
        numberOfActiveViewports
    );
}

void encodeFun() {
    unsigned char flags = 0;
    flags = showFPS.getVal()        ? flags | 1   : flags & ~1;   // bit 1
    flags = extraPackages.getVal()  ? flags | 2   : flags & ~2;   // bit 2
    flags = barrier.getVal()        ? flags | 4   : flags & ~4;   // bit 3
    flags = resetCounter.getVal()   ? flags | 8   : flags & ~8;   // bit 4
    flags = stats.getVal()          ? flags | 16  : flags & ~16;  // bit 5
    flags = takeScreenshot.getVal() ? flags | 32  : flags & ~32;  // bit 6
    flags = slowRendering.getVal()  ? flags | 64  : flags & ~64;  // bit 7
    flags = frametest.getVal()      ? flags | 128 : flags & ~128; // bit 8

    sgct::SharedUChar sf(flags);

    sgct::SharedData::instance()->writeDouble(dt);
    sgct::SharedData::instance()->writeDouble(currentTime);
    sgct::SharedData::instance()->writeFloat(speed);
    sgct::SharedData::instance()->writeUChar(sf);
    sgct::SharedData::instance()->writeWString(sTimeOfDay);

    if (extraPackages.getVal()) {
        sgct::SharedData::instance()->writeVector(extraData);
    }
}

void decodeFun() {
    sgct::SharedUChar sf;
    sgct::SharedData::instance()->readDouble(dt);
    sgct::SharedData::instance()->readDouble(currentTime);
    sgct::SharedData::instance()->readFloat(speed);
    sgct::SharedData::instance()->readUChar(sf);
    sgct::SharedData::instance()->readWString(sTimeOfDay);

    unsigned char flags = sf.getVal();
    showFPS.setVal(flags & 1);
    extraPackages.setVal(flags & 2);
    barrier.setVal(flags & 4);
    resetCounter.setVal(flags & 8);
    stats.setVal(flags & 16);
    takeScreenshot.setVal(flags & 32);
    slowRendering.setVal(flags & 64);
    frametest.setVal(flags & 128);

    if (extraPackages.getVal()) {
        sgct::SharedData::instance()->readVector(extraData);
    }
}

void keyCallback(int key, int, int action, int) {
    if (gEngine->isMaster()) {
        static bool mousePointer = true;

        switch (key) {
        case SGCT_KEY_C:
            if (action == SGCT_PRESS) {
                static bool useCompress = false;
                useCompress = !useCompress;
                sgct::SharedData::instance()->setCompression(useCompress);
            }
            break;
        case 'F':
            if (action == SGCT_PRESS) {
                frametest.setVal(!frametest.getVal());
            }
            break;
        case 'I':
            if (action == SGCT_PRESS) {
                showFPS.setVal(!showFPS.getVal());
            }
            break;
        case 'E':
            if (action == SGCT_PRESS) {
                extraPackages.setVal(!extraPackages.getVal());
            }
            break;
        case 'B':
            if (action == SGCT_PRESS) {
                barrier.setVal(!barrier.getVal());
            }
            break;
        case 'R':
            if (action == SGCT_PRESS) {
                resetCounter.setVal(!resetCounter.getVal());
            }
            break;
        case 'S':
            if (action == SGCT_PRESS) {
                stats.setVal(!stats.getVal());
            }
            break;
        case 'G':
            if (action == SGCT_PRESS) {
                gEngine->sendMessageToExternalControl("Testing!!\r\n");
            }
            break;
        case 'M':
            if (action == SGCT_PRESS) {
                mousePointer = !mousePointer;

                for (size_t i = 0; i < gEngine->getNumberOfWindows(); i++) {
                    sgct::Engine::setMouseCursorVisibility(i, mousePointer);
                }
            }
            break;
        case SGCT_KEY_F9:
            if (action == SGCT_PRESS) {
                slowRendering.setVal(!slowRendering.getVal());
            }
            break;
        case SGCT_KEY_F10:
            if (action == SGCT_PRESS) {
                takeScreenshot.setVal(true);
            }
            break;
        case SGCT_KEY_UP:
            speed.setVal(speed.getVal() * 1.1f);
            break;

        case SGCT_KEY_DOWN:
            speed.setVal(speed.getVal() / 1.1f);
            break;
        }
    }
}

void externalControlCallback(const char* receivedChars, int size) {
    if (gEngine->isMaster()) {
        std::string_view data(receivedChars, size);
        if (data == "info") {
            showFPS.setVal(!showFPS.getVal());
        }
        else if (data == "size") {
            gEngine->setExternalControlBufferSize(4096);
        }
    }
}
