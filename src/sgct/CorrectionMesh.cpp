/*************************************************************************
Copyright (c) 2012-2015 Miroslav Andel
All rights reserved.

For conditions of distribution and use, see copyright notice in sgct.h
*************************************************************************/

#define MAX_LINE_LENGTH 1024
#define CONVERT_SCISS_TO_DOMEPROJECTION 0
#define CONVERT_SIMCAD_TO_DOMEPROJECTION_AND_SGC 0

#include <stdio.h>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <sgct/ogl_headers.h>
#include <sgct/MessageHandler.h>
#include <sgct/CorrectionMesh.h>
#include <sgct/ClusterManager.h>
#include <sgct/Engine.h>
#include <sgct/Viewport.h>
#include <sgct/SGCTSettings.h>
#include <sgct/helpers/SGCTStringFunctions.h>
#include <string>
#include <cstring>
#include <algorithm>
#include <sstream>

#if (_MSC_VER >= 1400) //visual studio 2005 or later
    #define _sscanf sscanf_s
#else
    #define _sscanf sscanf
#endif

struct SCISSTexturedVertex
{
    float x, y, z;
    float tx, ty, tz;
    SCISSTexturedVertex()
    {
        x = y = z = tx = ty = tz = 0.0f;
    };
};

struct SCISSViewData
{
    float qx, qy, qz, qw; // Rotation quaternion
    float x, y, z;          // Position of view (currently unused in Uniview)
    float fovUp, fovDown, fovLeft, fovRight;

    SCISSViewData()
    {
        qx = qy = qz = x = y = z = 0.0f;
        qw = 1.0f;
        fovUp = fovDown = fovLeft = fovRight = 20.0f;
    };
};

enum SCISSDistortionType { MESHTYPE_PLANAR, MESHTYPE_CUBE };

sgct_core::CorrectionMeshGeometry::CorrectionMeshGeometry()
{
    mMeshData[0] = GL_FALSE;
    mMeshData[1] = GL_FALSE;
    mMeshData[2] = GL_FALSE;

    mGeometryType = GL_TRIANGLE_STRIP;
    mNumberOfVertices = 0;
    mNumberOfIndices = 0;
}

sgct_core::CorrectionMeshGeometry::~CorrectionMeshGeometry()
{
    if (ClusterManager::instance()->getMeshImplementation() == ClusterManager::DISPLAY_LIST)
    {
        if (mMeshData[0])
        {
            sgct::MessageHandler::instance()->print(sgct::MessageHandler::NOTIFY_DEBUG, "CorrectionMeshGeometry: Releasing correction mesh OpenGL data...\n");
            glDeleteLists(mMeshData[0], 1);
        }
    }
    else
    {
        if (mMeshData[2])
            glDeleteVertexArrays(1, &mMeshData[2]);

        //delete VBO and IBO
        if (mMeshData[0])
        {
            sgct::MessageHandler::instance()->print(sgct::MessageHandler::NOTIFY_DEBUG, "CorrectionMeshGeometry: Releasing correction mesh OpenGL data...\n");
            glDeleteBuffers(2, &mMeshData[0]);
        }
    }
}

sgct_core::CorrectionMesh::CorrectionMesh()
{
    mTempVertices = NULL;
    mTempIndices = NULL;

    for (int i = 0; i < LAST_MESH; i++)
    {
        mGeometries[i].mNumberOfVertices = 0;
        mGeometries[i].mNumberOfIndices = 0;
    }
}

sgct_core::CorrectionMesh::~CorrectionMesh()
{    
    
}

/*!
This function finds a suitible parser for warping meshes and loads them into memory.

@param meshPath the path to the mesh data
@param meshHint a hint to pass to the parser selector
@param parent the pointer to parent viewport
@return true if mesh found and loaded successfully
*/
bool sgct_core::CorrectionMesh::readAndGenerateMesh(std::string meshPath, sgct_core::Viewport * parent,
		                                            MeshHint hint)
{    
    //generate unwarped mask
    setupSimpleMesh(&mGeometries[QUAD_MESH], parent);
    createMesh(&mGeometries[QUAD_MESH]);
    cleanUp();
    
    //generate unwarped mesh for mask
    if(parent->hasBlendMaskTexture() || parent->hasBlackLevelMaskTexture())
    {
        sgct::MessageHandler::instance()->print(sgct::MessageHandler::NOTIFY_DEBUG, "CorrectionMesh: Creating mask mesh\n");
        
        bool flip_x = false;
        bool flip_y = false;
        //if (hint == DOMEPROJECTION_HINT)
        //    flip_x = true;

        setupMaskMesh(parent, flip_x, flip_y);
        createMesh(&mGeometries[MASK_MESH]);
        cleanUp();
    }

    //fallback if no mesh is provided
    if ( meshPath.empty())
    {
        setupSimpleMesh(&mGeometries[WARP_MESH], parent);
        createMesh(&mGeometries[WARP_MESH]);
        cleanUp();

        sgct::MessageHandler::instance()->print(sgct::MessageHandler::NOTIFY_DEBUG, "CorrectionMesh: Empty mesh path.\n");
        return false;
    }
    
    //transform to lowercase
    std::string path(meshPath);
    std::transform(path.begin(), path.end(), path.begin(), ::tolower);

    MeshFormat meshFmt = NO_FMT;
    //find a suitible format
    if (path.find(".sgc") != std::string::npos)
        meshFmt = SCISS_FMT;
    else if (path.find(".ol") != std::string::npos)
        meshFmt = SCALEABLE_FMT;
    else if (path.find(".skyskan") != std::string::npos)
        meshFmt = SKYSKAN_FMT;
    else if (path.find(".txt") != std::string::npos)
    {
        if (hint == NO_HINT || hint == SKYSKAN_HINT)//default for this suffix
            meshFmt = SKYSKAN_FMT;
    }
    else if (path.find(".csv") != std::string::npos)
    {
        if (hint == NO_HINT || hint == DOMEPROJECTION_HINT)//default for this suffix
            meshFmt = DOMEPROJECTION_FMT;
    }
    else if (path.find(".data") != std::string::npos)
    {
        if (hint == NO_HINT || hint == PAULBOURKE_HINT)//default for this suffix
            meshFmt = PAULBOURKE_FMT;
    }
    else if (path.find(".obj") != std::string::npos)
    {
        if (hint == NO_HINT || hint == OBJ_HINT)//default for this suffix
            meshFmt = OBJ_FMT;
    }
    else if (path.find(".mpcdi") != std::string::npos)
    {
        //if (hint == MPCDI_HINT)
        meshFmt = MPCDI_FMT;
    }
	else if (path.find(".pfm") != std::string::npos) //portable floatmap, MPCDI encodes the warp "mesh" as raster image;
	{
		if (hint == MPCDI_HINT)
		{
			meshFmt = MPCDI_FMT;
		}
	}
    else if (path.find(".simcad") != std::string::npos)
    {
        if (hint == NO_HINT || hint == SIMCAD_HINT)//default for this suffix
            meshFmt = SIMCAD_FMT;
    }

    //select parser
    bool loadStatus = false;
    switch (meshFmt)
    {
    case DOMEPROJECTION_FMT:
        loadStatus = readAndGenerateDomeProjectionMesh(meshPath, parent);
        break;

    case SCALEABLE_FMT:
        loadStatus = readAndGenerateScalableMesh(meshPath, parent);
        break;

    case SCISS_FMT:
        loadStatus = readAndGenerateScissMesh(meshPath, parent);
        break;

    case SIMCAD_FMT:
        loadStatus = readAndGenerateSimCADMesh(meshPath, parent);
        break;

    case SKYSKAN_FMT:
        loadStatus = readAndGenerateSkySkanMesh(meshPath, parent);
        break;

    case PAULBOURKE_FMT:
        loadStatus = readAndGeneratePaulBourkeMesh(meshPath, parent);
        break;

    case OBJ_FMT:
        loadStatus = readAndGenerateOBJMesh(meshPath, parent);
        break;

    case MPCDI_FMT:
		if (path.find(".pfm") != std::string::npos) 
		{
			// portable floatmap, MPCDI encodes the warp "mesh" as raster image; 
			// indicates that NOT a full zipped *.mpcdi file is loaded, but only the warp subset of it, which it a .pfm file
			loadStatus = readAndGenerateMpcdiMesh(meshPath, parent);
		}
		else
		{
			// old code; passing an empty path string; 
			// I suggest it's because an MPCDI file is loaded before this mesh-generation function and hence indicates that the data shall be grabed from an object in RAM instead of loading from file
			loadStatus = readAndGenerateMpcdiMesh("", parent); 
		}
		break;
            
    case NO_FMT:

        sgct::MessageHandler::instance()->print(sgct::MessageHandler::NOTIFY_ERROR, "CorrectionMesh error: Loading mesh '%s' failed!\n", meshPath.c_str());
		break;
    }

    //export
    if (loadStatus && sgct::SGCTSettings::instance()->getExportWarpingMeshes())
    {
        std::size_t found = meshPath.find_last_of(".");
        if (found != std::string::npos)
        {
            std::string filename = meshPath.substr(0, found) + "_export.obj";
            exportMesh(filename);
        }
    }
    cleanUp();

    if( !loadStatus )
    {
        sgct::MessageHandler::instance()->print(sgct::MessageHandler::NOTIFY_ERROR, "CorrectionMesh error: Loading mesh '%s' failed!\n", meshPath.c_str());
        
        setupSimpleMesh(&mGeometries[WARP_MESH], parent);
        createMesh(&mGeometries[WARP_MESH]);
        cleanUp();
        return false;
    }

    return true;
}

/*!
Parse data from domeprojection's camera based calibration system. Domeprojection.com
*/
bool sgct_core::CorrectionMesh::readAndGenerateDomeProjectionMesh(const std::string & meshPath, Viewport * parent)
{
    sgct::MessageHandler::instance()->print(sgct::MessageHandler::NOTIFY_INFO,
        "CorrectionMesh: Reading DomeProjection mesh data from '%s'.\n", meshPath.c_str());

    FILE * meshFile = NULL;
#if (_MSC_VER >= 1400) //visual studio 2005 or later
    if (fopen_s(&meshFile, meshPath.c_str(), "r") != 0 || !meshFile)
    {
        sgct::MessageHandler::instance()->print(sgct::MessageHandler::NOTIFY_ERROR, "CorrectionMesh: Failed to open warping mesh file!\n");
        return false;
    }
#else
    meshFile = fopen(meshPath.c_str(), "r");
    if (meshFile == NULL)
    {
        sgct::MessageHandler::instance()->print(sgct::MessageHandler::NOTIFY_ERROR, "CorrectionMesh: Failed to open warping mesh file!\n");
        return false;
    }
#endif

    char lineBuffer[MAX_LINE_LENGTH];
    float x, y, u, v;
    unsigned int col, row;
    unsigned int numberOfCols = 0;
    unsigned int numberOfRows = 0;

    CorrectionMeshVertex vertex;
    std::vector<CorrectionMeshVertex> vertices;

    //init to max intencity (opaque white)
    vertex.r = 1.0f;
    vertex.g = 1.0f;
    vertex.b = 1.0f;
    vertex.a = 1.0f;

    while (!feof(meshFile))
    {
        if (fgets(lineBuffer, MAX_LINE_LENGTH, meshFile) != NULL)
        {
#if (_MSC_VER >= 1400) //visual studio 2005 or later
            if (sscanf_s(lineBuffer, "%f;%f;%f;%f;%u;%u", &x, &y, &u, &v, &col, &row) == 6)
#else
            if (sscanf(lineBuffer, "%f;%f;%f;%f;%u;%u", &x, &y, &u, &v, &col, &row) == 6)
#endif
            {
                //find dimensions of meshdata
                if (col > numberOfCols)
                    numberOfCols = col;

                if (row > numberOfRows)
                    numberOfRows = row;

                //clamp
                clamp(x, 1.0f, 0.0f);
                clamp(y, 1.0f, 0.0f);
                //clamp(u, 1.0f, 0.0f);
                //clamp(v, 1.0f, 0.0f);

                //convert to [-1, 1]
                vertex.x = 2.0f * (x * parent->getXSize() + parent->getX()) - 1.0f;
                vertex.y = 2.0f * ((1.0f-y) * parent->getYSize() + parent->getY()) - 1.0f;

                //scale to viewport coordinates
                vertex.s = u * parent->getXSize() + parent->getX();
                vertex.t = (1.0f-v) * parent->getYSize() + parent->getY();

                vertices.push_back(vertex);
            }
            
        }

    }

    fclose(meshFile);

    //add one to actually store the dimensions instread of largest index
    numberOfCols++;
    numberOfRows++;

    //copy vertices
    unsigned int numberOfVertices = numberOfCols * numberOfRows;
    mTempVertices = new CorrectionMeshVertex[numberOfVertices];
    memcpy(mTempVertices, vertices.data(), numberOfVertices * sizeof(CorrectionMeshVertex));
    mGeometries[WARP_MESH].mNumberOfVertices = numberOfVertices;
    vertices.clear();

    std::vector<unsigned int> indices;
    unsigned int i0, i1, i2, i3;
    for (unsigned int c = 0; c < (numberOfCols -1); c++)
        for (unsigned int r = 0; r < (numberOfRows-1); r++)
        {
            i0 = r * numberOfCols + c;
            i1 = r * numberOfCols + (c + 1);
            i2 = (r + 1) * numberOfCols + (c + 1);
            i3 = (r + 1) * numberOfCols + c;

            //fprintf(stderr, "Indexes: %u %u %u %u\n", i0, i1, i2, i3);

            /*

            3      2
             x____x
             |   /|
             |  / |
             | /  |
             |/   |
             x----x
            0      1
            
            */

            //triangle 1
            indices.push_back(i0);
            indices.push_back(i1);
            indices.push_back(i2);

            //triangle 2
            indices.push_back(i0);
            indices.push_back(i2);
            indices.push_back(i3);
        }

    //allocate and copy indices
    mGeometries[WARP_MESH].mNumberOfIndices = static_cast<unsigned int>(indices.size());
    mTempIndices = new unsigned int[mGeometries[WARP_MESH].mNumberOfIndices];
    memcpy(mTempIndices, indices.data(), mGeometries[WARP_MESH].mNumberOfIndices * sizeof(unsigned int));
    indices.clear();

    mGeometries[WARP_MESH].mGeometryType = GL_TRIANGLES;

    createMesh(&mGeometries[WARP_MESH]);

    sgct::MessageHandler::instance()->print(sgct::MessageHandler::NOTIFY_DEBUG, "CorrectionMesh: Correction mesh read successfully! Vertices=%u, Indices=%u.\n", mGeometries[WARP_MESH].mNumberOfVertices, mGeometries[WARP_MESH].mNumberOfIndices);
    
    return true;
}

