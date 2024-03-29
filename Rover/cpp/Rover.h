#ifndef ROVER_H
#define ROVER_H
#include <memory>
#include <fstream>
#include <sstream>
#include <thread>
#include <mutex>
#include <sched.h>
#include <sys/time.h>
#include <sys/resource.h>

#include <cpu-features.h>

#include <opencv2/core/core.hpp>

#include <toadlet/egg.h>

#include "TNT/tnt.h"
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
#include "SensorManager.h"
#include "VideoMaker.h"
#include "MotorInterface.h"
#include "FeatureFinder.h"
#include "RegionFinder.h"
#include "ObjectTracker.h"
#include "VelocityEstimator.h"
#include "Listeners.h"

namespace ICSL {
namespace Quadrotor {
class Rover: public Observer_AngularListener,
				 public CommManagerListener,
				 public SensorManagerListener,
				 public FeatureFinderListener,
				 public RegionFinderListener,
				 public ObjectTrackerListener
{
public:
	Rover();
	virtual ~Rover();

	void initialize();
	void shutdown();

	void setLogFilename(String name);
	void setLogDir(String dir);
	void startLogging();
	void stopLogging();

	void start(){ thread th(&Rover::run, this); th.detach(); }
	void run();

	void setThreadPriority(int sched, int priority){mScheduler = sched; mThreadPriority = priority;};
	void setThreadNice(int nice){mThreadNiceValue = nice;};
	
	// these functions are primarily for the jni interface
	void copyImageData(cv::Mat *m);
	TNT::Array2D<double> getGyroValue();
	TNT::Array2D<double> getAccelValue();
	TNT::Array2D<double> getMagValue();
	TNT::Array2D<double> getAttitude();
	int getImageProcTimeMS(){mMutex_vision.lock(); int temp = mVelocityEstimator.getLastVisionDelayTimeUS()/1.0e3; mMutex_vision.unlock(); return temp;}
	bool pcIsConnected(){return mCommManager.pcIsConnected();}
	void passNewImage(cv::Mat *img, int64_t const &timestampNS){mSensorManager.passNewImage(img, timestampNS);}
	void onNewSonarReading(int heightMM, uint64_t timestampNS){mSensorManager.onNewSonarReading(heightMM, timestampNS);}

	// Observer_AngularListener
	void onObserver_AngularUpdated(const shared_ptr<SO3Data<double>> &attData, const shared_ptr<DataVector<double>> &angularVelData);

	// for CommManagerListener
	void onNewCommTimeSync(int time);
	void onNewCommLogTransfer();
	void onNewCommLogMask(uint32_t mask);
	void onNewCommLogClear();

	// for SensorManagerListener
	void onNewSensorUpdate(shared_ptr<IData> const &data);

	// for FeatureFinderListener
	void onFeaturesFound(shared_ptr<ImageFeatureData> const &data);

	// for RegionFinderListener
	void onRegionsFound(shared_ptr<ImageRegionData> const &data);

	// for ObjectTrackerListener
	void onObjectsTracked(const shared_ptr<ObjectTrackerData> &data);
protected:
	CommManager mCommManager;
	bool mRunning, mRunnerIsDone;
	bool mDataIsSending, mImageIsSending;

	TranslationController mTranslationController;
	AttitudeThrustController mAttitudeThrustController;

	Observer_Angular mObsvAngular;
	Observer_Translational mObsvTranslational;
	TNT::Array2D<double> mCurAtt, mCurAngularVel;

	Time mStartTime, mLastDataSendTime, mLastImageSendTime;

	TNT::Array2D<double> mRotViconToQuad, mRotQuadToPhone, mRotCamToPhone, mRotPhoneToCam, mRotViconToPhone;

	std::mutex mMutex_cntl, mMutex_observer, mMutex_vision, mMutex_vicon, mMutex_data;
	
	void transmitDataUDP();
	void transmitImage();

	DataLogger mDataLogger;

	VelocityEstimator mVelocityEstimator;
	FeatureFinder mFeatureFinder;
	RegionFinder mRegionFinder;
	ObjectTracker mObjectTracker;

	SensorManager mSensorManager;

	bool mUseIbvs;

	static TNT::Array2D<int> getCpuUsage(int numCpuCores);

	int mNumCpuCores;

	double mPressure, mPhoneTemp;

	shared_ptr<DataImage> mImageData;
	shared_ptr<ImageFeatureData> mFeatureData;
	shared_ptr<ImageRegionData> mRegionData;
	shared_ptr<ObjectTrackerData> mObjectData;

	VideoMaker mVideoMaker;
	
	MotorInterface mMotorInterface;

	int mThreadPriority, mScheduler, mThreadNiceValue;

	int getCpuFreq();
}; // class Rover

} // namespace Quadrotor
} // namespace ICSL

#endif
