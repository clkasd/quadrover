#include <memory>
#include <fstream>
#include <sstream>
#include <iostream>
#include <list>
#include <vector>

#include <opencv2/core/core.hpp>
#include <opencv2/imgproc/imgproc.hpp>
#include <opencv2/imgproc/imgproc_c.h>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/features2d/features2d.hpp>

#include "Time.h"

#include "ActiveObject.h"
#include "funcs.h"
//#include "mser.h"

int main(int argv, char* argc[])
{
	using namespace ICSL;
	using namespace ICSL::Quadrotor;
//	using namespace ICSL::Constants;
	using namespace TNT;
	using namespace std;
	cout << "start chadding" << endl;

	string dataDir;
	int dataSet = 0;
	int startImg=0, endImg=0;
	switch(dataSet)
	{
		case 0:
			dataDir = "../dataSets/Sep19";
//			startImg = 971;
			startImg = 1360;
			endImg = 2874;
			break;
	}

	string imgDir;
	imgDir = dataDir + "/video";

	vector<pair<int, Time>> imgIdList;
	// preload all images
	list<pair<int, shared_ptr<cv::Mat>>> imgList;
	int imgId = startImg;
	int numImages;
	numImages = endImg-startImg;;
	for(int i=0; i<numImages; i++)
	{
//		cout << "Loading image " << i << ": " << imgId << endl;
		cv::Mat img;
		while(img.data == NULL)
		{
			stringstream ss;
			ss << "image_" << ++imgId << ".bmp";
			img = cv::imread(imgDir+"/"+ss.str());
		}

		shared_ptr<cv::Mat> pImg(new cv::Mat);
		img.copyTo(*pImg);

		imgList.push_back(pair<int, shared_ptr<cv::Mat>>(imgId, pImg));
	}

	// Camera calibration
	cv::Point2f center;
	shared_ptr<cv::Mat> mCameraMatrix_640x480, mCameraMatrix_320x240, mCameraDistortionCoeffs;
	cv::FileStorage fs;
	string filename = dataDir + "/s3Calib_640x480.yml";
	fs.open(filename.c_str(), cv::FileStorage::READ);
	if( fs.isOpened() )
	{
		mCameraMatrix_640x480 = shared_ptr<cv::Mat>(new cv::Mat());
		mCameraDistortionCoeffs = shared_ptr<cv::Mat>(new cv::Mat());

		fs["camera_matrix"] >> *mCameraMatrix_640x480;
		fs["distortion_coefficients"] >> *mCameraDistortionCoeffs;
		cout << "Camera calib loaded from " << filename.c_str() << endl;
//		cout << "\t" << "Focal length: " << mCameraMatrix_640x480->at<double>(0,0) << endl;
//		cout << "\t" << "centerX: " << mCameraMatrix_640x480->at<double>(0,2) << endl;
//		cout << "\t" << "centerY: " << mCameraMatrix_640x480->at<double>(1,2) << endl;

		mCameraMatrix_320x240 = shared_ptr<cv::Mat>(new cv::Mat());
		mCameraMatrix_640x480->copyTo( *mCameraMatrix_320x240 );
		(*mCameraMatrix_320x240) = (*mCameraMatrix_320x240)*0.5;

		center.x = mCameraMatrix_320x240->at<double>(0,2);
		center.y = mCameraMatrix_320x240->at<double>(1,2);
	}
	else
	cout << "Failed to open " <<  filename.c_str();
	fs.release();

	cv::namedWindow("chad",1);
	cv::moveWindow("chad",0,0);
	cv::namedWindow("bob",1);
	cv::moveWindow("bob",321,0);
	cv::namedWindow("tom",1);
	cv::moveWindow("tom",0,261);

	// make the mser detector
	int delta = 5;
	int minArea = 1000;
	int maxArea = 0.5*320*240;
	double maxVariation = 0.25; // smaller reduces number of regions
//	double minDiversity = 0.5; // smaller increase the number of regions
	double minDiversity = 0.8; // smaller increase the number of regions
	// for color only
	int maxEvolution = 200;
	double areaThreshold = 1.01;
	double minMargin = 0.003;
	int edgeBlurSize = 5;
	cv::MSER mserDetector(delta, minArea, maxArea, maxVariation, minDiversity);

	vector<shared_ptr<ActiveObject>> activeObjects;

	int keypress = 0;
	list<pair<int, shared_ptr<cv::Mat>>>::const_iterator imgIter = imgList.begin();
	cv::Mat img(240,320, CV_8UC3), imgGray, oldImg(240,320,CV_8UC3);
	vector<vector<cv::Point>> regions;
	float t1, t2, t3, t4, t5, t6, t7, t8, t9, t10;
	t1 = t2 = t3 = t4 = t5 = t6 = t7 = t8 = t9 = t10 = 0;
	int imgCnt = 0;
	Time curTime;
	while(keypress != (int)'q' && imgIter != imgList.end())
	{
cout << "imgCnt: " << imgCnt <<  endl;
		Time chadTime;
		curTime.addTimeMS(33);

Time start;
		img = *(imgIter->second);
		cvtColor(img, imgGray, CV_BGR2GRAY);

t1 += start.getElapsedTimeMS(); start.setTime();
		mserDetector(imgGray, regions);

t2 += start.getElapsedTimeMS(); start.setTime();
		/////////////////// Find contours ///////////////////////
		vector<vector<cv::Point>> allContours;
		int border = 2;
		for(int i=0; i<regions.size(); i++)
		{
			cv::Rect boundRect = boundingRect( cv::Mat(regions[i]) );
			boundRect.x = max(0, boundRect.x-border);
			boundRect.y = max(0, boundRect.y-border);
			boundRect.width = min(img.cols, boundRect.x+boundRect.width+2*border)-boundRect.x;
			boundRect.height= min(img.rows, boundRect.y+boundRect.height+2*border)-boundRect.y;;
			cv::Point corner(boundRect.x, boundRect.y);
			cv::Mat mask(boundRect.height, boundRect.width, CV_8UC1, cv::Scalar(0));
//			int minX = 0;
//			int minY = 0;
//			int maxX = mask.cols-1;
//			int maxY = mask.rows-1;
			for(int j=0; j<regions[i].size(); j++)
			{
				int x = regions[i][j].x-corner.x;
				int y = regions[i][j].y-corner.y;
				mask.at<uchar>(y,x) = 255;

				// Do a dilate as long as I'm here. This should save a little time since I don't have
				// to slide over all the black pixels
				// Right now this isn't giving the same results as OpenCV's dilate :(
//				for(int c=max(minX, x-1); c<min(maxX,x+1); c++)
//					for(int r=max(minY, y-1); r<min(maxY,y+1); r++)
//						mask.at<uchar>(r,c) = 255;
			}

			cv::dilate(mask, mask, cv::Mat());
			cv::erode(mask, mask, cv::Mat());

			cv::Canny(mask, mask, 0, 10, 3);

			vector<vector<cv::Point>> contours;
			cv::findContours(mask, contours, CV_RETR_EXTERNAL, CV_CHAIN_APPROX_NONE, corner);
			cv::Mat mask2(img.size(), CV_8UC1, cv::Scalar(0));
			drawContours(mask2, contours, -1, cv::Scalar(255));

			for(int i=0; i<contours.size(); i++)
			{
				approxPolyDP(cv::Mat(contours[i]), contours[i], 2, true);
				if(contours[i].size() < 3 || cv::contourArea(contours[i]) < minArea)
					continue;
				allContours.push_back(contours[i]);
			}
		}

t3 += start.getElapsedTimeMS(); start.setTime();
		/////////////////// make objects of the new contours ///////////////////////
		vector<shared_ptr<ActiveObject>> curObjects;
		for(int i=0; i<allContours.size(); i++)
		{
			shared_ptr<ActiveObject> ao1(new ActiveObject(allContours[i]));
			ao1->lastFoundTime.setTime(curTime);
			if(ao1->mom.m00 > minArea)
				curObjects.push_back(ao1);
		}

		/////////////////// Get location priors for active objects ///////////////////////
		Array2D<double> mv(3,1,0.0);
		Array2D<double> Sv = 0.2*0.2*createIdentity((double)3);
		double mz = 1;
		double sz = 0.05;
		double f = mCameraMatrix_640x480->at<double>(0,0);
		Array2D<double> omega(3,1,0.0);

cout << "---------------- priors --------------------------" << endl;
		for(int i=0; i<activeObjects.size(); i++)
		{
			shared_ptr<ActiveObject> ao = activeObjects[i];
			ao->updatePosition(mv, Sv, mz, sz*sz, f, center, omega, curTime);
cout << ao->id << "\t\t";
cout << (int)(ao->expectedPos[0][0]+0.5) << " x " << (int)(ao->expectedPos[1][0]+0.5) << "\t\t";
cout << (int)(ao->posCov[0][0]+0.5) << " x " << (int)(ao->posCov[1][1]+0.5) << "\t\t";
cout << ao->mom.m00 << "\t\t";
cout << endl;
		}

cout << "---------------- current objects --------------------------" << endl;
for(int i=0; i<curObjects.size(); i++)
{
	cout << curObjects[i]->id << "\t\t";
	cout << curObjects[i]->lastCenter.x << " x " << curObjects[i]->lastCenter.y << "\t\t";
	cout << curObjects[i]->mom.m00 << "\t\t";
	cout << endl;
}

		/////////////////// Establish correspondence based on postiion ///////////////////////
		Array2D<double> Sn = 2*10*10*createIdentity((double)2);
		Array2D<double> SnInv(2,2,0.0);
		SnInv[0][0] = 1.0/Sn[0][0];
		SnInv[1][1] = 1.0/Sn[1][1];
		double varxi = pow(200,2);
		float probNoCorr = 0.0000001;
		Array2D<double> C = ActiveObject::calcCorrespondence(activeObjects, curObjects, Sn, SnInv, varxi, probNoCorr);
cout << "---------------- correspondence --------------------------" << endl;
printArray("\n",C);

		vector<Match> goodMatches;
		shared_ptr<ActiveObject> aoPrev, aoCur;
		vector<shared_ptr<ActiveObject>> repeatObjects, newObjects;
		int N1 = activeObjects.size();
		int N2 = curObjects.size();
		vector<bool> matched(N2, false);
		for(int i=0; i<N1; i++)
		{
			if(C[i][N2] > 0.5)
				continue; // this object probably doesn't have a partner

			aoPrev = activeObjects[i];

			int maxIndex = 0;
			float maxScore = 0;
			for(int j=0; j<N2; j++)
			{
				if(C[i][j] > maxScore && !matched[j])
				{
					maxScore = C[i][j];
					maxIndex =j;
					matched[j] = true;
				}
//				else if(C[i][j] > 0.3)
//				{
//					// I think this object is probably a duplicate (i.e.
//					// I found two very similar contours for the same
//					// thing) There may be some cases where this is not
//					// the best handling, though, but I'll dealing with
//					// those when they become an issue
//					matched[j] = true;
//				}
			}

			aoCur = curObjects[maxIndex];
cout << "matching " << aoPrev->id << " and " << aoCur->id << endl;

			Match m;
			m.aoPrev = aoPrev;
			m.aoCur = aoCur;
			m.score = C[i][maxIndex];
			goodMatches.push_back(m);

			// copy data over to update the shape and position
			aoPrev->contour.swap(aoCur->contour);
			aoPrev->mom = aoCur->mom;
			aoPrev->lastCenter = aoCur->lastCenter;
			aoPrev->lastFoundTime.setTime(curTime);
			memcpy(aoPrev->huMom, aoCur->huMom, 7*sizeof(double));
			memcpy(aoPrev->centralMoms, aoCur->centralMoms, 7*sizeof(double));
			aoPrev->life= min((float)21, aoPrev->life+2);
			repeatObjects.push_back(aoPrev);
		}
		
		for(int j=0; j<curObjects.size(); j++)
			if(!matched[j])
				newObjects.push_back(curObjects[j]);

t5 += start.getElapsedTimeMS(); start.setTime();
		for(int i=0; i<activeObjects.size(); i++)
			activeObjects[i]->life -= 1;

		sort(activeObjects.begin(), activeObjects.end(), ActiveObject::sortPredicate);
		while(activeObjects.size() > 0 && activeObjects.back()->life <= 0)
			activeObjects.pop_back();

//		cout << " =======  LIFE ======== " << endl;
//		for(int i=0; i<activeObjects.size(); i++)
//			cout << activeObjects[i]->id << ": " <<  activeObjects[i]->life << endl;
//		cout << endl;

		// TODO
		for(int i=0; i<newObjects.size(); i++)
			activeObjects.push_back(newObjects[i]);

		/////////////////// self-similarity check ///////////////////////
		// First reset all expected positions and covariances
		for(int i=0; i<activeObjects.size(); i++)
		{
			activeObjects[i]->expectedPos[0][0] = activeObjects[i]->lastCenter.x;
			activeObjects[i]->expectedPos[1][0] = activeObjects[i]->lastCenter.y;

			if(activeObjects[i]->posCov[0][1] == 0) // this is very crude indication of a new object
				activeObjects[i]->posCov.inject(Sn);
			else {/* posCov was already set when calculating the priors */};
		}
		// now calculate the correspondence matrix
		C = ActiveObject::calcCorrespondence(activeObjects, activeObjects, Sn, SnInv, varxi, 0);
printArray("self-similarity:\n",C);

t6 += start.getElapsedTimeMS(); start.setTime();
		imshow("chad",oldImg);

		cv::Mat dblImg(img.rows, 2*img.cols, img.type());
		oldImg.copyTo(dblImg(cv::Rect(0,0,oldImg.cols,oldImg.rows)));

		cv::drawContours(img, allContours, -1, cv::Scalar(255,0,0), 2);
		img.copyTo(oldImg);

		imshow("bob",img);
		vector<vector<cv::Point>> repeatContours(repeatObjects.size());
		
		for(int i=0; i<repeatObjects.size(); i++)
			repeatContours[i] = repeatObjects[i]->contour;
		cv::drawContours(img, repeatContours, -1, cv::Scalar(0,0,255), 2);

		img.copyTo(dblImg(cv::Rect(oldImg.cols,0,img.cols,img.rows)));
		cv::Point offset(321,0);
		for(int i=0; i<goodMatches.size(); i++)
			line(dblImg,goodMatches[i].aoPrev->lastCenter, goodMatches[i].aoCur->lastCenter+offset, cv::Scalar(0,255,0), 2);
		imshow("tom",dblImg);

t7 += start.getElapsedTimeMS(); start.setTime();
		keypress = cv::waitKey(0) % 256;

		imgIter++;
		imgCnt++;
	}

	cout << "t1:\t" << t1/imgCnt << endl;
	cout << "t2:\t" << t2/imgCnt << endl;
	cout << "t3:\t" << t3/imgCnt << endl;
	cout << "t4:\t" << t4/imgCnt << endl;
	cout << "t5:\t" << t5/imgCnt << endl;
	cout << "t6:\t" << t6/imgCnt << endl;
	cout << "t7:\t" << t7/imgCnt << endl;

    return 0;
}