bool sgct_core::CorrectionMesh::readAndGenerateScalableMesh(const std::string & meshPath, Viewport * parent)
{
    sgct::MessageHandler::instance()->print(sgct::MessageHandler::NOTIFY_INFO,
        "CorrectionMesh: Reading scalable mesh data from '%s'.\n", meshPath.c_str());

    FILE * meshFile = NULL;
#if (_MSC_VER >= 1400) //visual studio 2005 or later
    if( fopen_s(&meshFile, meshPath.c_str(), "r") != 0 || !meshFile )
    {
        sgct::MessageHandler::instance()->print(sgct::MessageHandler::NOTIFY_ERROR, "CorrectionMesh: Failed to open warping mesh file!\n");
        return false;
    }
#else
    meshFile = fopen(meshPath.c_str(), "r");
    if( meshFile == NULL )
    {
        sgct::MessageHandler::instance()->print(sgct::MessageHandler::NOTIFY_ERROR, "CorrectionMesh: Failed to open warping mesh file!\n");
        return false;
    }
#endif

    float x,y,s,t;
    unsigned int intensity;
    unsigned int a,b,c;
    unsigned int numOfVerticesRead = 0;
    unsigned int numOfFacesRead = 0;
    unsigned int numberOfFaces = 0;
    unsigned int numberOfVertices = 0;
    unsigned int numberOfIndices = 0;

    double orthoCoords[4];
    orthoCoords[0] = -1.0;
    orthoCoords[1] = 1.0;
    orthoCoords[2] = -1.0;
    orthoCoords[3] = 1.0;
    unsigned int resolution[2];
    resolution[0] = 0;
    resolution[1] = 0;

    CorrectionMeshVertex * vertexPtr;

    char lineBuffer[MAX_LINE_LENGTH];
    while( !feof( meshFile ) )
    {
        if( fgets(lineBuffer, MAX_LINE_LENGTH, meshFile ) != NULL )
        {
#if (_MSC_VER >= 1400) //visual studio 2005 or later
            if( sscanf_s(lineBuffer, "%f %f %u %f %f", &x, &y, &intensity, &s, &t) == 5 )
#else
            if( sscanf(lineBuffer, "%f %f %u %f %f", &x, &y, &intensity, &s, &t) == 5 )
#endif
            {
                if( mTempVertices != NULL && resolution[0] != 0 && resolution[1] != 0 )
                {
                    vertexPtr = &mTempVertices[numOfVerticesRead];
                    vertexPtr->x = (x / static_cast<float>(resolution[0])) * parent->getXSize() + parent->getX();
                    vertexPtr->y = (y / static_cast<float>(resolution[1])) * parent->getYSize() + parent->getY();
                    vertexPtr->r = static_cast<float>(intensity)/255.0f;
                    vertexPtr->g = static_cast<float>(intensity)/255.0f;
                    vertexPtr->b = static_cast<float>(intensity)/255.0f;
                    vertexPtr->a = 1.0f;
                    vertexPtr->s = (1.0f - t) * parent->getXSize() + parent->getX();
                    vertexPtr->t = (1.0f - s) * parent->getYSize() + parent->getY();

                    numOfVerticesRead++;
                }
            }
#if (_MSC_VER >= 1400) //visual studio 2005 or later
            else if( sscanf_s(lineBuffer, "[ %u %u %u ]", &a, &b, &c) == 3 )
#else
            else if( sscanf(lineBuffer, "[ %u %u %u ]", &a, &b, &c) == 3 )
#endif
            {
                if (mTempIndices != NULL)
                {
                    mTempIndices[numOfFacesRead * 3] = a;
                    mTempIndices[numOfFacesRead * 3 + 1] = b;
                    mTempIndices[numOfFacesRead * 3 + 2] = c;
                }

                numOfFacesRead++;
            }
            else
            {
                char tmpString[16];
                tmpString[0] = '\0';
                double tmpD = 0.0;
                unsigned int tmpUI = 0;

#if (_MSC_VER >= 1400) //visual studio 2005 or later
                if( sscanf_s(lineBuffer, "VERTICES %u", &numberOfVertices) == 1 )
#else
                if( sscanf(lineBuffer, "VERTICES %u", &numberOfVertices) == 1 )
#endif
                {
                    mTempVertices = new CorrectionMeshVertex[ numberOfVertices ];
                    memset(mTempVertices, 0, numberOfVertices * sizeof(CorrectionMeshVertex));
                }

#if (_MSC_VER >= 1400) //visual studio 2005 or later
                else if( sscanf_s(lineBuffer, "FACES %u", &numberOfFaces) == 1 )
#else
                else if (sscanf(lineBuffer, "FACES %u", &numberOfFaces) == 1)
#endif
                {
                    numberOfIndices = numberOfFaces * 3;
                    mTempIndices = new unsigned int[numberOfIndices];
                    memset(mTempIndices, 0, numberOfIndices * sizeof(unsigned int));
                }

#if (_MSC_VER >= 1400) //visual studio 2005 or later
                else if( sscanf_s(lineBuffer, "ORTHO_%s %lf", tmpString, 16, &tmpD) == 2 )
#else
                else if( sscanf(lineBuffer, "ORTHO_%s %lf", tmpString, &tmpD) == 2 )
#endif
                {
                    if( strcmp(tmpString, "LEFT") == 0 )
                        orthoCoords[0] = tmpD;
                    else if( strcmp(tmpString, "RIGHT") == 0 )
                        orthoCoords[1] = tmpD;
                    else if( strcmp(tmpString, "BOTTOM") == 0 )
                        orthoCoords[2] = tmpD;
                    else if( strcmp(tmpString, "TOP") == 0 )
                        orthoCoords[3] = tmpD;
                }

#if (_MSC_VER >= 1400) //visual studio 2005 or later
                else if( sscanf_s(lineBuffer, "NATIVEXRES %u", &tmpUI) == 1 )
#else
                else if( sscanf(lineBuffer, "NATIVEXRES %u", &tmpUI) == 1 )
#endif
                    resolution[0] = tmpUI;

#if (_MSC_VER >= 1400) //visual studio 2005 or later
                else if( sscanf_s(lineBuffer, "NATIVEYRES %u", &tmpUI) == 1 )
#else
                else if( sscanf(lineBuffer, "NATIVEYRES %u", &tmpUI) == 1 )
#endif
                    resolution[1] = tmpUI;
            }

            //fprintf(stderr, "Row text: %s", lineBuffer);
        }

    }

    if (numberOfVertices != numOfVerticesRead || numberOfFaces != numOfFacesRead)
    {
        sgct::MessageHandler::instance()->print(sgct::MessageHandler::NOTIFY_ERROR, "CorrectionMesh: Incorrect mesh data geometry!");
        return false;
    }

    //normalize
    for (unsigned int i = 0; i < numberOfVertices; i++)
    {
        float xMin = static_cast<float>(orthoCoords[0]);
        float xMax = static_cast<float>(orthoCoords[1]);
        float yMin = static_cast<float>(orthoCoords[2]);
        float yMax = static_cast<float>(orthoCoords[3]);

        //normalize between 0.0 and 1.0
        float xVal = (mTempVertices[i].x - xMin) / (xMax - xMin);
        float yVal = (mTempVertices[i].y - yMin) / (yMax - yMin);

        //normalize between -1.0 to 1.0
        mTempVertices[i].x = xVal * 2.0f - 1.0f;
        mTempVertices[i].y = yVal * 2.0f - 1.0f;
    }

    fclose( meshFile );

    mGeometries[WARP_MESH].mNumberOfVertices = numberOfVertices;
    mGeometries[WARP_MESH].mNumberOfIndices = numberOfIndices;
    mGeometries[WARP_MESH].mGeometryType = GL_TRIANGLES;

    createMesh(&mGeometries[WARP_MESH]);

    sgct::MessageHandler::instance()->print(sgct::MessageHandler::NOTIFY_INFO, "CorrectionMesh: Correction mesh read successfully! Vertices=%u, Faces=%u.\n", numOfVerticesRead, numOfFacesRead);

    return true;
}

