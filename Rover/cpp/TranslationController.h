#ifndef ICSL_TRANSLATIONALCONTROLLER
#define ICSL_TRANSLATIONALCONTROLLER
#include <sched.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <thread>

#include "TNT/tnt.h"

#include "toadlet/egg.h"

#include "constants.h"

#include "DataLogger.h"
#include "Time.h"
#include "Observer_Translational.h"
#include "Rotation.h"
#include "Listeners.h"

namespace ICSL {
namespace Quadrotor {
class TranslationController : 	public Observer_TranslationalListener,
								public CommManagerListener,
								public MotorInterfaceListener
{
	public:
	TranslationController();
	virtual ~TranslationController();

	void initialize();
	void start(){ thread th(&TranslationController::run, this); th.detach(); }
	void run();
	void shutdown();
	void setThreadPriority(int sched, int priority){mScheduler = sched; mThreadPriority = priority;};
	void setThreadNice(int nice){mThreadNiceValue = nice;};

	const TNT::Array2D<double> getDesiredState(){mMutex_data.lock(); TNT::Array2D<double> tempState = mDesState.copy(); mMutex_data.unlock(); return tempState;}
	const TNT::Array2D<double> getCurState(){mMutex_data.lock(); TNT::Array2D<double> tempState = mCurState.copy(); mMutex_data.unlock(); return tempState;}
	const TNT::Array2D<double> getErrorMemory(){mMutex_data.lock(); TNT::Array2D<double> tempInt = mErrInt.copy(); mMutex_data.unlock(); return tempInt;}

	void calcControl();
	void reset();

	void setStartTime(Time t){mStartTime = t;}
	void setDataLogger(DataLogger *log){mDataLogger = log;}
	void setRotViconToPhone(const TNT::Array2D<double> &rot){mRotViconToPhone.inject(rot);}
	void setDesPosAccel(const TNT::Array2D<double> &a);

	void addListener(TranslationControllerListener* listener){mListeners.push_back(listener);}

	void setObserverTranslational(Observer_Translational *obsv){mObsvTranslational = obsv;}

	// from CommManagerListener
	void onNewCommTransGains(const toadlet::egg::Collection<float> &gains);
	void onNewCommMass(float m){mMass = m;}
	void onNewCommDesState(const toadlet::egg::Collection<float> &data);
	void onNewCommSetDesiredPos();
	void onNewCommMotorOn(){reset();}
	void onNewCommMotorOff(){reset();}
	void onNewCommSendControlSystem(const Collection<toadlet::tbyte> &buff);
	void onNewCommUseIbvs(bool useIbvs){mUseIbvs = useIbvs;}
	void onNewCommIbvsGains(const toadlet::egg::Collection<float> &posGains, const toadlet::egg::Collection<float> &VELgAINS);
	void onNewCommStateVicon(const toadlet::egg::Collection<float> &data);

	// for Observer_TranslationalListener
	void onObserver_TranslationalUpdated(const TNT::Array2D<double> &pos, const TNT::Array2D<double> &vel);
	void onObserver_TranslationalImageProcessed(const shared_ptr<ImageTranslationData> &data);

	// for MotorInterfaceListener
	void onMotorWarmupDone(){reset();Log::alert("Tran Controller Received motor warmup done");}
	
	// for TargetFinderListener
//	void onTargetFound(const shared_ptr<ImageTargetFindData> &data);

	protected:
	bool mRunning, mDone;
	bool mNewMeasAvailable;
	bool mUseIbvs;
	DataLogger *mDataLogger;
	TNT::Array2D<double> mCurState, mDesState, mDesPosAccel;
	TNT::Array2D<double> mGainP, mGainD, mGainI;
	TNT::Array2D<double> mErrInt, mErrIntLimit;
	TNT::Array2D<double> mRotViconToPhone;
	TNT::Array2D<double> mDesAccel;
	double mMass;

	toadlet::egg::Mutex mMutex_data, mMutex_state;

	Time mStartTime, mLastControlTime;

	Collection<TranslationControllerListener*> mListeners;

	static double constrain(double val, double minVal, double maxVal)
	{ return min(maxVal, max(minVal, val)); }

	TNT::Array2D<double> calcControlPID(const TNT::Array2D<double> &error, double dt);

	TNT::Array2D<double> calcControlIBVS2(TNT::Array2D<double> &error, double dt);
	TNT::Array2D<double> mIbvsPosGains, mIbvsVelGains;
	std::mutex mMutex_gains;

	TNT::Array2D<double> mStateVicon;
	std::mutex mMutex_viconState;

	int mThreadPriority, mScheduler, mThreadNiceValue;

//	shared_ptr<ImageTargetFindData> mTarget2Data;
	shared_ptr<ImageTranslationData> mTargetTranslationData;
	std::mutex mMutex_target;

	SO3 mRotPhoneToCam, mRotCamToPhone;

	enum class Controller
	{
		PID,
		SYSTEM,
		IBVS
	};
	Controller mLastController;

	Observer_Translational *mObsvTranslational;
	std::mutex mMutex_obsvTran;
};

} // namespace Quadrotor
} // namespace ICSL

#endif
