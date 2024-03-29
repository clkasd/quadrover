#include <memory>
#include <fstream>
#include <sstream>
#include <iostream>
#include <random>
#include <list>

#include <opencv2/core/core.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/features2d/features2d.hpp>

#include "toadlet/egg.h"

#include "TNT_Utils.h"

#include "constants.h"
#include "Common.h"
#include "Data.h"
#include "Observer_Angular.h"
#include "Observer_Translational.h"
#include "DataLogger.h"
#include "CommManager.h"
#include "Time.h"
#include "TranslationController.h"
#include "AttitudeThrustController.h"
#include "VideoMaker.h"
#include "MotorInterface.h"
#include "FeatureFinder.h"
#include "RegionFinder.h"
#include "ObjectTracker.h"
#include "VelocityEstimator.h"
#include "Rotation.h"
#include "Listeners.h"

// yeah, I'm too lazy to avoid making this global at the moment
toadlet::egg::Collection<ICSL::Quadrotor::CommManagerListener *> commManagerListeners;
toadlet::egg::Collection<ICSL::Quadrotor::SensorManagerListener *> sensorManagerListeners;
void addSensorManagerListener(ICSL::Quadrotor::SensorManagerListener *l){sensorManagerListeners.push_back(l);}
void addCommManagerListener(ICSL::Quadrotor::CommManagerListener *l){commManagerListeners.push_back(l);}

std::list<std::string> loadLogFile(std::string filename);