bool sgct_core::CorrectionMesh::readAndGenerateScissMesh(const std::string & meshPath, sgct_core::Viewport * parent)
{    
    sgct::MessageHandler::instance()->print(sgct::MessageHandler::NOTIFY_INFO,
        "CorrectionMesh: Reading sciss mesh data from '%s'.\n", meshPath.c_str());

    FILE * meshFile = NULL;
#if (_MSC_VER >= 1400) //visual studio 2005 or later
    if (fopen_s(&meshFile, meshPath.c_str(), "rb") != 0 || !meshFile)
    {
        sgct::MessageHandler::instance()->print(sgct::MessageHandler::NOTIFY_ERROR, "CorrectionMesh: Failed to open warping mesh file!\n");
        return false;
    }
#else
    meshFile = fopen(meshPath.c_str(), "rb");
    if (meshFile == NULL)
    {
        sgct::MessageHandler::instance()->print(sgct::MessageHandler::NOTIFY_ERROR, "CorrectionMesh: Failed to open warping mesh file!\n");
        return false;
    }
#endif

    size_t retval;
    unsigned int numberOfVertices = 0;
    unsigned int numberOfIndices = 0;

    char fileID[3];
#if (_MSC_VER >= 1400) //visual studio 2005 or later
    retval = fread_s(fileID, sizeof(char)*3, sizeof(char), 3, meshFile);
#else
    retval = fread(fileID, sizeof(char), 3, meshFile);
#endif

    //check fileID
    if (fileID[0] != 'S' || fileID[1] != 'G' || fileID[2] != 'C' || retval != 3)
    {
        sgct::MessageHandler::instance()->print(sgct::MessageHandler::NOTIFY_ERROR, "CorrectionMesh: Incorrect file id!\n");
        fclose(meshFile);
        return false;
    }

    //read file version
    uint8_t fileVersion;
#if (_MSC_VER >= 1400) //visual studio 2005 or later
    retval = fread_s(&fileVersion, sizeof(uint8_t), sizeof(uint8_t), 1, meshFile);
#else
    retval = fread(&fileVersion, sizeof(uint8_t), 1, meshFile);
#endif
    if (retval != 1)
    {
        sgct::MessageHandler::instance()->print(sgct::MessageHandler::NOTIFY_ERROR, "CorrectionMesh: Error parsing file!\n");
        fclose(meshFile);
        return false;
    }
    else
        sgct::MessageHandler::instance()->print(sgct::MessageHandler::NOTIFY_DEBUG, "CorrectionMesh: file version %u\n", fileVersion);

    //read mapping type
    unsigned int mappingType;
#if (_MSC_VER >= 1400) //visual studio 2005 or later
    retval = fread_s(&mappingType, sizeof(unsigned int), sizeof(unsigned int), 1, meshFile);
#else
    retval = fread(&mappingType, sizeof(unsigned int), 1, meshFile);
#endif
    if (retval != 1)
    {
        sgct::MessageHandler::instance()->print(sgct::MessageHandler::NOTIFY_ERROR, "CorrectionMesh: Error parsing file!\n");
        fclose(meshFile);
        return false;
    }
    else
        sgct::MessageHandler::instance()->print(sgct::MessageHandler::NOTIFY_DEBUG, "CorrectionMesh: Mapping type = %s (%u)\n", mappingType == 0 ? "planar" : "cube", mappingType);

    //read viewdata
    SCISSViewData viewData;
    double yaw, pitch, roll;
#if (_MSC_VER >= 1400) //visual studio 2005 or later
    retval = fread_s(&viewData, sizeof(SCISSViewData), sizeof(SCISSViewData), 1, meshFile);
#else
    retval = fread(&viewData, sizeof(SCISSViewData), 1, meshFile);
#endif
    if (retval != 1)
    {
        sgct::MessageHandler::instance()->print(sgct::MessageHandler::NOTIFY_ERROR, "CorrectionMesh: Error parsing file!\n");
        fclose(meshFile);
        return false;
    }
    else
    {
        double x, y, z, w;
        x = static_cast<double>(viewData.qx);
        y = static_cast<double>(viewData.qy);
        z = static_cast<double>(viewData.qz);
        w = static_cast<double>(viewData.qw);

        glm::dvec3 angles = glm::degrees(glm::eulerAngles(glm::dquat(w, y, x, z)));
        yaw = -angles.x;
        pitch = angles.y;
        roll = -angles.z;
        
        sgct::MessageHandler::instance()->print(sgct::MessageHandler::NOTIFY_DEBUG, "CorrectionMesh: Rotation quat = [%f %f %f %f]\nyaw = %lf, pitch = %lf, roll = %lf\n",
            viewData.qx, viewData.qy, viewData.qz, viewData.qw,
            yaw, pitch, roll);

        sgct::MessageHandler::instance()->print(sgct::MessageHandler::NOTIFY_DEBUG, "CorrectionMesh: Position = [%f %f %f]\n",
            viewData.x, viewData.y, viewData.z);

        sgct::MessageHandler::instance()->print(sgct::MessageHandler::NOTIFY_DEBUG, "CorrectionMesh: FOV up = %f\n",
            viewData.fovUp);

        sgct::MessageHandler::instance()->print(sgct::MessageHandler::NOTIFY_DEBUG, "CorrectionMesh: FOV down = %f\n",
            viewData.fovDown);

        sgct::MessageHandler::instance()->print(sgct::MessageHandler::NOTIFY_DEBUG, "CorrectionMesh: FOV left = %f\n",
            viewData.fovLeft);

        sgct::MessageHandler::instance()->print(sgct::MessageHandler::NOTIFY_DEBUG, "CorrectionMesh: FOV right = %f\n",
            viewData.fovRight);
    }

    //read number of vertices
    unsigned int size[2];
#if (_MSC_VER >= 1400) //visual studio 2005 or later
    retval = fread_s(size, sizeof(unsigned int)*2, sizeof(unsigned int), 2, meshFile);
#else
    retval = fread(size, sizeof(unsigned int), 2, meshFile);
#endif
    if (retval != 2)
    { 
        sgct::MessageHandler::instance()->print(sgct::MessageHandler::NOTIFY_ERROR, "CorrectionMesh: Error parsing file!\n");
        fclose(meshFile);
        return false;
    }
    else
    {
        if (fileVersion == 2) {
            numberOfVertices =  size[1];
            sgct::MessageHandler::instance()->print(sgct::MessageHandler::NOTIFY_DEBUG, "CorrectionMesh: Number of vertices = %u\n", numberOfVertices);
        }
        else {
            numberOfVertices = size[0] * size[1];
            sgct::MessageHandler::instance()->print(sgct::MessageHandler::NOTIFY_DEBUG, "CorrectionMesh: Number of vertices = %u (%ux%u)\n", numberOfVertices, size[0], size[1]);
        }
    }
    //read vertices
    SCISSTexturedVertex * texturedVertexList = new SCISSTexturedVertex[numberOfVertices];
#if (_MSC_VER >= 1400) //visual studio 2005 or later
    retval = fread_s(texturedVertexList, sizeof(SCISSTexturedVertex) * numberOfVertices, sizeof(SCISSTexturedVertex), numberOfVertices, meshFile);
#else
    retval = fread(texturedVertexList, sizeof(SCISSTexturedVertex), numberOfVertices, meshFile);
#endif
    if (retval != numberOfVertices)
    {
        sgct::MessageHandler::instance()->print(sgct::MessageHandler::NOTIFY_ERROR, "CorrectionMesh: Error parsing file!\n");
        fclose(meshFile);
        return false;
    }

    //read number of indices
#if (_MSC_VER >= 1400) //visual studio 2005 or later
    retval = fread_s(&numberOfIndices, sizeof(unsigned int), sizeof(unsigned int), 1, meshFile);
#else
    retval = fread(&numberOfIndices, sizeof(unsigned int), 1, meshFile);
#endif

    if (retval != 1)
    {
        sgct::MessageHandler::instance()->print(sgct::MessageHandler::NOTIFY_ERROR, "CorrectionMesh: Error parsing file!\n");
        fclose(meshFile);
        return false;
    }
    else
        sgct::MessageHandler::instance()->print(sgct::MessageHandler::NOTIFY_DEBUG, "CorrectionMesh: Number of indices = %u\n", numberOfIndices);

    //read faces
    if (numberOfIndices > 0)
    {
        mTempIndices = new unsigned int[numberOfIndices];
#if (_MSC_VER >= 1400) //visual studio 2005 or later
        retval = fread_s(mTempIndices, sizeof(unsigned int)* numberOfIndices, sizeof(unsigned int), numberOfIndices, meshFile);
#else
        retval = fread(mTempIndices, sizeof(unsigned int), numberOfIndices, meshFile);
#endif
        if (retval != numberOfIndices)
        {
            sgct::MessageHandler::instance()->print(sgct::MessageHandler::NOTIFY_ERROR, "CorrectionMesh: Error parsing file!\n");
            fclose(meshFile);
            return false;
        }
    }

    fclose(meshFile);

    parent->getUser()->setPos(
        viewData.x, viewData.y, viewData.z);

    parent->setViewPlaneCoordsUsingFOVs(
        viewData.fovUp,
        viewData.fovDown,
        viewData.fovLeft,
        viewData.fovRight,
        glm::quat(viewData.qw, viewData.qx, viewData.qy, viewData.qz)
        );

    sgct::Engine::instance()->updateFrustums();

    CorrectionMeshVertex * vertexPtr;
    SCISSTexturedVertex * scissVertexPtr;

    //store all verts in sgct format
    mTempVertices = new CorrectionMeshVertex[numberOfVertices];
    for (unsigned int i = 0; i < numberOfVertices; i++)
    {
        vertexPtr = &mTempVertices[i];
        scissVertexPtr = &texturedVertexList[i];

        vertexPtr->r = 1.0f;
        vertexPtr->g = 1.0f;
        vertexPtr->b = 1.0f;
        vertexPtr->a = 1.0f;

        //clamp
        clamp(scissVertexPtr->x, 1.0f, 0.0f);
        clamp(scissVertexPtr->y, 1.0f, 0.0f);
        clamp(scissVertexPtr->tx, 1.0f, 0.0f);
        clamp(scissVertexPtr->ty, 1.0f, 0.0f);

        //convert to [-1, 1]
        vertexPtr->x = 2.0f*(scissVertexPtr->x * parent->getXSize() + parent->getX()) - 1.0f;
        vertexPtr->y = 2.0f*((1.0f - scissVertexPtr->y) * parent->getYSize() + parent->getY()) - 1.0f;

        vertexPtr->s = scissVertexPtr->tx * parent->getXSize() + parent->getX();
        vertexPtr->t = scissVertexPtr->ty * parent->getYSize() + parent->getY();

        /*fprintf(stderr, "Coords: %f %f %f\tTex: %f %f %f\n",
            scissVertexPtr->x, scissVertexPtr->y, scissVertexPtr->z,
            scissVertexPtr->tx, scissVertexPtr->ty, scissVertexPtr->tz);*/
    }

#if CONVERT_SCISS_TO_DOMEPROJECTION
    //test export to dome projection
    std::string baseOutFilename = meshPath.substr(0, meshPath.find_last_of(".sgc") - 3);
    
    //test export frustum
    std::string outFrustumFilename = baseOutFilename + "_frustum" + std::string(".csv");
    sgct::MessageHandler::instance()->print(sgct::MessageHandler::NOTIFY_DEBUG,
        "CorrectionMesh: Exporting dome projection frustum file \"%s\"\n", outFrustumFilename.c_str());
    std::ofstream outFrustumFile;
    outFrustumFile.open(outFrustumFilename, std::ios::out);
    outFrustumFile << "x;y;z;heading;pitch;bank;left;right;bottom;top;tanLeft;tanRight;tanBottom;tanTop;width;height" << std::endl;
    outFrustumFile << std::fixed;
    outFrustumFile << std::setprecision(8);
    outFrustumFile << viewData.x << ";" << viewData.y << ";" << viewData.z << ";"; //x y z
    outFrustumFile << yaw << ";" << pitch << ";" << roll << ";";
    outFrustumFile << viewData.fovLeft << ";" << viewData.fovRight << ";" << viewData.fovDown << ";" << viewData.fovUp << ";";
    float tanLeft = tan(glm::radians(viewData.fovLeft));
    float tanRight = tan(glm::radians(viewData.fovRight));
    float tanBottom = tan(glm::radians(viewData.fovDown));
    float tanTop = tan(glm::radians(viewData.fovUp));
    outFrustumFile << tanLeft << ";" << tanRight << ";" << tanBottom << ";" << tanTop << ";";
    outFrustumFile << tanRight - tanLeft << ";";
    outFrustumFile << tanTop - tanBottom << std::endl;
    outFrustumFile.close();

    //test export mesh
    std::string outMeshFilename = baseOutFilename + "_mesh" + std::string(".csv");
    sgct::MessageHandler::instance()->print(sgct::MessageHandler::NOTIFY_DEBUG,
        "CorrectionMesh: Exporting dome projection mesh file \"%s\"\n", outMeshFilename.c_str());
    std::ofstream outMeshFile;
    outMeshFile.open(outMeshFilename, std::ios::out);
    outMeshFile << "x;y;u;v;column;row" << std::endl;
    outMeshFile << std::fixed;
    outMeshFile << std::setprecision(6);

    unsigned int i = 0;
    for (unsigned int y = 0; y < size[1]; ++y)
        for (unsigned int x = 0; x < size[0]; ++x)
        {
            outMeshFile << texturedVertexList[i].x << ";";
            outMeshFile << texturedVertexList[i].y << ";";
            outMeshFile << texturedVertexList[i].tx << ";";
            outMeshFile << (1.0f - texturedVertexList[i].ty) << ";"; //flip v-coord
            outMeshFile << x << ";";
            outMeshFile << y << std::endl;
            ++i;
        }
    outMeshFile.close();
#endif

    mGeometries[WARP_MESH].mNumberOfVertices = numberOfVertices;
    mGeometries[WARP_MESH].mNumberOfIndices = numberOfIndices;
    
    if (fileVersion == '2' && size[0] == 4) {
        mGeometries[WARP_MESH].mGeometryType = GL_TRIANGLES;
    }
    else if (fileVersion == '2' && size[0] == 5) {
        mGeometries[WARP_MESH].mGeometryType = GL_TRIANGLE_STRIP;
    }
    else { // assume v1
        //GL_QUAD_STRIP removed in OpenGL 3.3+
        //mGeometries[WARP_MESH].mGeometryType = GL_QUAD_STRIP;
        mGeometries[WARP_MESH].mGeometryType = GL_TRIANGLE_STRIP;
    }

    //clean up
    delete [] texturedVertexList;
    texturedVertexList = NULL;

    createMesh(&mGeometries[WARP_MESH]);

    sgct::MessageHandler::instance()->print(sgct::MessageHandler::NOTIFY_DEBUG, "CorrectionMesh: Correction mesh read successfully! Vertices=%u, Indices=%u.\n", numberOfVertices, numberOfIndices);
    
    return true;
}

