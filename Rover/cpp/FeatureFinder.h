#ifndef ICSL_FEATUREFINDER_H
#define ICSL_FEATUREFINDER_H
#include <memory>
#include <sched.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <thread>
#include <mutex>
#include <cmath>
#include <list>

#include <opencv2/core/core.hpp>
#include <opencv2/imgproc/imgproc.hpp>

#include <toadlet/egg.h>

#include "constants.h"
#include "DataLogger.h"
#include "Common.h"
#include "Time.h"
#include "Data.h"
#include "Listeners.h"

namespace ICSL {
namespace Quadrotor {
class FeatureFinder : public CommManagerListener,
						public SensorManagerListener
{
	public:
	explicit FeatureFinder();
	virtual ~FeatureFinder(){};

	void shutdown();
	void start(){ thread th(&FeatureFinder::run, this); th.detach(); }
	void initialize();
	void setThreadPriority(int sched, int priority){mScheduler = sched; mThreadPriority = priority;};
	void setThreadNice(int nice){mThreadNiceValue = nice;};
	void setStartTime(Time t){mStartTime = t;}
	void setDataLogger(DataLogger *log){mDataLogger = log;}

	int getImageProcTimeMS(){mMutex_data.lock(); int temp = mImageProcTimeUS/1000.0; mMutex_data.unlock(); return temp;}
	int getImageProcTimeUS(){mMutex_data.lock(); int temp = mImageProcTimeUS; mMutex_data.unlock(); return temp;}
	void getLastImage(cv::Mat *outImage);
	void getLastImageAnnotated(cv::Mat *outImage);
	toadlet::egg::Collection<int> getVisionParams();

	static vector<cv::Point2f> findFeaturePoints(const cv::Mat &image, 
														 double qualityLevel,
														 double minDistance,
														 int fastThreshold);
	static void drawPoints(const vector<cv::Point2f> &points, cv::Mat &img);

	void addListener(FeatureFinderListener *listener){mListeners.push_back(listener);}

	// CommManagerListener functions
	void onNewCommVisionFeatureFindQualityLevel(float qLevel);
	void onNewCommVisionFeatureFindSeparationDistance(int sepDist);
	void onNewCommVisionFeatureFindFASTThreshold(int thresh);
	void onNewCommVisionFeatureFindPointCntTarget(int target);
	void onNewCommVisionFeatureFindFASTAdaptRate(float r);
	void onNewCommMotorOn(){mIsMotorOn = true;};
	void onNewCommMotorOff(){mIsMotorOn = false;};
	
	// SensorManagerListener
	void onNewSensorUpdate(const shared_ptr<IData> &data);

	protected:
	bool mUseIbvs;
	bool mRunning, mFinished;
	bool mNewImageReady; //, mNewImageReady_targetFind;
	bool mHaveUpdatedSettings;
	bool mIsMotorOn;

	shared_ptr<DataImage> mImageDataNext;
	shared_ptr<DataAnnotatedImage> mImageAnnotatedLast;

	Time mStartTime, mLastProcessTime;

	uint32_t mImageProcTimeUS;

	DataLogger *mDataLogger;

	std::mutex mMutex_data, mMutex_image, mMutex_imageData, mMutex_buffers;
	std::mutex mMutex_logger;
	std::mutex mMutex_params;

	toadlet::egg::Collection<FeatureFinderListener*> mListeners;

	float mQualityLevel, mFASTThreshold, mFASTAdaptRate;
	int mSepDist, mPointCntTarget;

	void run();

	int mThreadPriority, mScheduler, mThreadNiceValue;

	static void eigenValResponses(const cv::Mat& img, vector<cv::KeyPoint>& pts, int blockSize);
};

} // namespace Quadrotor
} // namespace ICSL

#endif