int main(int argv, char* argc[])
{
	using namespace ICSL;
	using namespace ICSL::Quadrotor;
	using namespace ICSL::Constants;
	using namespace TNT;
	using namespace std;
	cout << "start chadding" << endl;

	string dataDir;
	int dataSet = 9;
	int startImg=0, endImg=0;
	switch(dataSet)
	{
		case 0:
			dataDir = "../dataSets/Sep8";
			startImg = 6895;
			endImg = 9307;
			break;
		case 1:
			dataDir = "../dataSets/Sep12";
			startImg = 3702;
			endImg = 5713;
			break;
		case 2:
			dataDir = "../dataSets/Sep19";
			startImg = 971;
			endImg = 2874;
			break;
		case 3:
			dataDir = "../dataSets/Sep23";
			startImg = 3286;
			endImg = 5954;
			break;
		case 4:
			dataDir = "../dataSets/Oct3_2";
			startImg = 989;
			endImg = 3850;
			break;
		case 5:
			dataDir = "../dataSets/Oct13";
			startImg = 4029;
			endImg = 6546;
			break;
		case 6:
			dataDir = "../dataSets/Nov1";
			startImg = 11045;
			endImg = 13250;
			break;
		case 7:
			dataDir = "../dataSets/Nov2";
			startImg = 785;
			endImg = 2985;
			break;
		case 8:
			dataDir = "../dataSets/Nov13_3";
			startImg = 4686;
			endImg = 7486;
			break;
		case 9:
			dataDir = "../dataSets/Nov28";
			startImg = 3457;
			endImg = 5334;
			break;
	}

	string imgDir;
	imgDir = dataDir + "/video";

	vector<pair<int, Time>> imgIdList;
	// preload all images
	list<pair<int, shared_ptr<cv::Mat>>> imgList;
	int imgId = startImg;
	int numImages;
	numImages = endImg-startImg-1;
	for(int i=0; i<numImages; i++)
	{
//		cout << "Loading image " << i << ": " << imgId << endl;
		cv::Mat img;
		while(img.data == NULL)
		{
			stringstream ss;
			ss << "image_" << imgId++ << ".bmp";
			img = cv::imread(imgDir+"/"+ss.str());
			if(img.data == NULL)
				cout << "missing " << imgDir+"/"+ss.str() << endl;
		}

		shared_ptr<cv::Mat> pImg(new cv::Mat);
		img.copyTo(*pImg);

		imgList.push_back(pair<int, shared_ptr<cv::Mat>>(imgId, pImg));
	}

	// Camera calibration
	shared_ptr<cv::Mat> mCameraMatrix_640x480, mCameraMatrix_320x240, mCameraDistortionCoeffs;
	cv::FileStorage fs;
	string filename = dataDir + "/s3Calib_640x480.yml";
	fs.open(filename.c_str(), cv::FileStorage::READ);
	if( fs.isOpened() )
	{
		mCameraMatrix_640x480 = shared_ptr<cv::Mat>(new cv::Mat());
		mCameraDistortionCoeffs = shared_ptr<cv::Mat>(new cv::Mat());

		String str = String()+"Camera calib loaded from " + filename.c_str();
		fs["camera_matrix"] >> *mCameraMatrix_640x480;
		fs["distortion_coefficients"] >> *mCameraDistortionCoeffs;
		str = str+"\n\t"+"Focal length: " + mCameraMatrix_640x480->at<double>(0,0);
		str = str+"\n\t"+"centerX: " + mCameraMatrix_640x480->at<double>(0,2);
		str = str+"\n\t"+"centerY: " + mCameraMatrix_640x480->at<double>(1,2);
		Log::alert(str);

		mCameraMatrix_320x240 = shared_ptr<cv::Mat>(new cv::Mat());
		mCameraMatrix_640x480->copyTo( *mCameraMatrix_320x240 );
		(*mCameraMatrix_320x240) = (*mCameraMatrix_320x240)*0.5;
	}
	else
	{
		Log::alert(("Failed to open " + filename).c_str());
	}
	fs.release();

	//
	Array2D<double> mRotViconToQuad = createRotMat(0, (double)PI);
	Array2D<double> mRotQuadToPhone = matmult(createRotMat(2,-0.25*PI),
											  createRotMat(0,(double)PI));
	Array2D<double> mRotCamToPhone = matmult(createRotMat(2,-0.5*(double)PI),
											 matmult(createRotMat(1,0.0),
												 	 createRotMat(0,(double)PI)));
	Array2D<double> mRotPhoneToCam = transpose(mRotCamToPhone);
	Array2D<double> mRotViconToPhone = matmult(mRotQuadToPhone, mRotViconToQuad);

	// make the same workers as I use in Rover
	TranslationController mTranslationController;
	AttitudeThrustController mAttitudeThrustController;
	Observer_Angular mObsvAngular;
	Observer_Translational mObsvTranslational;
	DataLogger mDataLogger;
	VelocityEstimator mVelocityEstimator;
	FeatureFinder mFeatureFinder;
	RegionFinder mRegionFinder;
//	SensorManager mSensorManager;
	MotorInterface mMotorInterface;
//	CommManager mCommManager;
//	VideoMaker mVideoMaker;

	Time startTime;

	// Make the appropriate connections
	TrackedObject::setObserverAngular(&mObsvAngular);
	TrackedObject::setObserverTranslational(&mObsvTranslational);

	mTranslationController.setRotViconToPhone(mRotViconToPhone);
	mTranslationController.setStartTime(startTime);
	mTranslationController.setDataLogger(&mDataLogger);
	mTranslationController.initialize();
	mTranslationController.setObserverTranslational(&mObsvTranslational);
	addCommManagerListener(&mTranslationController);

	mAttitudeThrustController.setStartTime(startTime);
	mAttitudeThrustController.setDataLogger(&mDataLogger);
	mAttitudeThrustController.setMotorInterface(&mMotorInterface);
	mAttitudeThrustController.initialize();
	mTranslationController.addListener(&mAttitudeThrustController);
	addCommManagerListener(&mAttitudeThrustController);

	mObsvAngular.initialize();
	mObsvAngular.setStartTime(startTime);
	mObsvAngular.setDataLogger(&mDataLogger);
	mObsvAngular.addListener(&mAttitudeThrustController);
	mObsvAngular.start();
	addCommManagerListener(&mObsvAngular);

	mObsvTranslational.setDataLogger(&mDataLogger);
	mObsvTranslational.setStartTime(startTime);
	mObsvTranslational.setRotViconToPhone(mRotViconToPhone);
	mObsvTranslational.setObserverAngular(&mObsvAngular);
	mObsvTranslational.initialize();
	mObsvTranslational.addListener(&mTranslationController);
	mObsvAngular.addListener(&mObsvTranslational);
	addCommManagerListener(&mObsvTranslational);
//	mObsvTranslational.addListener(&mObsvAngular);

	mFeatureFinder.initialize();
	mFeatureFinder.setStartTime(startTime);
	mFeatureFinder.setDataLogger(&mDataLogger);
	addSensorManagerListener(&mFeatureFinder);
	addCommManagerListener(&mFeatureFinder);
	mFeatureFinder.start();

	mRegionFinder.initialize();
	mRegionFinder.setStartTime(startTime);
	mRegionFinder.setDataLogger(&mDataLogger);
//	mRegionFinder.start();
	addSensorManagerListener(&mRegionFinder);
	addCommManagerListener(&mRegionFinder);

	ObjectTracker mObjectTracker;
	mObjectTracker.initialize();
	mObjectTracker.setStartTime(startTime);
	mObjectTracker.setDataLogger(&mDataLogger);
	mObjectTracker.setObserverTranslation(&mObsvTranslational);
	mObjectTracker.setObserverAngular(&mObsvAngular);
	mObjectTracker.addListener(&mObsvTranslational);
	mObjectTracker.addListener(&mObsvAngular);
	mObjectTracker.start();
	mFeatureFinder.addListener(&mObjectTracker);
	mRegionFinder.addListener(&mObjectTracker);

	mVelocityEstimator.initialize();
	mVelocityEstimator.setStartTime(startTime);
	mVelocityEstimator.setDataLogger(&mDataLogger);
	mVelocityEstimator.setObserverTranslational(&mObsvTranslational);
	mVelocityEstimator.setRotPhoneToCam(mRotPhoneToCam);
	mVelocityEstimator.addListener(&mObsvTranslational);
	mFeatureFinder.addListener(&mVelocityEstimator);
	mRegionFinder.addListener(&mVelocityEstimator);
	addCommManagerListener(&mVelocityEstimator);
	mVelocityEstimator.start();

	addSensorManagerListener(&mObsvAngular);
	addSensorManagerListener(&mObsvTranslational);

	mMotorInterface.addListener(&mTranslationController);
//	mMotorInterface.start();

	uint32 logMask = 0;
	logMask = LOG_FLAG_PC_UPDATES ;
	logMask |= LOG_FLAG_STATE;
	logMask |= LOG_FLAG_STATE_DES;
	logMask |= LOG_FLAG_MOTORS;
	logMask |= LOG_FLAG_OBSV_UPDATE;
	logMask |= LOG_FLAG_OBSV_BIAS;
//	logMask |= LOG_FLAG_MAGNOMETER;
//	logMask |= LOG_FLAG_ACCEL;
//	logMask |= LOG_FLAG_GYRO;
//	logMask |= LOG_FLAG_PRESSURE;
	logMask |= LOG_FLAG_CAM_RESULTS;
//	logMask |= LOG_FLAG_CAM_IMAGES;
//	logMask |= LOG_FLAG_PHONE_TEMP;
//	logMask |= LOG_FLAG_SONAR;
	mDataLogger.setStartTime(startTime);
	
	////////////////////////////////////////////////////////////////////////////////////
	// Add some vision event listeners so I can display the images

	cv::namedWindow("dispFeatureFind",1);
	cv::namedWindow("dispRegionFind",1);
	cv::namedWindow("dispObjectTrack",1);
	cv::moveWindow("dispFeatureFind",0,0);
	cv::moveWindow("dispRegionFind",321,0);
	cv::moveWindow("dispObjectTrack",642,0);

	class MyFeatureFinderListener : public FeatureFinderListener
	{
		public:
		void onFeaturesFound(const shared_ptr<ImageFeatureData> &data)
		{
			imshow("dispFeatureFind",*(data->imageAnnotatedData->imageAnnotated));
			cv::waitKey(1);
		}
	} myFeatureFinderListener;
	mFeatureFinder.addListener(&myFeatureFinderListener);

	class MyRegionFinderListener : public RegionFinderListener
	{
		public:
		void onRegionsFound(const shared_ptr<ImageRegionData> &data)
		{
			imshow("dispRegionFind",*(data->imageAnnotatedData->imageAnnotated));
//			cv::waitKey(1);
		}
	} myRegionFinderListener;
	mRegionFinder.addListener(&myRegionFinderListener);

	class MyObjectTrackerListenr : public ObjectTrackerListener
	{
		public:
		void onObjectsTracked(const shared_ptr<ObjectTrackerData> &data)
		{
			stringstream ss;
			ss << imgDir << "/annotated_target/img_" << imgCnt++ << "_" << data->imageData->imageId << ".bmp";
//			imwrite(ss.str().c_str(),*data->imageAnnotatedData->imageAnnotated);
			imshow("dispObjectTrack",*(data->imageAnnotatedData->imageAnnotated));
//			cv::waitKey(1);
		};

		int imgCnt;
		string imgDir;
	} myObjectTrackerListener;
	myObjectTrackerListener.imgCnt = 0;
	myObjectTrackerListener.imgDir = imgDir;
	mObjectTracker.addListener(&myObjectTrackerListener);

	////////////////////////////////////////////////////////////////////////////////////
	// Now to set parameters like they would have been online
	for(int i=0; i<commManagerListeners.size(); i++)
	{
		double gainP = 0.5;
		double gainI = 0.0001;
		double accelWeight = 1;
		double magWeight = 0.1*2*2*2;
		Collection<float> nomMag;
		nomMag.push_back(-21.2);
		nomMag.push_back(13.4);
		nomMag.push_back(-35.3);
		commManagerListeners[i]->onNewCommAttObserverGain(gainP, gainI, accelWeight, magWeight);
		commManagerListeners[i]->onNewCommNominalMag(nomMag);

		Collection<float> measVar;
		measVar.push_back(0.0001);
		measVar.push_back(0.0001);
		measVar.push_back(0.0001);
		measVar.push_back(0.01*2);
		measVar.push_back(0.01*2);
		measVar.push_back(0.01);
		commManagerListeners[i]->onNewCommKalmanMeasVar(measVar);

		Collection<float> dynVar;
		dynVar.push_back(0.05);
		dynVar.push_back(0.05);
		dynVar.push_back(0.05);
		dynVar.push_back(10);
		dynVar.push_back(10);
		dynVar.push_back(20);
		dynVar.push_back(0.01); // accel bias
		dynVar.push_back(0.01);
		dynVar.push_back(0.01);
		commManagerListeners[i]->onNewCommKalmanDynVar(dynVar);

		// TODO: Need to add Leash dialogs to send this over wifi
//		commManagerListeners[i]->onNewCommViconCameraOffset(0, 0.035, 0.087);
		commManagerListeners[i]->onNewCommTargetNominalLength(0.210);
		commManagerListeners[i]->onNewCommMAPHeightMeasCov(0.1*0.1);

		commManagerListeners[i]->onNewCommVisionFeatureFindQualityLevel(0.01);
		commManagerListeners[i]->onNewCommVisionFeatureFindSeparationDistance(20);
		commManagerListeners[i]->onNewCommVisionFeatureFindFASTThreshold(10);
		commManagerListeners[i]->onNewCommVisionFeatureFindPointCntTarget(50);
		commManagerListeners[i]->onNewCommVisionFeatureFindFASTAdaptRate(0.05);

		commManagerListeners[i]->onNewCommVelEstMeasCov(2*pow(5,2));
		commManagerListeners[i]->onNewCommVelEstProbNoCorr(0.0005);

		Collection<float> posGains(3), velGains(3);
		posGains[0] = 1;
		posGains[1] = 1;
		posGains[2] = 1;
		velGains[0] = 1;
		velGains[1] = 1;
		velGains[2] = 1;
		commManagerListeners[i]->onNewCommIbvsGains(posGains, velGains);

		Collection<float> desState(12,0.0);
		desState[8] = 1;
		commManagerListeners[i]->onNewCommDesState(desState);

		commManagerListeners[i]->onNewCommForceGain(0.0022);
		commManagerListeners[i]->onNewCommTorqueGain(0.0005);
		commManagerListeners[i]->onNewCommMass(1.2);

		Collection<float> attCntlGains;
		attCntlGains.push_back(0.2);
		attCntlGains.push_back(0.2);
		attCntlGains.push_back(0.2);
		attCntlGains.push_back(0.2);
		attCntlGains.push_back(0.2);
		attCntlGains.push_back(0.2);
		commManagerListeners[i]->onNewCommAttitudeGains(attCntlGains);

	}

	mDataLogger.setMask(logMask);
	mDataLogger.setDir(dataDir.c_str());
	mDataLogger.setFilename("obsvLog.txt");

	////////////////////////////////////////////////////////////////////////////////////
	// load data and manually feed them to the appropriate listener ... hopefully
	list<shared_ptr<IData>> dataList;
	dataList.clear();

	double accelOffX = -0.15;
	double accelOffY = 0.08;
	double accelOffZ = -0.4;

	double accelScaleX = 1;
	double accelScaleY = 1;
	double accelScaleZ = 1;

	int firstTime = -1;

	sched_param sp;
	sp.sched_priority = sched_get_priority_max(SCHED_NORMAL);
	sched_setscheduler(0, SCHED_NORMAL, &sp);

	Array2D<double> curViconState(12,1);;
	float curHeight = -1;
	bool haveFirstVicon = false;

	////////////////////////////////////////////////////////////////////////////////////
	// Run settings
	int endTimeDelta = 200e3;
	float viconUpdateRate = 100; // Hz
	int viconUpdatePeriodMS = 1.0f/viconUpdateRate*1000+0.5;
	float heightUpdateRate = 20; // Hz
	int heightUpdatePeriodMS = 1.0f/heightUpdateRate*1000+0.5;
	Time lastViconUpdateTime, lastHeightUpdateTime;

	srand(1);
	default_random_engine randGenerator;
	normal_distribution<double> stdGaussDist(0,1);
	Array2D<double> noiseStd(12,1,0.0);
	noiseStd[6][0] = 0.000;
	noiseStd[7][0] = 0.000;
	noiseStd[8][0] = 0.010;

	list<string> lines = loadLogFile(dataDir+"/phoneLog.txt");
	{

		for(int i=0; i<commManagerListeners.size(); i++)
			commManagerListeners[i]->onNewCommMotorOn();

		// gyro bias burn-in
		Array2D<double> gyroBias(3,1);
		gyroBias[0][0] = -0.006;
		gyroBias[1][0] = -0.008;
		gyroBias[2][0] = -0.008;
		shared_ptr<IData> gyroBiasData(new DataVector<double>());
		gyroBiasData->type = DATA_TYPE_GYRO;
		static_pointer_cast<DataVector<double> >(gyroBiasData)->dataRaw = gyroBias.copy();
		static_pointer_cast<DataVector<double> >(gyroBiasData)->dataCalibrated = gyroBias.copy();
		for(int i=0; i<2000; i++)
		{
			mObsvAngular.onNewSensorUpdate(gyroBiasData);
			System::usleep(700);
		}

		list<string>::const_iterator lineIter = lines.begin();
		list<pair<int, shared_ptr<cv::Mat>>>::const_iterator imageIter = imgList.begin();
		toadlet::uint64 lastDispTimeMS;
		while(lineIter != lines.end() && startTime.getElapsedTimeMS() < firstTime+endTimeDelta)
		{
			stringstream ss(*lineIter);
			double time;
			int type;
			ss >> time >> type;

			if(firstTime == -1)
			{
				firstTime = time;
				lastDispTimeMS = time;
				Time now;
				startTime.setTimeMS(now.getMS()-firstTime);

				mTranslationController.setStartTime(startTime);
				mAttitudeThrustController.setStartTime(startTime);
				mObsvAngular.setStartTime(startTime);
				mObsvTranslational.setStartTime(startTime);
				mFeatureFinder.setStartTime(startTime);
//				mTargetFinder.setStartTime(startTime);
				mObjectTracker.setStartTime(startTime);
				mVelocityEstimator.setStartTime(startTime);
				mDataLogger.setStartTime(startTime);

				mDataLogger.start();
				mObsvTranslational.start();
				mAttitudeThrustController.start();
				mTranslationController.start();
				cout << "Time: " << time << endl;
			}
			
			while(startTime.getElapsedTimeMS() < time)
				System::usleep(10);

			if(time - lastDispTimeMS > 5e3)
			{
				cout << "Time: " << time << endl;
				lastDispTimeMS = time;
			}

			// Vicon updates
			if(lastViconUpdateTime.getElapsedTimeMS() > viconUpdatePeriodMS && haveFirstVicon)
			{
				lastViconUpdateTime.setTime();
				toadlet::egg::Collection<float> state;
				for(int i=0; i<curViconState.dim1(); i++)
					state.push_back(curViconState[i][0] + noiseStd[i][0]*stdGaussDist(randGenerator) );
				for(int i=0; i<commManagerListeners.size(); i++)
					commManagerListeners[i]->onNewCommStateVicon(state);
			}

			// Height "sensor"
			if(lastHeightUpdateTime.getElapsedTimeMS() > heightUpdatePeriodMS && curHeight > 0)
			{
				double height = curHeight;
				height = (int)(100.0*height+100.0*noiseStd[8][0]*stdGaussDist(randGenerator));
				height /= 100.0;
				lastHeightUpdateTime.setTime();
				shared_ptr<HeightData<double>> heightData(new HeightData<double>);
				heightData->type = DATA_TYPE_HEIGHT;
				heightData->heightRaw = height-0.1;
				heightData->height = height-0.1;

				for(int i=0; i<sensorManagerListeners.size(); i++)
					sensorManagerListeners[i]->onNewSensorUpdate(heightData);
			}

			// Start IBVS
//			if(startTime.getElapsedTimeMS() > 20e3)
			if(curHeight > 0.5)
				for(int i=0; i<commManagerListeners.size(); i++)
					commManagerListeners[i]->onNewCommUseIbvs(true);

			// Sensor updates
			shared_ptr<IData> data = NULL;
			switch(type)
			{
				case LOG_ID_ACCEL:
					{
						int dataTime;
						if(dataSet == 0)
							dataTime = time;
						else
							ss >> dataTime;

						Array2D<double> accel(3,1), accelCal(3,1);
						ss >> accel[0][0] >> accel[1][0] >> accel[2][0];
//						accelCal[0][0] = (accel[0][0]-accelOffX)/accelScaleX;
//						accelCal[1][0] = (accel[1][0]-accelOffY)/accelScaleY;
//						accelCal[2][0] = (accel[2][0]-accelOffZ)/accelScaleZ;
						accelCal[0][0] = (accel[0][0]-accelOffX)*accelScaleX;
						accelCal[1][0] = (accel[1][0]-accelOffY)*accelScaleY;
						accelCal[2][0] = (accel[2][0]-accelOffZ)*accelScaleZ;

						data = shared_ptr<IData>(new DataVector<double>());
						data->type = DATA_TYPE_ACCEL;
						static_pointer_cast<DataVector<double> >(data)->dataRaw = accel;
						static_pointer_cast<DataVector<double> >(data)->dataCalibrated = accelCal;

						data->timestamp.setTimeMS(startTime.getMS()+dataTime);
						for(int i=0; i<sensorManagerListeners.size(); i++)
							sensorManagerListeners[i]->onNewSensorUpdate(data);
					}
					break;
				case LOG_ID_GYRO:
					{
						int dataTime;
						if(dataSet == 0)
							dataTime = time;
						else
							ss >> dataTime;

						Array2D<double> gyro(3,1);
						ss >> gyro[0][0] >> gyro[1][0] >> gyro[2][0];

						data = shared_ptr<IData>(new DataVector<double>());
						data->type = DATA_TYPE_GYRO;
						static_pointer_cast<DataVector<double> >(data)->dataRaw = gyro;
						static_pointer_cast<DataVector<double> >(data)->dataCalibrated = gyro;

						data->timestamp.setTimeMS(startTime.getMS()+dataTime);
						for(int i=0; i<sensorManagerListeners.size(); i++)
							sensorManagerListeners[i]->onNewSensorUpdate(data);
					}
					break;
				case LOG_ID_MAGNOMETER:
					{
						int dataTime;
						if(dataSet == 0)
							dataTime = time;
						else
							ss >> dataTime;

						Array2D<double> mag(3,1);
						ss >> mag[0][0] >> mag[1][0] >> mag[2][0];

						data = shared_ptr<IData>(new DataVector<double>());
						data->type = DATA_TYPE_MAG;
						static_pointer_cast<DataVector<double> >(data)->dataRaw = mag;
						static_pointer_cast<DataVector<double> >(data)->dataCalibrated = mag;

						data->timestamp.setTimeMS(startTime.getMS()+dataTime);
						for(int i=0; i<sensorManagerListeners.size(); i++)
							sensorManagerListeners[i]->onNewSensorUpdate(data);
					}
					break;
				case LOG_ID_PRESSURE:
					break;
				case LOG_ID_MOTOR_CMDS:
					break;
				case LOG_ID_PHONE_TEMP:
					break;
				case LOG_ID_RECEIVE_VICON:
					// Ideally I'd pull this directly from the Leash log file, but that's work :)
					{
						for(int i=0; i<12; i++)
							ss >> curViconState[i][0];
						curHeight = curViconState[8][0];
						haveFirstVicon = true;
					}
					break;
				case LOG_ID_IMAGE:
					{
						int dataTime;
						if(dataSet == 0)
							dataTime = time;
						else
							ss >> dataTime;

						int imgId;
						ss >> imgId;
						imageIter = imgList.begin();
						if(imageIter != imgList.end() && imageIter->first > imgId)
						{
							// This happens because the images occurred before the motors were
							// turn on, which is the cue to start saving images to file
//							cout << "We've got some image chad going on here.\t";
//							cout << "imageIter->firt = " << imageIter->first << "\t";
//							cout << "imgId = " << imgId << endl;
							break;
						}
						while(imageIter != imgList.end() && imageIter->first < imgId)
							imageIter++;
						if(imageIter != imgList.end())
						{
							shared_ptr<DataImage> data(new DataImage());
							data->type = DATA_TYPE_IMAGE;
							data->timestamp.setTimeMS(startTime.getMS()+dataTime);
							data->image = imageIter->second;

							SO3 att = mObsvAngular.estimateAttAtTime( data->timestamp );
							data->att = att;

							shared_ptr<cv::Mat> gray(new cv::Mat());
							cv::cvtColor(*(data->image), *gray, CV_BGR2GRAY);
							data->imageGray = gray;
							data->imageFormat = IMG_FORMAT_BGR;
							data->cap = NULL;
							if(data->image->rows = 240)
								data->cameraMatrix = mCameraMatrix_320x240;
							else
								data->cameraMatrix = mCameraMatrix_640x480;
							data->focalLength = data->cameraMatrix->at<double>(0,0);
							data->center.x = data->cameraMatrix->at<double>(0,2);
							data->center.y = data->cameraMatrix->at<double>(1,2);
							data->distCoeffs = mCameraDistortionCoeffs;

							for(int i=0; i<sensorManagerListeners.size(); i++)
								sensorManagerListeners[i]->onNewSensorUpdate(static_pointer_cast<IData>(data));
						}
					}
					break;
				default:
					cout << "Unknown data type: " << type << endl;
			}

			lineIter++;
		}

	}

	mDataLogger.shutdown();
	mTranslationController.shutdown();
	mAttitudeThrustController.shutdown();
//	mCommManager.shutdown();
	mVelocityEstimator.shutdown();
	mFeatureFinder.shutdown();
//	mTargetFinder.shutdown();
//	mVideoMaker.shutdown();
	mObsvAngular.shutdown(); 
	mObsvTranslational.shutdown(); 
//	mSensorManager.shutdown();
	mMotorInterface.shutdown();

    return 0;
}

std::list<std::string> loadLogFile(std::string filename)
{
	using namespace std;
	using namespace ICSL::Quadrotor;

	string line;
	list<string> lines;
	ifstream file(filename.c_str());
	if(file.is_open())
	{
		getline(file, line); // first line is a throw-away
		getline(file, line); // second line is also a throw-away
		
		while(file.good())
		{
			getline(file, line);
			stringstream ss(line);
			double time;
			int type;
			ss >> time >> type;

			if(type == LOG_ID_ACCEL ||
			   type == LOG_ID_GYRO ||
			   type == LOG_ID_MAGNOMETER ||
			   type == LOG_ID_RECEIVE_VICON ||
			   type == LOG_ID_IMAGE)
				lines.push_back(line);
		}

		file.close();
	}
	else
		cout << "Failed to open file: " << filename << endl;

	cout << "Loaded " << lines.size() << " lines" << endl;
	return lines;
}