bool sgct_core::CorrectionMesh::readAndGenerateSimCADMesh(const std::string & meshPath, sgct_core::Viewport * parent)
{
    /*During projector alignment, a 33x33 matrix is used.
     This means 33x33 points can be set to define geometry correction.
     So(x, y) coordinates are defined by the 33x33 matrix and the resolution used, defined by the tag.
     And the corrections to be applied for every point in that 33x33 matrix, are stored in the warp file.
     This explains why this file only contains zero�s when no warp is applied.*/

    sgct::MessageHandler::instance()->print(sgct::MessageHandler::NOTIFY_INFO,
        "CorrectionMesh: Reading simcad warp data from '%s'.\n", meshPath.c_str());

    float xrange = 1.0f;
    float yrange = 1.0f;
    std::vector<float> xcorrections, ycorrections;

    tinyxml2::XMLDocument xmlDoc;
    if (xmlDoc.LoadFile(meshPath.c_str()) != tinyxml2::XML_NO_ERROR)
    {
        std::stringstream ss;
        if (xmlDoc.GetErrorStr1() && xmlDoc.GetErrorStr2())
            ss << "Parsing failed after: " << xmlDoc.GetErrorStr1() << " " << xmlDoc.GetErrorStr2();
        else if (xmlDoc.GetErrorStr1())
            ss << "Parsing failed after: " << xmlDoc.GetErrorStr1();
        else if (xmlDoc.GetErrorStr2())
            ss << "Parsing failed after: " << xmlDoc.GetErrorStr2();
        else
            ss << "File not found";
        sgct::MessageHandler::instance()->print(sgct::MessageHandler::NOTIFY_ERROR, "ReadConfig: Error occured while reading config file '%s'\nError: %s\n", meshPath.c_str(), ss.str().c_str());
        return false;
    }
    else {
        tinyxml2::XMLElement* XMLroot = xmlDoc.FirstChildElement("GeometryFile");
        if (XMLroot == NULL)
        {
            sgct::MessageHandler::instance()->print(sgct::MessageHandler::NOTIFY_ERROR, "ReadConfig: Error occured while reading config file '%s'\nError: %s\n", meshPath.c_str(), "Cannot find XML root!");
            return false;
        }

        tinyxml2::XMLElement* element[MAX_XML_DEPTH];
        const char * val[MAX_XML_DEPTH];
        element[0] = XMLroot->FirstChildElement();
        if (element[0] != NULL)
        {
            val[0] = element[0]->Value();

            if (strcmp("GeometryDefinition", val[0]) == 0)
            {
                element[1] = element[0]->FirstChildElement();
                while (element[1] != NULL)
                {
                    val[1] = element[1]->Value();
                    
                    if (strcmp("X-FlatParameters", val[1]) == 0)
                    { 
                        if (element[1]->QueryFloatAttribute("range", &xrange) == tinyxml2::XML_NO_ERROR)
                        {
                            std::string xcoordstr(element[1]->GetText());
                            std::vector<std::string> xcoords = sgct_helpers::split(xcoordstr, ' ');
                            for (auto &x : xcoords)
                            {
                                xcorrections.push_back(std::stof(x)/xrange);
                            } 
                        }
                    }
                    else if (strcmp("Y-FlatParameters", val[1]) == 0)
                    {
                        if (element[1]->QueryFloatAttribute("range", &yrange) == tinyxml2::XML_NO_ERROR)
                        {
                            std::string ycoordstr(element[1]->GetText());
                            std::vector<std::string> ycoords = sgct_helpers::split(ycoordstr, ' ');
                            for (auto &y : ycoords)
                            {
                                ycorrections.push_back(std::stof(y)/yrange);
                            }
                        }
                    }

                    //iterate
                    element[1] = element[1]->NextSiblingElement();
                }
            }
        }
    }

    if (xcorrections.size() != ycorrections.size())
    {
        sgct::MessageHandler::instance()->print(sgct::MessageHandler::NOTIFY_ERROR, "CorrectionMesh: Not the same x coords as y coords!\n");
        return false;
    }

    float numberOfColsf = sqrtf(static_cast<float>(xcorrections.size()));
    float numberOfRowsf = sqrtf(static_cast<float>(ycorrections.size()));

    if (ceilf(numberOfColsf) != numberOfColsf || ceilf(numberOfRowsf) != numberOfRowsf)
    {
        sgct::MessageHandler::instance()->print(sgct::MessageHandler::NOTIFY_ERROR, "CorrectionMesh: Not a valid squared matrix read from SimCAD file!\n");
        return false;
    }

    unsigned int numberOfCols = static_cast<unsigned int>(numberOfColsf);
    unsigned int numberOfRows = static_cast<unsigned int>(numberOfRowsf);


#if CONVERT_SIMCAD_TO_DOMEPROJECTION_AND_SGC
    //export to dome projection
    std::string baseOutFilename = meshPath.substr(0, meshPath.find_last_of(".simcad") - 6);

    //export domeprojection frustum
    std::string outFrustumFilename = baseOutFilename + "_frustum" + std::string(".csv");
    sgct::MessageHandler::instance()->print(sgct::MessageHandler::NOTIFY_DEBUG,
        "CorrectionMesh: Exporting dome projection frustum file \"%s\"\n", outFrustumFilename.c_str());
    std::ofstream outFrustumFile;
    outFrustumFile.open(outFrustumFilename, std::ios::out);
    outFrustumFile << "x;y;z;heading;pitch;bank;left;right;bottom;top;tanLeft;tanRight;tanBottom;tanTop;width;height" << std::endl;
    outFrustumFile << std::fixed;
    outFrustumFile << std::setprecision(8);

    //write viewdata
    SCISSViewData viewData;

    glm::quat rotation = parent->getRotation();
    viewData.qw = rotation.w;
    viewData.qx = rotation.x;
    viewData.qy = rotation.y;
    viewData.qz = rotation.z;

    glm::vec3 position = parent->getUser()->getPos();
    viewData.x = position.x;
    viewData.y = position.y;
    viewData.z = position.z;

    glm::vec4 fov = parent->getFOV();
    viewData.fovUp = fov.x;
    viewData.fovDown = -fov.y;
    viewData.fovLeft = -fov.z;
    viewData.fovRight = fov.w;

    double yaw, pitch, roll;
    glm::dvec3 angles = glm::degrees(glm::eulerAngles(glm::dquat(static_cast<double>(viewData.qw), static_cast<double>(viewData.qy), static_cast<double>(viewData.qx), static_cast<double>(viewData.qz))));
    yaw = -angles.x;
    pitch = angles.y;
    roll = -angles.z;

    sgct::MessageHandler::instance()->print(sgct::MessageHandler::NOTIFY_DEBUG, "CorrectionMesh: Rotation quat = [%f %f %f %f]\nyaw = %lf, pitch = %lf, roll = %lf\n",
        viewData.qx, viewData.qy, viewData.qz, viewData.qw,
        yaw, pitch, roll);

    sgct::MessageHandler::instance()->print(sgct::MessageHandler::NOTIFY_DEBUG, "CorrectionMesh: Position = [%f %f %f]\n",
        viewData.x, viewData.y, viewData.z);

    sgct::MessageHandler::instance()->print(sgct::MessageHandler::NOTIFY_DEBUG, "CorrectionMesh: FOV up = %f\n",
        viewData.fovUp);

    sgct::MessageHandler::instance()->print(sgct::MessageHandler::NOTIFY_DEBUG, "CorrectionMesh: FOV down = %f\n",
        viewData.fovDown);

    sgct::MessageHandler::instance()->print(sgct::MessageHandler::NOTIFY_DEBUG, "CorrectionMesh: FOV left = %f\n",
        viewData.fovLeft);

    sgct::MessageHandler::instance()->print(sgct::MessageHandler::NOTIFY_DEBUG, "CorrectionMesh: FOV right = %f\n",
        viewData.fovRight);


    outFrustumFile << viewData.x << ";" << viewData.y << ";" << viewData.z << ";"; //x y z
    outFrustumFile << yaw << ";" << pitch << ";" << roll << ";";
    outFrustumFile << viewData.fovLeft << ";" << viewData.fovRight << ";" << viewData.fovDown << ";" << viewData.fovUp << ";";
    float tanLeft = tan(glm::radians(viewData.fovLeft));
    float tanRight = tan(glm::radians(viewData.fovRight));
    float tanBottom = tan(glm::radians(viewData.fovDown));
    float tanTop = tan(glm::radians(viewData.fovUp));
    outFrustumFile << tanLeft << ";" << tanRight << ";" << tanBottom << ";" << tanTop << ";";
    outFrustumFile << tanRight - tanLeft << ";";
    outFrustumFile << tanTop - tanBottom << std::endl;
    outFrustumFile.close();

    //start sgc export
    std::string outSGCFilenames[2] = { baseOutFilename + std::string("_u2.sgc"), baseOutFilename + std::string("_u3.sgc") };
    sgct::MessageHandler::instance()->print(sgct::MessageHandler::NOTIFY_DEBUG,
        "CorrectionMesh: Exporting sgc v1(u2) file \"%s\" and v2(u3) file \"%s\"\n", outSGCFilenames[0].c_str(), outSGCFilenames[1].c_str());
    std::ofstream outSGCFiles[2];
    outSGCFiles[0].open(outSGCFilenames[0], std::ios::out | std::ios::binary);
    outSGCFiles[1].open(outSGCFilenames[1], std::ios::out | std::ios::binary);

    outSGCFiles[0] << 'S' << 'G' << 'C';
    outSGCFiles[1] << 'S' << 'G' << 'C';

    uint8_t SGCversion1 = 1;
    uint8_t SGCversion2 = 2;

    SCISSDistortionType dT = MESHTYPE_PLANAR;
    unsigned int vertexCount = numberOfCols*numberOfRows;
    unsigned int primType = 5; //Triangle list

    viewData.fovLeft = -viewData.fovLeft;
    viewData.fovDown = -viewData.fovDown;
    
    outSGCFiles[0].write(reinterpret_cast<const char *>(&SGCversion1), sizeof(uint8_t));
    outSGCFiles[0].write(reinterpret_cast<const char *>(&dT), sizeof(SCISSDistortionType));
    outSGCFiles[0].write(reinterpret_cast<const char *>(&viewData), sizeof(SCISSViewData));
    outSGCFiles[0].write(reinterpret_cast<const char *>(&numberOfCols), sizeof(unsigned int));
    outSGCFiles[0].write(reinterpret_cast<const char *>(&numberOfRows), sizeof(unsigned int));

    outSGCFiles[1].write(reinterpret_cast<const char *>(&SGCversion2), sizeof(uint8_t));
    outSGCFiles[1].write(reinterpret_cast<const char *>(&dT), sizeof(SCISSDistortionType));
    outSGCFiles[1].write(reinterpret_cast<const char *>(&viewData), sizeof(SCISSViewData));
    outSGCFiles[1].write(reinterpret_cast<const char *>(&primType), sizeof(unsigned int));
    outSGCFiles[1].write(reinterpret_cast<const char *>(&vertexCount), sizeof(unsigned int));

    //test export mesh
    std::string outMeshFilename = baseOutFilename + "_mesh" + std::string(".csv");
    sgct::MessageHandler::instance()->print(sgct::MessageHandler::NOTIFY_DEBUG,
        "CorrectionMesh: Exporting dome projection mesh file \"%s\"\n", outMeshFilename.c_str());
    std::ofstream outMeshFile;
    outMeshFile.open(outMeshFilename, std::ios::out);
    outMeshFile << "x;y;u;v;column;row" << std::endl;
    outMeshFile << std::fixed;
    outMeshFile << std::setprecision(6);
#endif

    CorrectionMeshVertex vertex;
    std::vector<CorrectionMeshVertex> vertices;
    SCISSTexturedVertex scissVertex;

    //init to max intensity (opaque white)
    vertex.r = 1.0f;
    vertex.g = 1.0f;
    vertex.b = 1.0f;
    vertex.a = 1.0f;

    float x, y, u, v;
    size_t i = 0;

    for (unsigned int r = 0; r < numberOfRows; r++)
    {
        for (unsigned int c = 0; c < numberOfCols; c++)
        {
            //vertex-mapping
            u = (static_cast<float>(c) / (static_cast<float>(numberOfCols) - 1.f));
            v = 1.f - (static_cast<float>(r) / (static_cast<float>(numberOfRows) - 1.f));

            x = u + xcorrections[i];
            y = v - ycorrections[i];

            //convert to [-1, 1]
            vertex.x = 2.0f * (x * parent->getXSize() + parent->getX()) - 1.0f;
            vertex.y = 2.0f * (y * parent->getYSize() + parent->getY()) - 1.0f;

            //scale to viewport coordinates
            vertex.s = u * parent->getXSize() + parent->getX();
            vertex.t = v * parent->getYSize() + parent->getY();

            vertices.push_back(vertex);

            i++;

#if CONVERT_SIMCAD_TO_DOMEPROJECTION_AND_SGC
            outMeshFile << x << ";";
            outMeshFile << 1.0f - y << ";";
            outMeshFile << u << ";";
            outMeshFile << 1.0f - v << ";";
            outMeshFile << c << ";";
            outMeshFile << r << std::endl;

            scissVertex.x = x;
            scissVertex.y = 1.f - y;
            scissVertex.tx = u;
            scissVertex.ty = v;

            outSGCFiles[0].write(reinterpret_cast<const char *>(&scissVertex), sizeof(SCISSTexturedVertex));
            outSGCFiles[1].write(reinterpret_cast<const char *>(&scissVertex), sizeof(SCISSTexturedVertex));
#endif
        }

    }

    //copy vertices
    unsigned int numberOfVertices = numberOfCols * numberOfRows;
    mTempVertices = new CorrectionMeshVertex[numberOfVertices];
    memcpy(mTempVertices, vertices.data(), numberOfVertices * sizeof(CorrectionMeshVertex));
    mGeometries[WARP_MESH].mNumberOfVertices = numberOfVertices;
    vertices.clear();

    // Make a triangle strip index list
    std::vector<unsigned int> indices_trilist;
    for (unsigned int r = 0; r<numberOfRows - 1; r++) {
        if ((r & 1) == 0) { // even rows
            for (unsigned int c = 0; c < numberOfCols; c++) {
                indices_trilist.push_back(c + r * numberOfCols);
                indices_trilist.push_back(c + (r + 1) * numberOfCols);
            }
        }
        else { // odd rows
            for (unsigned int c = numberOfCols - 1; c>0; c--) {
                indices_trilist.push_back(c + (r + 1) * numberOfCols);
                indices_trilist.push_back(c - 1 + r * numberOfCols);
            }
        }
    }

#if CONVERT_SIMCAD_TO_DOMEPROJECTION_AND_SGC
    // Implementation of individual triangle index list
    /*std::vector<unsigned int> indices_tris;
    unsigned int i0, i1, i2, i3;
    for (unsigned int c = 0; c < (numberOfCols - 1); c++)
    {
        for (unsigned int r = 0; r < (numberOfRows - 1); r++)
        {
            i0 = r * numberOfCols + c;
            i1 = r * numberOfCols + (c + 1);
            i2 = (r + 1) * numberOfCols + (c + 1);
            i3 = (r + 1) * numberOfCols + c;

            //triangle 1
            indices_tris.push_back(i0);
            indices_tris.push_back(i1);
            indices_tris.push_back(i2);

            //triangle 2
            indices_tris.push_back(i0);
            indices_tris.push_back(i2);
            indices_tris.push_back(i3);
        }
    }*/

    outMeshFile.close();

    unsigned int indicesCount = static_cast<unsigned int>(indices_trilist.size());
    outSGCFiles[0].write(reinterpret_cast<const char *>(&indicesCount), sizeof(unsigned int));
    outSGCFiles[1].write(reinterpret_cast<const char *>(&indicesCount), sizeof(unsigned int));
    for (size_t i = 0; i < indices_trilist.size(); i++) {
        outSGCFiles[0].write(reinterpret_cast<const char *>(&indices_trilist[i]), sizeof(unsigned int));
        outSGCFiles[1].write(reinterpret_cast<const char *>(&indices_trilist[i]), sizeof(unsigned int));
    }

    /*indicesCount = static_cast<unsigned int>(indices_tris.size());
    outSGCFiles[1].write(reinterpret_cast<const char *>(&indicesCount), sizeof(unsigned int));
    for (size_t i = 0; i < indices_tris.size(); i++) {
        outSGCFiles[1].write(reinterpret_cast<const char *>(&indices_tris[i]), sizeof(unsigned int));
    }
    indices_tris.clear();*/

    outSGCFiles[0].close();
    outSGCFiles[1].close();
#endif

    //allocate and copy indices
    mGeometries[WARP_MESH].mNumberOfIndices = static_cast<unsigned int>(indices_trilist.size());
    mTempIndices = new unsigned int[mGeometries[WARP_MESH].mNumberOfIndices];
    memcpy(mTempIndices, indices_trilist.data(), mGeometries[WARP_MESH].mNumberOfIndices * sizeof(unsigned int));
    indices_trilist.clear();

    mGeometries[WARP_MESH].mGeometryType = GL_TRIANGLE_STRIP;

    createMesh(&mGeometries[WARP_MESH]);

    sgct::MessageHandler::instance()->print(sgct::MessageHandler::NOTIFY_DEBUG, "CorrectionMesh: Correction mesh read successfully! Vertices=%u, Indices=%u.\n", mGeometries[WARP_MESH].mNumberOfVertices, mGeometries[WARP_MESH].mNumberOfIndices);

    return true;
}

