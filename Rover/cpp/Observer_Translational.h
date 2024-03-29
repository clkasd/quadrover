#ifndef ICSL_OBSERVER_TRANSLATIONAL
#define ICSL_OBSERVER_TRANSLATIONAL
#include <memory>
#include <fstream>
#include <sched.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <thread>
#include <mutex>
#include <unordered_map>

#include <opencv2/core/core.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/imgproc/imgproc.hpp>
#include <opencv2/features2d/features2d.hpp>

#include "TNT/tnt.h"
#include "TNT/jama_lu.h"
#include "TNT/jama_qr.h"

#include "constants.h"

#include "Data.h"
#include "Time.h"
#include "DataLogger.h"
#include "Observer_Angular.h"
#include "Listeners.h"
#include "TrackedObject.h"

#include "toadlet/egg.h"

namespace ICSL{
namespace Quadrotor{
class Observer_Translational : public Observer_AngularListener,
								public CommManagerListener,
								public SensorManagerListener,
								public VelocityEstimatorListener,
								public ObjectTrackerListener
{
	public:
	Observer_Translational();
	virtual ~Observer_Translational();

	void initialize();
	void start(){ thread th(&Observer_Translational ::run, this); th.detach(); }
	void run();
	void shutdown();

	void setThreadPriority(int sched, int priority){mScheduler = sched; mThreadPriority = priority;};
	void setThreadNice(int nice){mThreadNiceValue = nice;};

	void setStartTime(Time t);
	void setDataLogger(DataLogger *log){mDataLogger = log;}
	void setRotViconToPhone(const TNT::Array2D<double> &rot){mRotViconToPhone.inject(rot);}

	void addListener(Observer_TranslationalListener *listener){mMutex_listeners.lock(); mListeners.push_back(listener); mMutex_listeners.unlock();}

	TNT::Array2D<double> estimateStateAtTime(const Time &t);
	TNT::Array2D<double> estimateErrCovAtTime(const Time &t);

	bool isTargetFound(){return mHaveFirstCameraPos;}

	void setObserverAngular(Observer_Angular *obsv){mObsvAngular = obsv;}

	// from Observer_AngularListener
	void onObserver_AngularUpdated(const shared_ptr<SO3Data<double>> &attData, const shared_ptr<DataVector<double>> &angularVelData);

	// from CommManagerListener
	void onNewCommStateVicon(const toadlet::egg::Collection<float> &data);
	void onNewCommKalmanMeasVar(const toadlet::egg::Collection<float> &var);
	void onNewCommKalmanDynVar(const toadlet::egg::Collection<float> &var);
	void onNewCommUseIbvs(bool useIbvs);
	void onNewCommAccelBias(float xBias, float yBias, float zBias);
	void onNewCommViconCameraOffset(float x, float y, float z);
	void onNewCommTargetNominalLength(float length);
	void onNewCommMAPHeightMeasCov(float cov);

	// for SensorManagerListener
	void onNewSensorUpdate(const shared_ptr<IData> &data);

	// for VelocityEstimatorListener
	void onVelocityEstimator_newEstimate(const shared_ptr<DataVector<double>> &velData,
										 const shared_ptr<Data<double>> &heightData);

	// for ObjectTrackerListener
	void onObjectsTracked(const shared_ptr<ObjectTrackerData> &data);

	protected:
	bool mRunning, mDone;
	bool mNewViconPosAvailable, mNewCameraPosAvailable;
	bool mUseViconPos, mUseCameraPos;
	bool mHaveFirstVicon;
	Time mStartTime;

	double mMAPHeightMeasCov;
	double mTargetNominalLength;
	TNT::Array2D<double> mViconCameraOffset;

	TNT::Array2D<double> mRotViconToPhone;
	DataLogger *mDataLogger;

	toadlet::egg::Collection<Observer_TranslationalListener*> mListeners;
	std::mutex mMutex_listeners;

	// for the translational Kalman Filter
	TNT::Array2D<double> mMeasCov, mPosMeasCov, mVelMeasCov; 
	TNT::Array2D<double> mDynCov, mErrCovKF;
	// State vector
	// 0. x
	// 1. y
	// 2. z
	// 3. x vel
	// 4. y vel
	// 5. z vel
	// 6. x accel bias
	// 7. y accel bias
	// 8. z accel bias
	TNT::Array2D<double> mStateKF;
	TNT::Array2D<double> mAccelBiasReset;

	std::mutex mMutex_events;
	std::mutex mMutex_kfData;
	std::mutex mMutex_accel, mMutex_gravDir;

	static void doTimeUpdateKF(const TNT::Array2D<double> &accel, 
							   double dt,
							   TNT::Array2D<double> &state,
							   TNT::Array2D<double> &errCov,
							   const TNT::Array2D<double> &dynCov,
							   const SO3 &att);
	static void doMeasUpdateKF_velOnly(const TNT::Array2D<double> &meas,
									   const TNT::Array2D<double> &measCov,
									   TNT::Array2D<double> &state,
									   TNT::Array2D<double> &errCov,
									   const SO3 &att);
	static void doMeasUpdateKF_posOnly(const TNT::Array2D<double> &meas,
									   const TNT::Array2D<double> &measCov,
									   TNT::Array2D<double> &state,
									   TNT::Array2D<double> &errCov,
									   const SO3 &att);
	static void doMeasUpdateKF_xyOnly(const TNT::Array2D<double> &meas,
									   const TNT::Array2D<double> &measCov,
									   TNT::Array2D<double> &state,
									   TNT::Array2D<double> &errCov,
									   const SO3 &att);
	static void doMeasUpdateKF_heightOnly(double meas, 
									      double measCov, 
										  TNT::Array2D<double> &state, 
										  TNT::Array2D<double> &errCov,
										  const SO3 &att);

	SO3 mRotCamToPhone, mRotPhoneToCam;
	SO3 mCurAtt;
	std::mutex mMutex_att;

	Observer_Angular *mObsvAngular;

	int mThreadPriority, mScheduler, mThreadNiceValue;

	vector<list<shared_ptr<IData>> *> mDataBuffers;
	list<shared_ptr<DataVector<double>>> mStateBuffer, mErrCovKFBuffer, mViconPosBuffer, mCameraPosBuffer;
	list<shared_ptr<DataVector<double>>> mViconVelBuffer, mCameraVelBuffer, /*mOpticFlowVelBuffer,*/ mMapVelBuffer;
	list<shared_ptr<Data<double>>> /*mHeightDataBuffer,*/ mMapHeightBuffer;
	list<shared_ptr<HeightData<double>>> mHeightDataBuffer;
	list<shared_ptr<DataVector<double>>> mRawAccelDataBuffer;
	list<shared_ptr<IData>> mNewEventsBuffer;

	bool mHaveFirstCameraPos;
	Time mLastCameraPosTime, mLastViconPosTime;
	std::mutex mMutex_posTime;

	bool mUseIbvs;

	Time applyData(list<shared_ptr<IData>> &events);

	bool mIsViconCameraOffsetSet;

	unordered_map<size_t, shared_ptr<TrackedObject>> mObjectMap;
	// the nominal position of each region when the quadrotor is
	// at the "origin"
	unordered_map<size_t, cv::Point2f> mObjectNominalPosMap;

	cv::Point2f mLastImageOffset;
};

} // namespace Quadrotor
} // namespace ICSL

#endif
