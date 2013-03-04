#ifndef ICSL_TRANSLATIONALCONTROLLER
#define ICSL_TRANSLATIONALCONTROLLER

#include "toadlet/egg.h"

#include "TNT/tnt.h"

#include "ICSL/constants.h"

#include "Time.h"
#include "CommManager.h"
#include "QuadLogger.h"
#include "Observer_Translational.h"

namespace ICSL {
namespace Quadrotor {

class TranslationControllerListener
{
	public:
	TranslationControllerListener(){};
	virtual ~TranslationControllerListener(){};

	virtual void onTranslationControllerAccelCmdUpdated(TNT::Array2D<double> const &accelCmd)=0;
};

class TranslationController : 	public toadlet::egg::Thread,
								public Observer_TranslationalListener,
								public CommManagerListener
{
	public:
	TranslationController();
	virtual ~TranslationController();

	void initialize();
	void run();
	void shutdown();

	TNT::Array2D<double> const getDesiredState(){mMutex_data.lock(); Array2D<double> tempState = mDesState.copy(); mMutex_data.unlock(); return tempState;}
	TNT::Array2D<double> const getCurState(){mMutex_data.lock(); Array2D<double> tempState = mCurState.copy(); mMutex_data.unlock(); return tempState;}
	TNT::Array2D<double> const getErrorMemory(){mMutex_data.lock(); Array2D<double> tempInt = mErrInt.copy(); mMutex_data.unlock(); return tempInt;}

	void calcControl();
	void reset();

	void setStartTime(Time t){mStartTime = t;}
	void setQuadLogger(QuadLogger *log){mQuadLogger = log;}
	void setRotViconToPhone(TNT::Array2D<double> const &rot){mRotViconToPhone.inject(rot);}
	void setDesPosAccel(TNT::Array2D<double> const &a);

	void addListener(TranslationControllerListener* listener){mListeners.push_back(listener);}

	// from CommManagerListener
	void onNewCommTransGains(toadlet::egg::Collection<float> const &gains);
	void onNewCommMass(float m){mMass = m;}
	void onNewCommDesState(toadlet::egg::Collection<float> const &data);
	void onNewCommSetDesiredPos();
	void onNewCommMotorOn();

	// for Observer_TranslationalListener
	void onObserver_TranslationalUpdated(TNT::Array2D<double> const &pos, TNT::Array2D<double> const &vel);

	protected:
	bool mRunning, mDone;
	bool mNewMeasAvailable;
	QuadLogger *mQuadLogger;
	TNT::Array2D<double> mCurState, mDesState, mDesPosAccel;
	TNT::Array2D<double> mAccelCmd;
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

	TNT::Array2D<double> calcControlPID(TNT::Array2D<double> const &error, double dt);
};

} // namespace Quadrotor
} // namespace ICSL

#endif