bool sgct_core::CorrectionMesh::readAndGenerateSkySkanMesh(const std::string & meshPath, Viewport * parent)
{
    sgct::MessageHandler::instance()->print(sgct::MessageHandler::NOTIFY_INFO,
        "CorrectionMesh: Reading SkySkan mesh data from '%s'.\n", meshPath.c_str());

    FILE * meshFile = NULL;
#if (_MSC_VER >= 1400) //visual studio 2005 or later
    if (fopen_s(&meshFile, meshPath.c_str(), "r") != 0 || !meshFile)
    {
        sgct::MessageHandler::instance()->print(sgct::MessageHandler::NOTIFY_ERROR, "CorrectionMesh: Failed to open warping mesh file!\n");
        return false;
    }
#else
    meshFile = fopen(meshPath.c_str(), "r");
    if (meshFile == NULL)
    {
        sgct::MessageHandler::instance()->print(sgct::MessageHandler::NOTIFY_ERROR, "CorrectionMesh: Failed to open warping mesh file!\n");
        return false;
    }
#endif

    float azimuth = 0.0f;
    float elevation = 0.0f;
    float horizontal_fov = 0.0f;
    float vertical_fov = 0.0f;
    float fovTweaks[2] = { 1.0f, 1.0f };
    float UVTweaks[2] = { 1.0f, 1.0f };
    bool dimensionsSet = false;
    bool azimuthSet = false;
    bool elevationSet = false;
    bool hFovSet = false;
    bool vFovSet = false;
    float x, y, u, v;

    unsigned int size[2];
    unsigned int counter = 0;

    char lineBuffer[MAX_LINE_LENGTH];
    while (!feof(meshFile))
    {
        if (fgets(lineBuffer, MAX_LINE_LENGTH, meshFile) != NULL)
        {

#if (_MSC_VER >= 1400) //visual studio 2005 or later
            if (sscanf_s(lineBuffer, "Dome Azimuth=%f", &azimuth) == 1)
#else
            if (sscanf(lineBuffer, "Dome Azimuth=%f", &azimuth) == 1)
#endif
            {
                azimuthSet = true;
            }

#if (_MSC_VER >= 1400) //visual studio 2005 or later
            else if (sscanf_s(lineBuffer, "Dome Elevation=%f", &elevation) == 1)
#else
            else if (sscanf(lineBuffer, "Dome Elevation=%f", &elevation) == 1)
#endif
            {
                elevationSet = true;
            }

#if (_MSC_VER >= 1400) //visual studio 2005 or later
            else if (sscanf_s(lineBuffer, "Horizontal FOV=%f", &horizontal_fov) == 1)
#else
            else if (sscanf(lineBuffer, "Horizontal FOV=%f", &horizontal_fov) == 1)
#endif
            {
                hFovSet = true;
            }

#if (_MSC_VER >= 1400) //visual studio 2005 or later
            else if (sscanf_s(lineBuffer, "Vertical FOV=%f", &vertical_fov) == 1)
#else
            else if (sscanf(lineBuffer, "Vertical FOV=%f", &vertical_fov) == 1)
#endif
            {
                vFovSet = true;
            }

#if (_MSC_VER >= 1400) //visual studio 2005 or later
            else if (sscanf_s(lineBuffer, "Horizontal Tweak=%f", &fovTweaks[0]) == 1)
#else
            else if (sscanf(lineBuffer, "Horizontal Tweak=%f", &fovTweaks[0]) == 1)
#endif
            {
                ;
            }

#if (_MSC_VER >= 1400) //visual studio 2005 or later
            else if (sscanf_s(lineBuffer, "Vertical Tweak=%f", &fovTweaks[1]) == 1)
#else
            else if (sscanf(lineBuffer, "Vertical Tweak=%f", &fovTweaks[1]) == 1)
#endif
            {
                ;
            }

#if (_MSC_VER >= 1400) //visual studio 2005 or later
            else if (sscanf_s(lineBuffer, "U Tweak=%f", &UVTweaks[0]) == 1)
#else
            else if (sscanf(lineBuffer, "U Tweak=%f", &UVTweaks[0]) == 1)
#endif
            {
                ;
            }

#if (_MSC_VER >= 1400) //visual studio 2005 or later
            else if (sscanf_s(lineBuffer, "V Tweak=%f", &UVTweaks[1]) == 1)
#else
            else if (sscanf(lineBuffer, "V Tweak=%f", &UVTweaks[1]) == 1)
#endif
            {
                ;
            }

#if (_MSC_VER >= 1400) //visual studio 2005 or later
            else if (!dimensionsSet && sscanf_s(lineBuffer, "%u %u", &size[0], &size[1]) == 2)
#else
            else if (!dimensionsSet && sscanf(lineBuffer, "%u %u", &size[0], &size[1]) == 2)
#endif
            {
                dimensionsSet = true;
                mTempVertices = new CorrectionMeshVertex[size[0] * size[1]];
                mGeometries[WARP_MESH].mNumberOfVertices = size[0] * size[1];
            }

#if (_MSC_VER >= 1400) //visual studio 2005 or later
            else if (dimensionsSet && sscanf_s(lineBuffer, "%f %f %f %f", &x, &y, &u, &v) == 4)
#else
            else if (dimensionsSet && sscanf(lineBuffer, "%f %f %f %f", &x, &y, &u, &v) == 4)
#endif
            {
                if (UVTweaks[0] > -1.0f)
                    u *= UVTweaks[0];

                if (UVTweaks[1] > -1.0f)
                    v *= UVTweaks[1];
                
                mTempVertices[counter].x = x;
                mTempVertices[counter].y = y;
                mTempVertices[counter].s = u;
                //mTempVertices[counter].t = v;
                //mTempVertices[counter].s = 1.0f - u;
                mTempVertices[counter].t = 1.0f - v;

                mTempVertices[counter].r = 1.0f;
                mTempVertices[counter].g = 1.0f;
                mTempVertices[counter].b = 1.0f;
                mTempVertices[counter].a = 1.0f;

                //fprintf(stderr, "Adding vertex: %u %.3f %.3f %.3f %.3f\n", counter, x, y, u, v);

                counter++;
            }

            //fprintf(stderr, "Row text: %s", lineBuffer);
        }

    }

    fclose(meshFile);

    if (!dimensionsSet ||
        !azimuthSet ||
        !elevationSet ||
        !hFovSet ||
        horizontal_fov <= 0.0f)
    {
        sgct::MessageHandler::instance()->print(sgct::MessageHandler::NOTIFY_ERROR, "CorrectionMesh: Data reading error!\n");
        return false;
    }

    //create frustums and projection matrices
    if (!vFovSet || vertical_fov <= 0.0f)
    {
        //half the width (radius is one unit, cancels it self out)
        float hw = tanf(glm::radians<float>(horizontal_fov) / 2.0f);
        //half height
        float hh = (1200.0f / 2048.0f) * hw;
        
        vertical_fov = 2.0f * glm::degrees<float>(atanf(hh));

        fprintf(stderr, "HFOV: %f VFOV: %f\n", horizontal_fov, vertical_fov);
        
        //vertical_fov = (1200.0f / 2048.0f) * horizontal_fov;
        //vertical_fov = horizontal_fov;
    }

    //correct fovs ??
    //horizontal_fov *= 0.948f;
    //vertical_fov *= 1.51f;

    if (fovTweaks[0] > 0.0f)
        horizontal_fov *= fovTweaks[0];
    if (fovTweaks[1] > 0.0f)
        vertical_fov *= fovTweaks[1];

    glm::quat rotQuat;
    rotQuat = glm::rotate(rotQuat, glm::radians(-azimuth), glm::vec3(0.0f, 1.0f, 0.0f));
    rotQuat = glm::rotate(rotQuat, glm::radians(elevation), glm::vec3(1.0f, 0.0f, 0.0f));

    parent->getUser()->setPos(0.0f, 0.0f, 0.0f);
    parent->setViewPlaneCoordsUsingFOVs(
        vertical_fov / 2.0f,
        -vertical_fov / 2.0f,
        -horizontal_fov / 2.0f,
        horizontal_fov / 2.0f,
        rotQuat
        );
    
    sgct::Engine::instance()->updateFrustums();

    std::vector<unsigned int> indices;
    unsigned int i0, i1, i2, i3;
    for (unsigned int c = 0; c < (size[0]-1); c++)
        for (unsigned int r = 0; r < (size[1]-1); r++)
        {
            i0 = r * size[0] + c;
            i1 = r * size[0] + (c + 1);
            i2 = (r + 1) * size[0] + (c + 1);
            i3 = (r + 1) * size[0] + c;

            //fprintf(stderr, "Indexes: %u %u %u %u\n", i0, i1, i2, i3);

            /*

            3      2
             x____x
             |   /|
             |  / |
             | /  |
             |/   |
             x----x
            0      1
            
            */

            //triangle 1
            if (mTempVertices[i0].x != -1.0f && mTempVertices[i0].y != -1.0f &&
                mTempVertices[i1].x != -1.0f && mTempVertices[i1].y != -1.0f &&
                mTempVertices[i2].x != -1.0f && mTempVertices[i2].y != -1.0f)
            {
                indices.push_back(i0);
                indices.push_back(i1);
                indices.push_back(i2);
            }

            //triangle 2
            if (mTempVertices[i0].x != -1.0f && mTempVertices[i0].y != -1.0f &&
                mTempVertices[i2].x != -1.0f && mTempVertices[i2].y != -1.0f &&
                mTempVertices[i3].x != -1.0f && mTempVertices[i3].y != -1.0f)
            {
                indices.push_back(i0);
                indices.push_back(i2);
                indices.push_back(i3);
            }
        }

    for (unsigned int i = 0; i < mGeometries[WARP_MESH].mNumberOfVertices; i++)
    {
        //clamp
        /*if (mTempVertices[i].x > 1.0f)
            mTempVertices[i].x = 1.0f;
        else if (mTempVertices[i].x < 0.0f)
            mTempVertices[i].x = 0.0f;

        if (mTempVertices[i].y > 1.0f)
            mTempVertices[i].y = 1.0f;
        else if (mTempVertices[i].y < 0.0f)
            mTempVertices[i].y = 0.0f;

        if (mTempVertices[i].s > 1.0f)
            mTempVertices[i].s = 1.0f;
        else if (mTempVertices[i].s < 0.0f)
            mTempVertices[i].s = 0.0f;

        if (mTempVertices[i].t > 1.0f)
            mTempVertices[i].t = 1.0f;
        else if (mTempVertices[i].t < 0.0f)
            mTempVertices[i].t = 0.0f;*/

        //convert to [-1, 1]
        mTempVertices[i].x = 2.0f*(mTempVertices[i].x * parent->getXSize() + parent->getX()) - 1.0f;
        //mTempVertices[i].x = 2.0f*((1.0f - mTempVertices[i].x) * parent->getXSize() + parent->getX()) - 1.0f;
        //mTempVertices[i].y = 2.0f*(mTempVertices[i].y * parent->getYSize() + parent->getY()) - 1.0f;
        mTempVertices[i].y = 2.0f*((1.0f - mTempVertices[i].y) * parent->getYSize() + parent->getY()) - 1.0f;
        //test code
        //mTempVertices[i].x /= 1.5f;
        //mTempVertices[i].y /= 1.5f;

        mTempVertices[i].s = mTempVertices[i].s * parent->getXSize() + parent->getX();
        mTempVertices[i].t = mTempVertices[i].t * parent->getYSize() + parent->getY();
    }

    //allocate and copy indices
    mGeometries[WARP_MESH].mNumberOfIndices = static_cast<unsigned int>(indices.size());
    mTempIndices = new unsigned int[mGeometries[WARP_MESH].mNumberOfIndices];
    memcpy(mTempIndices, indices.data(), mGeometries[WARP_MESH].mNumberOfIndices * sizeof(unsigned int));

    mGeometries[WARP_MESH].mGeometryType = GL_TRIANGLES;

    createMesh(&mGeometries[WARP_MESH]);

    sgct::MessageHandler::instance()->print(sgct::MessageHandler::NOTIFY_DEBUG, "CorrectionMesh: Correction mesh read successfully! Vertices=%u, Indices=%u.\n", mGeometries[WARP_MESH].mNumberOfVertices, mGeometries[WARP_MESH].mNumberOfIndices);

    return true;
}

