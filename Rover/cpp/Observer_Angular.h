#ifndef ICSL_OBSERVER_ANGULAR 
#define ICSL_OBSERVER_ANGULAR 
#include <memory>
#include <sched.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <thread>
#include <mutex>
#include <unordered_map>
#include <functional>

#include <toadlet/egg.h>
#include "TNT_Utils.h"
//#include "TNT/jama_lu.h"
//#include "TNT/jama_svd.h"

#include "Data.h"
#include "constants.h"
#include "TNT/tnt.h"
#include "DataLogger.h"
#include "Common.h"
#include "Time.h"
#include "TrackedObject.h"
#include "Listeners.h"
#include "TrackedObject.h"

namespace ICSL{
namespace Quadrotor{

using namespace std;

class Observer_Angular : public CommManagerListener,
						 public SensorManagerListener,
						 public ObjectTrackerListener//,
//						 public Observer_TranslationalListener
{
	public:
	Observer_Angular();
	virtual ~Observer_Angular();

	void doInnovationUpdate(double dt, const shared_ptr<DataVector<double>> &accelData, const shared_ptr<DataVector<double>> &magData);
	void doGyroUpdate(double dt, const shared_ptr<DataVector<double>> &gyroData);

	SO3 getCurAttitude();
	TNT::Array2D<double> getCurVel();
	TNT::Array2D<double> getBias();
	TNT::Array2D<double> getLastGyro();
	TNT::Array2D<double> getLastAccel();
	TNT::Array2D<double> getLastMagnometer();
	double getGainP(){return mGainP;};
	double getGainI(){return mGainI;};

	void setGainP(double p){mMutex_data.lock(); mGainP = p; mMutex_data.unlock();};
	void setGainI(double i){mMutex_data.lock(); mGainI = i; mMutex_data.unlock();};
	void setWeights(double accelWeight, double magWeight);
	void setStartTime(Time time){mStartTime = time;}
	void setYawZero();

	void reset();
	bool doingBurnIn(){return mDoingBurnIn;}
	
	void addListener(Observer_AngularListener* chad){mListeners.push_back(chad);};

	void setDataLogger(DataLogger *log){mDataLogger = log;}

	void addDirectionMeasurement(const TNT::Array2D<double> &dirMeas, const TNT::Array2D<double> &dirInertial, double weight);

	void initialize();
	void start(){ thread th(&Observer_Angular::run, this); th.detach(); }
	void run();
	void shutdown();

	void setThreadPriority(int sched, int priority){mScheduler = sched; mThreadPriority = priority;};
	void setThreadNice(int nice){mThreadNiceValue = nice;};

	SO3 estimateAttAtTime(const Time &t);

	// CommManagerListener functions
	void onNewCommObserverReset();
	void onNewCommAttObserverGain(double gainP, double gainI, double accelWeight, double magWeight);
	void onNewCommNominalMag(const toadlet::egg::Collection<float> &nomMag);
	void onNewCommStateVicon(const toadlet::egg::Collection<float> &data);
	void onNewCommUseIbvs(bool useIbvs);
	
	// for SensorManagerListener
	void onNewSensorUpdate(shared_ptr<IData> const &data);

	// for ObjectTrackerListener
	void onObjectsTracked(const shared_ptr<ObjectTrackerData> &data);

	// for Observer_TranslationalListener
//	void onObserver_TranslationalUpdated(const TNT::Array2D<double> &pos, const TNT::Array2D<double> &vel){};
//	void onObserver_TranslationalImageProcessed(const shared_ptr<ImageTranslationData> &data);

	protected:
	bool mRunning, mDone;
	bool mNewAccelReady, mNewGyroReady, mNewMagReady;
	bool mDoingBurnIn;
	double mGainP, mGainI;
	double mAccelWeight, mMagWeight;
	TNT::Array2D<double> mGyroBias;
	TNT::Array2D<double> mInnovation, mVisionInnovation;
	TNT::Array2D<double> mCurVel;
	SO3 mCurAttitude;
	shared_ptr<DataVector<double>>  mAccelData, mGyroData, mMagData; // use this for copying data from SensorManager updates
	TNT::Array2D<double> mAccelDirNom, mMagDirNom;
	toadlet::egg::Collection<TNT::Array2D<double>> mExtraDirsMeasured, mExtraDirsInertial;
	toadlet::egg::Collection<double> mExtraDirsWeight;

	Time mStartTime;
	int mBurnCount;
	std::mutex mMutex_data, mMutex_cache, mMutex_visionInnovation;

	toadlet::egg::Collection<Observer_AngularListener*> mListeners;

	DataLogger *mDataLogger;

	int mThreadPriority, mScheduler, mThreadNiceValue;

	list<shared_ptr<SO3Data<double>>> mSO3Buffer;
	std::mutex mMutex_SO3Buffer;

	bool mIsDoingIbvs;
	Time mLastObjectTrackedTime;
	std::mutex mMutex_targetFindTime;

	TNT::Array2D<double> mRotCamToPhone, mRotPhoneToCam;

	TNT::Array2D<double> mLastGoodAccel;

	// adapted from http://stackoverflow.com/questions/17016175/c-unordered-map-using-a-custom-class-type-as-the-key
//	class KeyHasher
//	{
//		public:
//		size_t operator()(const pair<size_t, size_t> &p) const
//		{ return hash<size_t>()(p.first) ^ (hash<size_t>()(p.second << 1) >> 1); }
//	};

//	unordered_map<size_t, shared_ptr<TrackedObject>> mObjectMap;
//	// the nominal direction vector between the pair of regions
//	// identified in the key
//	unordered_map<pair<size_t, size_t>, TNT::Array2D<double>, KeyHasher> mNominalDirMap;
//	unordered_map<pair<size_t, size_t>, Time, KeyHasher> mNominalDirCreateTime;
};

}
}

#endif
