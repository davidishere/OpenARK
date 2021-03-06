#include "stdafx.h"

// OpenARK Libraries
#include "version.h"
#ifdef PMDSDK_ENABLED
    #include "PMDCamera.h"
#endif
#ifdef RSSDK_ENABLED
    #include "SR300Camera.h"
#endif

#include "Core.h"
#include "Visualizer.h"
#include "StreamingAverager.h"

using namespace ark;

int main() {
    printf("Welcome to OpenARK v %s Demo\n\n", VERSION);
    printf("CONTROLS:\nQ or ESC to quit, P to show/hide planes, H to show/hide hands, SPACE to play/pause\n\n");
    printf("VIEWER BACKGROUNDS:\n1 = IR Image, 2 = Depth Image, 3 = Normal Map, 0 = None\n\n");
    printf("HAND DETECTION OPTIONS:\nS = Enable/Disable SVM, C = Enforce/Unenforce Edge Connected Criterion\n\n");
    printf("MISCELLANEOUS:\nA = Measure Surface Area (Of Hands and Planes)\n");

    // seed the rng
    srand(time(NULL));

    // initialize the camera
    DepthCamera::Ptr camera;

#ifdef PMDSDK_ENABLED
    if (!strcmp(OPENARK_CAMERA_TYPE, "pmd")) {
        camera = std::make_shared<PMDCamera>();
    }
    else {
        return -1;
    }
#endif
#ifdef RSSDK_ENABLED
    if (!strcmp(OPENARK_CAMERA_TYPE, "sr300")) {
        camera = std::make_shared<SR300Camera>();
    }
    else {
        return -1;
    }
#endif
    // initialize parameters
    DetectionParams::Ptr params = DetectionParams::create(); // default parameters

    // initialize detectors
    PlaneDetector::Ptr planeDetector = std::make_shared<PlaneDetector>();
    HandDetector::Ptr handDetector = std::make_shared<HandDetector>(planeDetector);
    
    // store frame & FPS information
    const int FPS_CYCLE_FRAMES = 8; // number of frames to average FPS over (FPS 'cycle' length)
    clock_t currCycleStartTime = 0; // start time of current cycle
    float currFPS; // current FPS

    int currFrame = 0; // current frame number (since launch/last pause)
    int backgroundStyle = 1; // background style: 0=none, 1=ir, 2=depth, 3=normal

    // option flags
    bool showHands = true, showPlanes = false, useSVM = true, useEdgeConn = false, showArea = false, playing = true;

    // turn on the camera
    camera->beginCapture();

    // main demo loop
    while (true)
    {
        // get latest image from the camera
        cv::Mat xyzMap = camera->getXYZMap();

        /**** Start: Hand/plane detection ****/

        // query objects in the current frame
        params->handUseSVM = useSVM;
        params->handRequireEdgeConnected = useEdgeConn;

        std::vector<Hand::Ptr> hands;
        std::vector<FramePlane::Ptr> planes;

        planeDetector->setParams(params);
        handDetector->setParams(params);
        
        if (showPlanes || showHands) {
            /* even if only hand layer is enabled, 
               planes need to be detected anyway for finding hand contact points */
            planeDetector->update(*camera);
            planes = planeDetector->getPlanes();

            if (showHands) {
                handDetector->update(*camera);
                hands = handDetector->getHands();
            }
        }

        /**** End: Hand/plane detection ****/

        /**** Start: Visualization ****/

        // construct visualizations
        cv::Mat handVisual;

        // background of visualization
        if (backgroundStyle == 1 && camera->hasIRMap()) {
            // IR background
            cv::cvtColor(camera->getIRMap(), handVisual, cv::COLOR_GRAY2BGR, 3);
        }

        else if (backgroundStyle == 2) {
            // depth map background
            Visualizer::visualizeXYZMap(xyzMap, handVisual);
        }

        else if (backgroundStyle == 3) {
            // normal map background
            cv::Mat normalMap = planeDetector->getNormalMap();
            if (!normalMap.empty()) {
                Visualizer::visualizeNormalMap(normalMap, handVisual, params->normalResolution);
            }
            else {
                handVisual = cv::Mat::zeros(camera->getImageSize(), CV_8UC3);
            }
        }

        else {
            handVisual = cv::Mat::zeros(camera->getImageSize(), CV_8UC3);
        }

        const cv::Scalar WHITE(255, 255, 255);

        if (showPlanes) {
            // color all points on the screen close to a plane
            cv::Vec3s color;

            for (uint i = 0; i < planes.size(); ++i) {
                color = util::paletteColor(i);

                for (int row = 0; row < xyzMap.rows; ++row) {
                    const Vec3f * ptr = xyzMap.ptr<Vec3f>(row);
                    Vec3b * outPtr = handVisual.ptr<Vec3b>(row);

                    for (int col = 0; col < xyzMap.cols; ++col) {
                        const Vec3f & xyz = ptr[col];
                        if (xyz[2] == 0) continue;

                        float norm = util::pointPlaneNorm(xyz, planes[i]->equation);

                        float fact = std::max(0.0, 0.5f - norm / params->handPlaneMinNorm / 8.0f);
                        if (fact == 0.0f) continue;

                        outPtr[col] += (color - (cv::Vec3s)outPtr[col]) * fact;
                    }
                }

                // draw normal vector
                Point2i drawPt = planes[i]->getCenterIJ();
                cv::Vec3f normal = planes[i]->getNormalVector();
                Point2i arrowPt(drawPt.x + normal[0] * 100, drawPt.y - normal[1] * 100);
                cv::arrowedLine(handVisual, drawPt, arrowPt, WHITE, 4, cv::LINE_AA, 0, 0.2);

                if (showArea) {
                    double area = planes[i]->getSurfArea();
                    cv::putText(handVisual, std::to_string(area), drawPt + Point(10, 10),
                        0, 0.6, cv::Scalar(255, 255, 255));
                }
            }
        }

        // draw hands
        if (showHands) {
            for (Hand::Ptr hand : hands) {
                double dispVal;
                if (showArea) {
                    dispVal = hand->getSurfArea();
                }
                else if (useSVM) {
                    dispVal = hand->getSVMConfidence();
                }
                else {
                    dispVal = FLT_MAX;
                }
                Visualizer::visualizeHand(handVisual, handVisual, hand.get(),
                                          dispVal, &planes);
            }
        }

        if (showHands && hands.size() > 0) {
            // show "N Hands" on top left
            cv::putText(handVisual, std::to_string(hands.size()) +
                util::pluralize(" Hand", hands.size()),
                Point2i(10, 25), 0, 0.5, WHITE);
        }

        if (showPlanes && planes.size() > 0) {
            // show "N Planes" on top left
            cv::putText(handVisual, std::to_string(planes.size()) +
                util::pluralize(" Plane", planes.size()),
                Point2i(10, 50), 0, 0.5, WHITE);
        }

        // update FPS
        if (currFrame % FPS_CYCLE_FRAMES == 0) {
            clock_t now = clock();
            currFPS = (float) FPS_CYCLE_FRAMES * CLOCKS_PER_SEC / (now - currCycleStartTime);
            currCycleStartTime = now;
        }

        if (currFrame > FPS_CYCLE_FRAMES && !camera->badInput()) {
            // show FPS on top right
            std::stringstream fpsDisplay;
            static char chr[32];
            sprintf(chr, "FPS: %02.3lf", currFPS);
            cv::putText(handVisual, chr, Point2i(handVisual.cols - 120, 25), 0, 0.5, WHITE);
#ifdef DEBUG
            cv::putText(handVisual, "Frame: " + std::to_string(currFrame),
                Point2i(handVisual.cols - 120, 50), 0, 0.5, WHITE);
#endif
        }

        int wait = 1;

        // show "NO SIGNAL" on each visual in case of bad input
        if (camera->badInput() || !playing) {
            // wait 50 ms, or pause
            wait = !playing ? 0 : 50;

            const int RECT_WID = (!playing ? 120 : 160), RECT_HI = 40;
            cv::Rect rect(handVisual.cols / 2 - RECT_WID / 2,
                handVisual.rows / 2 - RECT_HI / 2,
                RECT_WID, RECT_HI);

            const cv::Scalar RECT_COLOR = cv::Scalar(0, (!playing ? 160 : 50), 255);
            const std::string NO_SIGNAL_STR = (!playing ? "PAUSED" : "NO SIGNAL");
            const cv::Point STR_POS(handVisual.cols / 2 - (!playing ? 50 : 65), handVisual.rows / 2 + 7);

            // show on each visual
            cv::rectangle(handVisual, rect, RECT_COLOR, -1);
            cv::putText(handVisual, NO_SIGNAL_STR, STR_POS, 0, 0.8, WHITE, 1, cv::LINE_AA);
            if (xyzMap.rows) {
                cv::rectangle(xyzMap, rect, RECT_COLOR / 255.0, -1);
                cv::putText(xyzMap, NO_SIGNAL_STR, STR_POS, 0, 0.8, cv::Scalar(1.0f, 1.0f, 1.0f), 1, cv::LINE_AA);
            }
        }

        // show visualizations
        cv::imshow(camera->getModelName() + " Depth Map", xyzMap);
        cv::imshow("Demo Output - OpenARK v" + std::string(VERSION), handVisual);
        /**** End: Visualization ****/

        /**** Start: Controls ****/
        int c = cv::waitKey(wait);

        // make case insensitive
        if (c >= 'a' && c <= 'z') c &= 0xdf;

        // 27 is ESC
        if (c == 'Q' || c == 27) {
            /*** Loop Break Condition ***/
            break;
        }
        else if (c >= '0' && c <= '3') {
            backgroundStyle = c - '0';
        }

        switch (c) {
        case 'P':
            showPlanes ^= 1; break;
        case 'H':
            showHands ^= 1; break;
        case 'S':
            useSVM ^= 1; break;
        case 'C':
            useEdgeConn ^= 1; break;
        case 'A':
            showArea ^= 1; break;
        case ' ':
            playing ^= 1; 
            // reset frame number
            if (playing) currFrame = -1;
            break;
        }

        /**** End: Controls ****/
        ++currFrame;
    }

    camera->endCapture();

    cv::destroyAllWindows();
    return 0;
}