bool sgct_core::CorrectionMesh::readAndGeneratePaulBourkeMesh(const std::string & meshPath, Viewport * parent)
{
    sgct::MessageHandler::instance()->print(sgct::MessageHandler::NOTIFY_INFO,
        "CorrectionMesh: Reading Paul Bourke spherical mirror mesh data from '%s'.\n", meshPath.c_str());

    FILE * meshFile = NULL;
#if (_MSC_VER >= 1400) //visual studio 2005 or later
    if (fopen_s(&meshFile, meshPath.c_str(), "r") != 0 || !meshFile)
    {
        sgct::MessageHandler::instance()->print(sgct::MessageHandler::NOTIFY_ERROR, "CorrectionMesh: Failed to open warping mesh file!\n");
        return false;
    }
#else
    meshFile = fopen(meshPath.c_str(), "r");
    if (meshFile == NULL)
    {
        sgct::MessageHandler::instance()->print(sgct::MessageHandler::NOTIFY_ERROR, "CorrectionMesh: Failed to open warping mesh file!\n");
        return false;
    }
#endif

    //variables
    int mappingType = -1;
    int size[2] = {-1, -1};
    unsigned int counter = 0;
    float x, y, s, t, intensity;
    char lineBuffer[MAX_LINE_LENGTH];

    //get the fist line containing the mapping type id
    if (fgets(lineBuffer, MAX_LINE_LENGTH, meshFile) != NULL)
    {
        int tmpi;
        if(_sscanf(lineBuffer, "%d", &tmpi) == 1)
            mappingType = tmpi;
    }

    //get the mesh dimensions
    if (fgets(lineBuffer, MAX_LINE_LENGTH, meshFile) != NULL)
    {
        if (_sscanf(lineBuffer, "%d %d", &size[0], &size[1]) == 2)
        {
            mTempVertices = new CorrectionMeshVertex[size[0] * size[1]];
            mGeometries[WARP_MESH].mNumberOfVertices = static_cast<unsigned int>(size[0] * size[1]);
        }
    }

    //check if everyting useful is set
    if (mappingType == -1 || size[0] == -1 || size[1] == -1)
    {
        sgct::MessageHandler::instance()->print(sgct::MessageHandler::NOTIFY_ERROR, "CorrectionMesh: Invalid data");
        return false;
    }

    //get all data
    while (!feof(meshFile))
    {
        if (fgets(lineBuffer, MAX_LINE_LENGTH, meshFile) != NULL)
        {
            if (_sscanf(lineBuffer, "%f %f %f %f %f", &x, &y, &s, &t, &intensity) == 5)
            {
                mTempVertices[counter].x = x;
                mTempVertices[counter].y = y;
                mTempVertices[counter].s = s;
                mTempVertices[counter].t = t;

                mTempVertices[counter].r = intensity;
                mTempVertices[counter].g = intensity;
                mTempVertices[counter].b = intensity;
                mTempVertices[counter].a = 1.0f;

                //if(counter <= 100)
                //    fprintf(stderr, "Adding vertex: %u %.3f %.3f %.3f %.3f %.3f\n", counter, x, y, s, t, intensity);

                counter++;
            }
        }
    }

    //generate indices
    std::vector<unsigned int> indices;
    int i0, i1, i2, i3;
    for (int c = 0; c < (size[0] - 1); c++)
        for (int r = 0; r < (size[1] - 1); r++)
        {
            i0 = r * size[0] + c;
            i1 = r * size[0] + (c + 1);
            i2 = (r + 1) * size[0] + (c + 1);
            i3 = (r + 1) * size[0] + c;

            //fprintf(stderr, "Indexes: %u %u %u %u\n", i0, i1, i2, i3);

            /*

            3      2
            x____x
            |   /|
            |  / |
            | /  |
            |/   |
            x----x
            0      1

            */

            //triangle 1
            indices.push_back(i0);
            indices.push_back(i1);
            indices.push_back(i2);

            //triangle 2
            indices.push_back(i0);
            indices.push_back(i2);
            indices.push_back(i3);
        }

    float aspect = sgct::Engine::instance()->getCurrentWindowPtr()->getAspectRatio() * 
        (parent->getXSize() / parent->getYSize());
    
    for (unsigned int i = 0; i < mGeometries[WARP_MESH].mNumberOfVertices; i++)
    {
        //convert to [0, 1] (normalize)
        mTempVertices[i].x /= aspect;
        mTempVertices[i].x = (mTempVertices[i].x + 1.0f) / 2.0f;
        mTempVertices[i].y = (mTempVertices[i].y + 1.0f) / 2.0f;
        
        //scale, re-position and convert to [-1, 1]
        mTempVertices[i].x = (mTempVertices[i].x * parent->getXSize() + parent->getX()) * 2.0f - 1.0f;
        mTempVertices[i].y = (mTempVertices[i].y * parent->getYSize() + parent->getY()) * 2.0f - 1.0f;

        //convert to viewport coordinates
        mTempVertices[i].s = mTempVertices[i].s * parent->getXSize() + parent->getX();
        mTempVertices[i].t = mTempVertices[i].t * parent->getYSize() + parent->getY();
    }

    //allocate and copy indices
    mGeometries[WARP_MESH].mNumberOfIndices = static_cast<unsigned int>(indices.size());
    mTempIndices = new unsigned int[mGeometries[WARP_MESH].mNumberOfIndices];
    memcpy(mTempIndices, indices.data(), mGeometries[WARP_MESH].mNumberOfIndices * sizeof(unsigned int));

    mGeometries[WARP_MESH].mGeometryType = GL_TRIANGLES;
    createMesh(&mGeometries[WARP_MESH]);

    //force regeneration of dome render quad
    if (FisheyeProjection* fishPrj = dynamic_cast<FisheyeProjection*>(parent->getNonLinearProjectionPtr()))
    {
        fishPrj->setIgnoreAspectRatio(true);
        fishPrj->update(1.0f, 1.0f);
    }

    sgct::MessageHandler::instance()->print(sgct::MessageHandler::NOTIFY_DEBUG, "CorrectionMesh: Correction mesh read successfully! Vertices=%u, Indices=%u.\n", mGeometries[WARP_MESH].mNumberOfVertices, mGeometries[WARP_MESH].mNumberOfIndices);
    return true;
}

bool sgct_core::CorrectionMesh::readAndGenerateOBJMesh(const std::string & meshPath, Viewport * parent)
{
    sgct::MessageHandler::instance()->print(sgct::MessageHandler::NOTIFY_INFO,
        "CorrectionMesh: Reading Maya Wavefront OBJ mesh data from '%s'.\n", meshPath.c_str());

    FILE * meshFile = NULL;
#if (_MSC_VER >= 1400) //visual studio 2005 or later
    if (fopen_s(&meshFile, meshPath.c_str(), "r") != 0 || !meshFile)
    {
        sgct::MessageHandler::instance()->print(sgct::MessageHandler::NOTIFY_ERROR, "CorrectionMesh: Failed to open warping mesh file!\n");
        return false;
    }
#else
    meshFile = fopen(meshPath.c_str(), "r");
    if (meshFile == NULL)
    {
        sgct::MessageHandler::instance()->print(sgct::MessageHandler::NOTIFY_ERROR, "CorrectionMesh: Failed to open warping mesh file!\n");
        return false;
    }
#endif

    //variables
    int i0, i1, i2;
    unsigned int counter = 0;
    char lineBuffer[MAX_LINE_LENGTH];
    CorrectionMeshVertex tmpVert;
    std::vector<CorrectionMeshVertex> verts;
    std::vector<unsigned int> indices;

    //get all data
    while (!feof(meshFile))
    {
        if (fgets(lineBuffer, MAX_LINE_LENGTH, meshFile) != NULL)
        {
            if (_sscanf(lineBuffer, "v %f %f %*f", &tmpVert.x, &tmpVert.y) == 2)
            {
                tmpVert.r = 1.0f;
                tmpVert.g = 1.0f;
                tmpVert.b = 1.0f;
                tmpVert.a = 1.0f;

                verts.push_back(tmpVert);
            }
            else if (_sscanf(lineBuffer, "vt %f %f %*f", &tmpVert.s, &tmpVert.t) == 2)
            {
                if (counter < verts.size())
                {
                    verts[counter].s = tmpVert.s;
                    verts[counter].t = tmpVert.t;
                }
                
                counter++;
            }
            else if (_sscanf(lineBuffer, "f %d/%*d/%*d %d/%*d/%*d %d/%*d/%*d", &i0, &i1, &i2) == 3)
            {
                //indexes starts at 1 in OBJ
                indices.push_back(i0-1);
                indices.push_back(i1-1);
                indices.push_back(i2-1);
            }
        }
    }

    //sanity check
    if (counter != verts.size() || verts.size() == 0)
    {
        sgct::MessageHandler::instance()->print(sgct::MessageHandler::NOTIFY_ERROR, "CorrectionMesh: Vertex count doesn't match number of texture coordinates!\n");
        return false;
    }

    //allocate and copy indices
    mGeometries[WARP_MESH].mNumberOfIndices = static_cast<unsigned int>(indices.size());
    mTempIndices = new unsigned int[mGeometries[WARP_MESH].mNumberOfIndices];
    memcpy(mTempIndices, indices.data(), mGeometries[WARP_MESH].mNumberOfIndices * sizeof(unsigned int));

    mGeometries[WARP_MESH].mNumberOfVertices = static_cast<unsigned int>(verts.size());
    mTempVertices = new CorrectionMeshVertex[mGeometries[WARP_MESH].mNumberOfVertices];
    memcpy(mTempVertices, verts.data(), mGeometries[WARP_MESH].mNumberOfVertices * sizeof(CorrectionMeshVertex));

    mGeometries[WARP_MESH].mGeometryType = GL_TRIANGLES;
    createMesh(&mGeometries[WARP_MESH]);

    sgct::MessageHandler::instance()->print(sgct::MessageHandler::NOTIFY_DEBUG, "CorrectionMesh: Correction mesh read successfully! Vertices=%u, Indices=%u.\n", mGeometries[WARP_MESH].mNumberOfVertices, mGeometries[WARP_MESH].mNumberOfIndices);
    return true;
}

int numberOfDigitsInInt(int number)
{
    int i = 0;
    while (number > pow(10, i))
        i++;
    return i;
}

