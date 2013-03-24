#ifndef ICSL_SENSOR_MANAGER
#define ICSL_SENSOR_MANAGER
#include <sched.h>

#include <memory>
#include <string>
#include <fstream>
#include <sstream>

#include <android/sensor.h>
#include <opencv2/core/core.hpp>
#include <toadlet/egg.h>

#include "TNT/tnt.h"

#include <opencv2/core/core.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/imgproc/imgproc.hpp>
#include <opencv2/features2d/features2d.hpp>
#include <opencv2/video/tracking.hpp>

#include "constants.h"

#include "Common.h"
#include "QuadLogger.h"
#include "Time.h"
#define ICSL_OBSERVER_ANGULAR_LISTENER_ONLY
#include "Observer_Angular.h"
#undef ICSL_OBSERVER_ANGULAR_LISTENER_ONLY

namespace ICSL{
namespace Quadrotor{
using namespace std;
static const int ASENSOR_TYPE_PRESSURE=6; // not yet defined for NDK

class SensorManagerListener
{
	public:
	virtual void onNewSensorUpdate(shared_ptr<Data> const &data)=0;
};

class SensorManager : public toadlet::egg::Thread, public Observer_AngularListener
{
	public:
	SensorManager();
	virtual ~SensorManager();

	void run();
	void initialize();
	void shutdown();
	void setThreadPriority(int sched, int priority){mScheduler = sched; mThreadPriority = priority;};

	void setQuadLogger(QuadLogger *log){mQuadLogger = log;}
	void setStartTime(Time time){mMutex_startTime.lock(); mStartTime = time; mTimestampOffsetNS = 0; mMutex_startTime.unlock();}

	void addListener(SensorManagerListener *l){mMutex_listeners.lock(); mListeners.push_back(l); mMutex_listeners.unlock();}

	// for Observer_AngularListener
	void onObserver_AngularUpdated(shared_ptr<DataVector> attData, shared_ptr<DataVector> angularVelData);

	protected:
	bool mRunning, mDone;
	ASensorManager* mSensorManager;
	ASensorEventQueue* mSensorEventQueue;
	const ASensor *mAccelSensor, *mGyroSensor, *mMagSensor, *mPressureSensor;

	static int getBatteryTemp();
	static int getSecTemp();
	static int getFuelgaugeTemp();
	static int getTmuTemp();
	void runTemperatureMonitor();

	QuadLogger *mQuadLogger;

	TNT::Array2D<double> mLastAccel, mLastGyro, mLastMag;
	double mLastPressure;
	toadlet::egg::Mutex mMutex_data, mMutex_listeners, mMutex_startTime, mMutex_attData;

	Time mStartTime;

	Collection<SensorManagerListener *> mListeners;

	shared_ptr<cv::VideoCapture> initCamera();
	void runImgAcq(shared_ptr<cv::VideoCapture> cap);
	TNT::Array2D<double> mCurAtt, mCurAngularVel;

	TNT::Array2D<double> mRotCamToPhone, mRotPhoneToCam;

	int64_t mTimestampOffsetNS;
	int mThreadPriority, mScheduler;
};


} // namespace Quadrotor
} // namespace ICSL
#endif