bool sgct_core::CorrectionMesh::readAndGenerateMpcdiMesh(const std::string & meshPath, Viewport* parent)
{
    bool isReadingFile = ( meshPath.length() > 0 ) ? true : false;
    unsigned int srcIdx = 0;
    char* srcBuff;
    size_t srcSize_bytes;
    const int MaxHeaderLineLength = 100;
    FILE * meshFile = nullptr;

    if( isReadingFile )
    {
        sgct::MessageHandler::instance()->print(sgct::MessageHandler::NOTIFY_INFO,
            "CorrectionMesh: Reading MPCDI mesh (PFM format) data from '%s'.\n", meshPath.c_str());
#if (_MSC_VER >= 1400) //visual studio 2005 or later
        //if (fopen_s(&meshFile, meshPath.c_str(), "r") != 0 || !meshFile)
		if (fopen_s(&meshFile, meshPath.c_str(), "rb") != 0 || !meshFile)
        {
            sgct::MessageHandler::instance()->print(sgct::MessageHandler::NOTIFY_ERROR,
                "CorrectionMesh: Failed to open warping mesh file!\n");
            return false;
        }
#else
        meshFile = fopen(meshPath.c_str(), "rb");
        if (meshFile == NULL)
        {
            sgct::MessageHandler::instance()->print(sgct::MessageHandler::NOTIFY_ERROR,
                "CorrectionMesh: Failed to open warping mesh file!\n");
            return false;
        }
#endif
    }
    else
    {
        sgct::MessageHandler::instance()->print(sgct::MessageHandler::NOTIFY_INFO,
            "CorrectionMesh: Reading MPCDI mesh (PFM format) from buffer.\n", meshPath.c_str());
        srcBuff = parent->mMpcdiWarpMeshData;
        srcSize_bytes = parent->mMpcdiWarpMeshSize;
    }

    size_t retval;
    char headerChar;
    char headerBuffer[MaxHeaderLineLength];
    int index = 0;
    int nNewlines = 0;
    const int read3lines = 3;

    do {
        if( isReadingFile )
        {
#if (_MSC_VER >= 1400) //visual studio 2005 or later
            retval = fread_s(&headerChar, sizeof(char)*1, sizeof(char), 1, meshFile);
#else
            retval = fread(&headerChar, sizeof(char), 1, meshFile);
#endif
        }
        else
        {
            if( srcIdx == srcSize_bytes )
                retval = -1;
            else
            {
                headerChar = srcBuff[srcIdx++];
                retval = 1;
            }
        }

        if (retval != 1) {
            sgct::MessageHandler::instance()->print(sgct::MessageHandler::NOTIFY_ERROR,
                                                    "CorrectionMesh: Error reading from file.\n");
            fclose(meshFile);
            return false;
        }

        headerBuffer[index++] = headerChar;
        if( headerChar == '\n' )
            nNewlines++;
    } while (nNewlines < read3lines);

    char fileFormatHeader[2];
    unsigned int numberOfCols = 0;
    unsigned int numberOfRows = 0;
    float endiannessIndicator = 0;

#ifdef __WIN32__
    _sscanf(&headerBuffer[0], "%2c\n", &fileFormatHeader, static_cast<unsigned int>(sizeof(fileFormatHeader)));
    //Read header past the 2 character start
    _sscanf(&headerBuffer[3], "%d %d\n", &numberOfCols, &numberOfRows);
    int indexForEndianness = 3 + numberOfDigitsInInt(numberOfCols)
        + numberOfDigitsInInt(numberOfRows) + 2;
    _sscanf(&headerBuffer[indexForEndianness], "%f\n", &endiannessIndicator);
#else
    if (_sscanf(headerBuffer, "%2c %d %d %f", fileFormatHeader,
                &numberOfCols, &numberOfRows, &endiannessIndicator) != 4)
    {
        sgct::MessageHandler::instance()->print(sgct::MessageHandler::NOTIFY_ERROR,
            "CorrectionMesh: Invalid header syntax.\n");
        if (isReadingFile)
            fclose(meshFile);
        return false;
    }
#endif

    if (fileFormatHeader[0] != 'P' || fileFormatHeader[1] != 'F') {
        //The 'Pf' header is invalid because PFM grayscale type is not supported.
        sgct::MessageHandler::instance()->print(sgct::MessageHandler::NOTIFY_ERROR,
                                                "CorrectionMesh: Incorrect file type.\n");
    }

    int numCorrectionValues = numberOfCols * numberOfRows;
	
	float* allFloats = new float[3 * numCorrectionValues];
    float* correctionGridX = new float[numCorrectionValues];
    float* correctionGridY = new float[numCorrectionValues];
    //float  errorPosition;
	float * errorValues = new float[numCorrectionValues];

	memset(allFloats,       0, 3 * sizeof(float) * numCorrectionValues);
	memset(correctionGridX, 1, 1 * sizeof(float) * numCorrectionValues);
	memset(correctionGridY, 2, 1 * sizeof(float) * numCorrectionValues);
	memset(errorValues,     0, 1 * sizeof(float) * numCorrectionValues);


    const int value32bit = 4;

    if( isReadingFile )
    {
		fread_s(
			static_cast<void*>(allFloats), 
			3 * sizeof(float) * numCorrectionValues, 
			sizeof(float), 
			3* numCorrectionValues, 
			meshFile);

		for (size_t i = 0; i < numCorrectionValues; i++)
		{
			//distribute raw read
			correctionGridX[i] = allFloats[3 * i + 0];
			correctionGridY[i] = allFloats[3 * i + 1];
			errorValues    [i] = allFloats[3 * i + 2];
		}


		//for some reasons I don't understnad, this code stops fuilling the buffer after the four'th float value...
//#if (_MSC_VER >= 1400) //visual studio 2005 or later
// #define SGCT_READ fread_s
//#else
// #define SGCT_READ fread
//#endif
//        for (int i = 0; i < numCorrectionValues; ++i)
//        {
//
//
//#ifdef __WIN32__ 
//            retval = SGCT_READ(correctionGridX + i, sizeof(float), sizeof(float), 1, meshFile);
//            retval = SGCT_READ(correctionGridY + i, sizeof(float), sizeof(float), 1, meshFile);
//            //MPCDI uses the PFM format for correction grid. PFM format is designed for 3 RGB
//            // values. However MPCDI substitutes Red for X correction, Green for Y
//            // correction, and Blue for correction error. This will be NaN for no error value
//            retval = SGCT_READ(&errorPosition, sizeof(float), sizeof(float), 1, meshFile);
//
//#else
//            retval = SGCT_READ(correctionGridX + i, value32bit, 1, meshFile);
//            retval = SGCT_READ(correctionGridY + i, value32bit, 1, meshFile);
//            retval = SGCT_READ(&errorPosition,      value32bit, 1, meshFile);
//#endif
//
//
//
//        }

        fclose(meshFile);
        
		// old code, not working on windows 10 with VS2019
		// there is not "4" expectedt as return value, but 1 or zero, depending on error or EOGF:
		// https://docs.microsoft.com/en-us/cpp/c-runtime-library/reference/fread-s?view=vs-2019#return-value
		// I don't know about the other platforms, so just doing a HACK here to also allow 0 and 1 as return value
		//if (retval != value32bit)
		if ( ! (
			(retval == value32bit)
			||
			(retval == 0)
			||
			(retval == 1)
			)
		)
        {
            sgct::MessageHandler::instance()->print(sgct::MessageHandler::NOTIFY_ERROR,
                "CorrectionMesh: Error reading all correction values!\n");
            return false;
        }

    }
    else // read from local object
    {
        bool readErr = false;
        for (int i = 0; i < numCorrectionValues; ++i)
        {

//#define TEST_FLAT_MESH
#ifdef TEST_FLAT_MESH
            correctionGridX[i] = (float)(i%32) / 32.0;
            correctionGridY[i] = 1.0 - (float)i * 32.0 / 32.0;
#else
            if( !readMeshBuffer(&correctionGridX[i], srcIdx, srcBuff, srcSize_bytes, value32bit) )
                readErr = true;
            else if( !readMeshBuffer(&correctionGridY[i], srcIdx, srcBuff, srcSize_bytes, value32bit) )
                readErr = true;
            else if( !readMeshBuffer(&errorValues[i], srcIdx, srcBuff, srcSize_bytes, value32bit) )
                readErr = true;

            if( readErr ) {
                sgct::MessageHandler::instance()->print(sgct::MessageHandler::NOTIFY_ERROR,
                    "CorrectionMesh: Error reading mpcdi correction value at index %d\n", i);
            }
#endif
        }


    }

    int   gridIndex_column, gridIndex_row;
    float* smoothPos_x = new float[numCorrectionValues];
    float* smoothPos_y = new float[numCorrectionValues];
    float* warpedPos_x = new float[numCorrectionValues];
    float* warpedPos_y = new float[numCorrectionValues];

    for (int i = 0; i < numCorrectionValues; ++i) {
        gridIndex_column = i % numberOfCols;
        gridIndex_row = i / numberOfCols;
        //Compute XY positions for each point based on a normalized 0,0 to 1,1 grid,
        // add the correction offsets to each warp point
        smoothPos_x[i] = (float)gridIndex_column / (float)(numberOfCols - 1);
        
		
		//Reverse the y position because the values from pfm file are given in raster-scan
        // order, which is left to right but starts at upper-left rather than lower-left.
        //smoothPos_y[i] = 1.0f - ((float)gridIndex_row    / (float)(numberOfRows - 1));
		//test HACK: this non-inversion actually works with pfm files that look "correct" e.g. in GIMP:
		smoothPos_y[i] =  ((float)gridIndex_row / (float)(numberOfRows - 1));
        
		//old code
		//warpedPos_x[i] = smoothPos_x[i] + correctionGridX[i];
        //warpedPos_y[i] = smoothPos_y[i] + correctionGridY[i];
		
		//HACK TEST
		warpedPos_x[i] = correctionGridX[i]; // * sin( 1.57 * float(gridIndex_column) / (float(numberOfCols)) );
		warpedPos_y[i] = correctionGridY[i]; // * sin( 1.57 * float(gridIndex_row) / (float(numberOfRows)));
    }


//#define NORMALIZE_CORRECTION_MESH
#ifdef NORMALIZE_CORRECTION_MESH
    float maxX = *std::max_element(warpedPos_x, warpedPos_x + numCorrectionValues);
    float minX = *std::min_element(warpedPos_x, warpedPos_x + numCorrectionValues);
    float scaleRangeX = maxX - minX;
    float maxY = *std::max_element(warpedPos_y, warpedPos_y + numCorrectionValues);
    float minY = *std::min_element(warpedPos_y, warpedPos_y + numCorrectionValues);
    float scaleRangeY = maxY - minY;
    float scaleFactor = (scaleRangeX >= scaleRangeY) ? scaleRangeX : scaleRangeY;
    //Scale all positions to fit within 0,0 to 1,1
    for (int i = 0; i < numCorrectionValues; ++i) {
        warpedPos_x[i] = (warpedPos_x[i] - minX) / scaleFactor;
        warpedPos_y[i] = (warpedPos_y[i] - minY) / scaleFactor;
    }
#endif //NORMALIZE_CORRECTION_MESH

    CorrectionMeshVertex vertex;
    std::vector<CorrectionMeshVertex> vertices;
    //init to max intensity (opaque white)
    vertex.r = 1.0f;
    vertex.g = 1.0f;
    vertex.b = 1.0f;
    vertex.a = 1.0f;

    for (int i = 0; i < numCorrectionValues; ++i) 
	{
        //vertex.s = smoothPos_x[i];
        //vertex.t = smoothPos_y[i];

        ////scale to viewport coordinates
        //vertex.x = 2.0f * warpedPos_x[i] - 1.0f;
        //vertex.y = 2.0f * warpedPos_y[i] - 1.0f;


		//scale to viewport coordinates
		vertex.x = 2.0f * smoothPos_x[i] - 1.0f;
		vertex.y = 2.0f * smoothPos_y[i] - 1.0f;

		vertex.s = 1.0f * warpedPos_x[i] - 0.0f;
		vertex.t = 1.0f * warpedPos_y[i] - 0.0f;

		vertices.push_back(vertex);
    }
    delete[] warpedPos_x;
    delete[] warpedPos_y;
    delete[] smoothPos_x;
    delete[] smoothPos_y;

    //copy vertices
    unsigned int numberOfVertices = numberOfCols * numberOfRows;
    mTempVertices = new CorrectionMeshVertex[numberOfVertices];
    memcpy(mTempVertices, vertices.data(), numberOfVertices * sizeof(CorrectionMeshVertex));
    mGeometries[WARP_MESH].mNumberOfVertices = numberOfVertices;
    vertices.clear();

    std::vector<unsigned int> indices;
    unsigned int i0, i1, i2, i3;
    for (unsigned int c = 0; c < (numberOfCols -1); c++)
        for (unsigned int r = 0; r < (numberOfRows-1); r++)
        {
            i0 = r * numberOfCols + c;
            i1 = r * numberOfCols + (c + 1);
            i2 = (r + 1) * numberOfCols + (c + 1);
            i3 = (r + 1) * numberOfCols + c;

            //fprintf(stderr, "Indexes: %u %u %u %u\n", i0, i1, i2, i3);

            /*

            3      2
             x____x
             |   /|
             |  / |
             | /  |
             |/   |
             x----x
            0      1

            */

            //triangle 1
            indices.push_back(i0);
            indices.push_back(i1);
            indices.push_back(i2);

            //triangle 2
            indices.push_back(i0);
            indices.push_back(i2);
            indices.push_back(i3);
        }

    //allocate and copy indices
    mGeometries[WARP_MESH].mNumberOfIndices = static_cast<unsigned int>(indices.size());
    mTempIndices = new unsigned int[mGeometries[WARP_MESH].mNumberOfIndices];
    memcpy(mTempIndices, indices.data(), mGeometries[WARP_MESH].mNumberOfIndices * sizeof(unsigned int));
    indices.clear();

    mGeometries[WARP_MESH].mGeometryType = GL_TRIANGLES;

    createMesh(&mGeometries[WARP_MESH]);

    sgct::MessageHandler::instance()->print(sgct::MessageHandler::NOTIFY_DEBUG, "CorrectionMesh: Mpcdi Correction mesh read successfully! Vertices=%u, Indices=%u.\n", mGeometries[WARP_MESH].mNumberOfVertices, mGeometries[WARP_MESH].mNumberOfIndices);

    return true;
}

bool sgct_core::CorrectionMesh::readMeshBuffer(float* dest, unsigned int& idx,
                                               char* src,
                                               const size_t srcSize_bytes,
                                               const int readSize_bytes)
{
    float val;
    if( (idx + readSize_bytes) > srcSize_bytes )
    {
        sgct::MessageHandler::instance()->print(sgct::MessageHandler::NOTIFY_ERROR,
            "CorrectionMesh: Reached EOF in mesh buffer!\n");
        return false;
    }
    memcpy(&val, &src[idx], readSize_bytes);
    *dest = val;
    idx += readSize_bytes;
    return true;
}

void sgct_core::CorrectionMesh::setupSimpleMesh(CorrectionMeshGeometry * geomPtr, Viewport * parent)
{
    unsigned int numberOfVertices = 4;
    unsigned int numberOfIndices = 4;
    
    geomPtr->mNumberOfVertices = numberOfVertices;
    geomPtr->mNumberOfIndices = numberOfIndices;
    geomPtr->mGeometryType = GL_TRIANGLE_STRIP;

    mTempVertices = new CorrectionMeshVertex[ numberOfVertices ];
    memset(mTempVertices, 0, numberOfVertices * sizeof(CorrectionMeshVertex));

    mTempIndices = new unsigned int[numberOfIndices];
    memset(mTempIndices, 0, numberOfIndices * sizeof(unsigned int));

    mTempIndices[0] = 0;
    mTempIndices[1] = 3;
    mTempIndices[2] = 1;
    mTempIndices[3] = 2;

    mTempVertices[0].r = 1.0f;
    mTempVertices[0].g = 1.0f;
    mTempVertices[0].b = 1.0f;
    mTempVertices[0].a = 1.0f;
    mTempVertices[0].s = 0.0f * parent->getXSize() + parent->getX();
    mTempVertices[0].t = 0.0f * parent->getYSize() + parent->getY();
    mTempVertices[0].x = 2.0f*(0.0f * parent->getXSize() + parent->getX()) - 1.0f;
    mTempVertices[0].y = 2.0f*(0.0f * parent->getYSize() + parent->getY()) -1.0f;

    mTempVertices[1].r = 1.0f;
    mTempVertices[1].g = 1.0f;
    mTempVertices[1].b = 1.0f;
    mTempVertices[1].a = 1.0f;
    mTempVertices[1].s = 1.0f * parent->getXSize() + parent->getX();
    mTempVertices[1].t = 0.0f * parent->getYSize() + parent->getY();
    mTempVertices[1].x = 2.0f*(1.0f * parent->getXSize() + parent->getX()) - 1.0f;
    mTempVertices[1].y = 2.0f*(0.0f * parent->getYSize() + parent->getY()) - 1.0f;

    mTempVertices[2].r = 1.0f;
    mTempVertices[2].g = 1.0f;
    mTempVertices[2].b = 1.0f;
    mTempVertices[2].a = 1.0f;
    mTempVertices[2].s = 1.0f * parent->getXSize() + parent->getX();
    mTempVertices[2].t = 1.0f * parent->getYSize() + parent->getY();
    mTempVertices[2].x = 2.0f*(1.0f * parent->getXSize() + parent->getX()) - 1.0f;
    mTempVertices[2].y = 2.0f*(1.0f * parent->getYSize() + parent->getY()) - 1.0f;

    mTempVertices[3].r = 1.0f;
    mTempVertices[3].g = 1.0f;
    mTempVertices[3].b = 1.0f;
    mTempVertices[3].a = 1.0f;
    mTempVertices[3].s = 0.0f * parent->getXSize() + parent->getX();
    mTempVertices[3].t = 1.0f * parent->getYSize() + parent->getY();
    mTempVertices[3].x = 2.0f*(0.0f * parent->getXSize() + parent->getX()) - 1.0f;
    mTempVertices[3].y = 2.0f*(1.0f * parent->getYSize() + parent->getY()) - 1.0f;
}

void sgct_core::CorrectionMesh::setupMaskMesh(Viewport * parent, bool flip_x, bool flip_y)
{
    unsigned int numberOfVertices = 4;
    unsigned int numberOfIndices = 4;

    mGeometries[MASK_MESH].mNumberOfVertices = numberOfVertices;
    mGeometries[MASK_MESH].mNumberOfIndices = numberOfIndices;
    mGeometries[MASK_MESH].mGeometryType = GL_TRIANGLE_STRIP;

    mTempVertices = new CorrectionMeshVertex[numberOfVertices];
    memset(mTempVertices, 0, numberOfVertices * sizeof(CorrectionMeshVertex));

    mTempIndices = new unsigned int[numberOfIndices];
    memset(mTempIndices, 0, numberOfIndices * sizeof(unsigned int));

    mTempIndices[0] = 0;
    mTempIndices[1] = 3;
    mTempIndices[2] = 1;
    mTempIndices[3] = 2;

    mTempVertices[0].r = 1.0f;
    mTempVertices[0].g = 1.0f;
    mTempVertices[0].b = 1.0f;
    mTempVertices[0].a = 1.0f;
    mTempVertices[0].s = flip_x ? 1.0f : 0.0f;
    mTempVertices[0].t = flip_y ? 1.0f : 0.0f;
    mTempVertices[0].x = 2.0f*(0.0f * parent->getXSize() + parent->getX()) - 1.0f;
    mTempVertices[0].y = 2.0f*(0.0f * parent->getYSize() + parent->getY()) - 1.0f;

    mTempVertices[1].r = 1.0f;
    mTempVertices[1].g = 1.0f;
    mTempVertices[1].b = 1.0f;
    mTempVertices[1].a = 1.0f;
    mTempVertices[1].s = flip_x ? 0.0f : 1.0f;
    mTempVertices[1].t = flip_y ? 1.0f : 0.0f;
    mTempVertices[1].x = 2.0f*(1.0f * parent->getXSize() + parent->getX()) - 1.0f;
    mTempVertices[1].y = 2.0f*(0.0f * parent->getYSize() + parent->getY()) - 1.0f;

    mTempVertices[2].r = 1.0f;
    mTempVertices[2].g = 1.0f;
    mTempVertices[2].b = 1.0f;
    mTempVertices[2].a = 1.0f;
    mTempVertices[2].s = flip_x ? 0.0f : 1.0f;
    mTempVertices[2].t = flip_y ? 0.0f : 1.0f;
    mTempVertices[2].x = 2.0f*(1.0f * parent->getXSize() + parent->getX()) - 1.0f;
    mTempVertices[2].y = 2.0f*(1.0f *parent->getYSize() + parent->getY()) - 1.0f;

    mTempVertices[3].r = 1.0f;
    mTempVertices[3].g = 1.0f;
    mTempVertices[3].b = 1.0f;
    mTempVertices[3].a = 1.0f;
    mTempVertices[3].s = flip_x ? 1.0f : 0.0f;
    mTempVertices[3].t = flip_y ? 0.0f : 1.0f;
    mTempVertices[3].x = 2.0f*(0.0f * parent->getXSize() + parent->getX()) - 1.0f;
    mTempVertices[3].y = 2.0f*(1.0f * parent->getYSize() + parent->getY()) - 1.0f;
}

void sgct_core::CorrectionMesh::createMesh(sgct_core::CorrectionMeshGeometry * geomPtr)
{
    /*sgct::MessageHandler::instance()->print(sgct::MessageHandler::NOTIFY_INFO, "Uploading mesh data (type=%d)...\n",
        ClusterManager::instance()->getMeshImplementation());*/
    
    if(ClusterManager::instance()->getMeshImplementation() == ClusterManager::BUFFER_OBJECTS)
    {
        if(!sgct::Engine::instance()->isOGLPipelineFixed())
        {
            glGenVertexArrays(1, &(geomPtr->mMeshData[Array]));
            glBindVertexArray(geomPtr->mMeshData[Array]);

            sgct::MessageHandler::instance()->print(sgct::MessageHandler::NOTIFY_DEBUG, "CorrectionMesh: Generating VAO: %d\n", geomPtr->mMeshData[Array]);
        }

        glGenBuffers(2, &(geomPtr->mMeshData[0]));
        sgct::MessageHandler::instance()->print(sgct::MessageHandler::NOTIFY_DEBUG, "CorrectionMesh: Generating VBOs: %d %d\n", geomPtr->mMeshData[0], geomPtr->mMeshData[1]);

        glBindBuffer(GL_ARRAY_BUFFER, geomPtr->mMeshData[Vertex]);
        glBufferData(GL_ARRAY_BUFFER, geomPtr->mNumberOfVertices * sizeof(CorrectionMeshVertex), &mTempVertices[0], GL_STATIC_DRAW);

        if(!sgct::Engine::instance()->isOGLPipelineFixed())
        {
            glEnableVertexAttribArray(0);
            glVertexAttribPointer(
                0, // The attribute we want to configure
                2,                           // size
                GL_FLOAT,                    // type
                GL_FALSE,                    // normalized?
                sizeof(CorrectionMeshVertex),// stride
                reinterpret_cast<void*>(0)   // array buffer offset
            );

            glEnableVertexAttribArray(1);
            glVertexAttribPointer(
                1, // The attribute we want to configure
                2,                           // size
                GL_FLOAT,                    // type
                GL_FALSE,                    // normalized?
                sizeof(CorrectionMeshVertex),// stride
                reinterpret_cast<void*>(8)   // array buffer offset
            );

            glEnableVertexAttribArray(2);
            glVertexAttribPointer(
                2, // The attribute we want to configure
                4,                           // size
                GL_FLOAT,                    // type
                GL_FALSE,                    // normalized?
                sizeof(CorrectionMeshVertex),// stride
                reinterpret_cast<void*>(16)  // array buffer offset
            );
        }

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, geomPtr->mMeshData[Index]);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, geomPtr->mNumberOfIndices * sizeof(unsigned int), &mTempIndices[0], GL_STATIC_DRAW);

        //unbind
        if(!sgct::Engine::instance()->isOGLPipelineFixed())
            glBindVertexArray(0);
        
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    }
    else //display lists
    {
        geomPtr->mMeshData[Vertex] = glGenLists(1);
        glNewList(geomPtr->mMeshData[Vertex], GL_COMPILE);

        glBegin(geomPtr->mGeometryType);
        CorrectionMeshVertex vertex;
        
        for (unsigned int i = 0; i < geomPtr->mNumberOfIndices; i++)
        {
            vertex = mTempVertices[mTempIndices[i]];

            glColor4f(vertex.r, vertex.g, vertex.b, vertex.a);
            glTexCoord2f(vertex.s, vertex.t);
            glVertex2f(vertex.x, vertex.y);
        }
        glEnd();

        glEndList();
        
        sgct::MessageHandler::instance()->print(sgct::MessageHandler::NOTIFY_DEBUG, "CorrectionMesh: Generating display list: %d\n", geomPtr->mMeshData[Vertex]);
    }
}

void sgct_core::CorrectionMesh::exportMesh(const std::string & exportMeshPath)
{
    if (mGeometries[WARP_MESH].mGeometryType != GL_TRIANGLES && mGeometries[WARP_MESH].mGeometryType != GL_TRIANGLE_STRIP)
    {
        sgct::MessageHandler::instance()->print(sgct::MessageHandler::NOTIFY_ERROR, "CorrectionMesh error: Failed to export '%s'. Geometry type is not supported!\n", exportMeshPath.c_str());
        return;
    }
    
    std::ofstream file;
    file.open(exportMeshPath, std::ios::out);
    if (file.is_open())
    {
        file << std::fixed;
        file << std::setprecision(6);
        
        file << "# SGCT warping mesh\n";
        file << "# Number of vertices: " << mGeometries[WARP_MESH].mNumberOfVertices << "\n";
        
        //export vertices
        for (unsigned int i = 0; i < mGeometries[WARP_MESH].mNumberOfVertices; i++)
            file << "v " << mTempVertices[i].x << " " << mTempVertices[i].y << " 0\n";

        //export texture coords
        for (unsigned int i = 0; i < mGeometries[WARP_MESH].mNumberOfVertices; i++)
            file << "vt " << mTempVertices[i].s << " " << mTempVertices[i].t << " 0\n";

        //export generated normals
        for (unsigned int i = 0; i < mGeometries[WARP_MESH].mNumberOfVertices; i++)
            file << "vn 0 0 1\n";

        file << "# Number of faces: " << mGeometries[WARP_MESH].mNumberOfIndices/3 << "\n";

        //export face indices
        if (mGeometries[WARP_MESH].mGeometryType == GL_TRIANGLES)
        {
            for (unsigned int i = 0; i < mGeometries[WARP_MESH].mNumberOfIndices; i += 3)
            {
                file << "f " << mTempIndices[i] + 1 << "/" << mTempIndices[i] + 1 << "/" << mTempIndices[i] + 1 << " ";
                file << mTempIndices[i + 1] + 1 << "/" << mTempIndices[i + 1] + 1 << "/" << mTempIndices[i + 1] + 1 << " ";
                file << mTempIndices[i + 2] + 1 << "/" << mTempIndices[i + 2] + 1 << "/" << mTempIndices[i + 2] + 1 << "\n";
            }
        }
        else //trangle strip
        {
            //first base triangle
            file << "f " << mTempIndices[0] + 1 << "/" << mTempIndices[0] + 1 << "/" << mTempIndices[0] + 1 << " ";
            file << mTempIndices[1] + 1 << "/" << mTempIndices[1] + 1 << "/" << mTempIndices[1] + 1 << " ";
            file << mTempIndices[2] + 1 << "/" << mTempIndices[2] + 1 << "/" << mTempIndices[2] + 1 << "\n";

            for (unsigned int i = 2; i < mGeometries[WARP_MESH].mNumberOfIndices; i++)
            {
                file << "f " << mTempIndices[i] + 1 << "/" << mTempIndices[i] + 1 << "/" << mTempIndices[i] + 1 << " ";
                file << mTempIndices[i - 1] + 1 << "/" << mTempIndices[i - 1] + 1 << "/" << mTempIndices[i - 1] + 1 << " ";
                file << mTempIndices[i - 2] + 1 << "/" << mTempIndices[i - 2] + 1 << "/" << mTempIndices[i - 2] + 1 << "\n";
            }
        }

        file.close();

        sgct::MessageHandler::instance()->print(sgct::MessageHandler::NOTIFY_INFO, "CorrectionMesh: Mesh '%s' exported successfully.\n", exportMeshPath.c_str());
    }
    else
        sgct::MessageHandler::instance()->print(sgct::MessageHandler::NOTIFY_ERROR, "CorrectionMesh error: Failed to export '%s'!\n", exportMeshPath.c_str());
}

void sgct_core::CorrectionMesh::cleanUp()
{
    delete[] mTempVertices;
    mTempVertices = NULL;
    delete[] mTempIndices;
    mTempIndices = NULL;
}

void sgct_core::CorrectionMesh::clamp(float & val, const float max, const float min)
{
    if (val > max)
        val = max;
    else if (val < min)
        val = min;
}

/*!
Render the final mesh where for mapping the frame buffer to the screen
\param mask to enable mask texture mode
*/
void sgct_core::CorrectionMesh::render(const MeshType & mt)
{
    //for test
    //glDisable(GL_CULL_FACE);

    if(sgct::SGCTSettings::instance()->getShowWarpingWireframe())
        glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);

    CorrectionMeshGeometry * geomPtr = &mGeometries[mt];

    if( ClusterManager::instance()->getMeshImplementation() == ClusterManager::BUFFER_OBJECTS )
    {
        if(sgct::Engine::instance()->isOGLPipelineFixed())
        {
            glPushClientAttrib(GL_CLIENT_VERTEX_ARRAY_BIT);

            glEnableClientState(GL_VERTEX_ARRAY);
            glClientActiveTexture(GL_TEXTURE0);
            glEnableClientState(GL_TEXTURE_COORD_ARRAY);
            glEnableClientState(GL_COLOR_ARRAY);

            glBindBuffer(GL_ARRAY_BUFFER, geomPtr->mMeshData[Vertex]);
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, geomPtr->mMeshData[Index]);
        
            glVertexPointer(2, GL_FLOAT, sizeof(CorrectionMeshVertex), reinterpret_cast<void*>(0));        
            glTexCoordPointer(2, GL_FLOAT, sizeof(CorrectionMeshVertex), reinterpret_cast<void*>(8));
            glColorPointer(4, GL_FLOAT, sizeof(CorrectionMeshVertex), reinterpret_cast<void*>(16));

            glDrawElements(geomPtr->mGeometryType, geomPtr->mNumberOfIndices, GL_UNSIGNED_INT, NULL);
        
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
            glBindBuffer(GL_ARRAY_BUFFER, 0);

            glPopClientAttrib();
        }
        else
        {
            glBindVertexArray(geomPtr->mMeshData[Array]);
            glDrawElements(geomPtr->mGeometryType, geomPtr->mNumberOfIndices, GL_UNSIGNED_INT, NULL);
            glBindVertexArray(0);
        }
    }
    else
    {
        glCallList(geomPtr->mMeshData[Vertex]);
    }

    if (sgct::SGCTSettings::instance()->getShowWarpingWireframe())
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

    //for test
    //glEnable(GL_CULL_FACE);
}

/*!
Parse hint from string to enum.
*/
sgct_core::CorrectionMesh::MeshHint sgct_core::CorrectionMesh::parseHint(const std::string & hintStr)
{
    if (hintStr.empty())
        return NO_HINT;
    
    //transform to lowercase
    std::string str(hintStr);
    std::transform(str.begin(), str.end(), str.begin(), ::tolower);

    sgct_core::CorrectionMesh::MeshHint hint = NO_HINT;
    if (str.compare("domeprojection") == 0)
        hint = DOMEPROJECTION_HINT;
    else if(str.compare("scalable") == 0)
        hint = SCALEABLE_HINT;
    else if (str.compare("sciss") == 0)
        hint = SCISS_HINT;
    else if (str.compare("simcad") == 0)
        hint = SIMCAD_HINT;
    else if (str.compare("skyskan") == 0)
        hint = SKYSKAN_HINT;
    else if (str.compare("mpcdi") == 0)
        hint = MPCDI_HINT;
    else
    {
        sgct::MessageHandler::instance()->print(sgct::MessageHandler::NOTIFY_WARNING, "CorrectionMesh: hint '%s' is invalid!\n", hintStr.c_str());
    }

    return hint;
}
