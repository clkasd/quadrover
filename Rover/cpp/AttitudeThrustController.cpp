#include "AttitudeThrustController.h"
#include "AttitudeThrustControllerListener.h"

using namespace toadlet;

namespace ICSL {
namespace Quadrotor {
	AttitudeThrustController::AttitudeThrustController() :
		mCurAtt(3,1,0.0),
		mCurAngularVel(3,1,0.0),
		mDesAtt(3,1,0.0),
		mDesRotMat(3,3,0.0),
		mDesRotMat_T(3,3,0.0),
		mGainRate(3,1,0.0)
	{
		mRunning = false;
		mDone = true;
	
		mDesRotMat.inject(createIdentity(3));
		mDesRotMat_T.inject(createIdentity(3));
	
		mDoControl = false;
		mPcIsConnected = false;
	
		mLastControlTime.setTimeMS(0);

		mGainAngle = 0;

		mForceScaling = 3e-3;
		mTorqueScaling = 1e-3;

		mThrust = 0;
		mMass = 0;
		
		mMotorDist = 0.160;

		mMotorTrim[0] = mMotorTrim[1] = mMotorTrim[2] = mMotorTrim[3] = 0;
	}
	
	AttitudeThrustController::~AttitudeThrustController()
	{
	}
	
	void AttitudeThrustController::shutdown()
	{
		Log::alert("------------------------- AttitudeThrustController shutdown started  --------------------------------------------------");
		mMutex_motorInterface.lock();
		mMotorInterface.enableMotors(false);
		mMutex_motorInterface.unlock();
		mRunning = false;
		System sys;
		while(!mDone)
			sys.msleep(10);
	
		mMotorInterface.shutdown();
	
		Log::alert("------------------------- AttitudeThrustController shutdown done");
	}
	
	void AttitudeThrustController::initialize()
	{
		mMotorInterface.initialize();
		mMotorInterface.enableMotors(false);
		mMotorInterface.start();
	}
	
	void AttitudeThrustController::run()
	{
		mDone = false;
		mRunning = true;
		System sys;
		while(mRunning)
		{
			if(mDoControl)
				calcControl();
	
			sys.msleep(1);
		}
	
		mDone = true;
	}
	
	void AttitudeThrustController::calcControl()
	{
		mMutex_data.lock();
		Array2D<double> curRotMat = createRotMat_ZYX(mCurAtt[2][0], mCurAtt[1][0], mCurAtt[0][0]);
		Array2D<double> rotMatErr = matmult(mDesRotMat_T, curRotMat);
		Array2D<double> curStateAngular = stackVertical(mCurAtt,mCurAngularVel);
		mMutex_data.unlock();
	
		Array2D<double> rotMatErr_AS = rotMatErr-transpose(rotMatErr);
		Array2D<double> rotErr(3,1);
		rotErr[0][0] = rotMatErr_AS[2][1];
		rotErr[1][0] = rotMatErr_AS[0][2];
		rotErr[2][0] = rotMatErr_AS[1][0];
	
		mMutex_data.lock();
// a bit of a hack ... I need to check the proof again
Array2D<double> gainAngle(3,1,mGainAngle);
gainAngle[2][0] = mGainRate[2][0];
		// do I have an error in my controller derivation and previous implementation?
		// The orignal form isn't giving the right torque
//		Array2D<double> torque = -mGainAngle*rotErr-mGainRate*mCurAngularVel;
		Array2D<double> torque = gainAngle*rotErr-mGainRate*mCurAngularVel;
		double cmdRoll = torque[0][0]/mForceScaling/mMotorDist/4.0;
		double cmdPitch = torque[1][0]/mForceScaling/mMotorDist/4.0;
		double cmdYaw = torque[2][0]/mTorqueScaling/4.0;
		double cmdThrust = mThrust/mForceScaling/4.0;
		mMutex_data.unlock();

		double cmds[4];
		cmds[0] = cmdThrust-cmdRoll-cmdPitch+cmdYaw;
		cmds[1] = cmdThrust-cmdRoll+cmdPitch-cmdYaw;
		cmds[2] = cmdThrust+cmdRoll+cmdPitch+cmdYaw;
		cmds[3] = cmdThrust+cmdRoll-cmdPitch-cmdYaw;
	
		Collection<uint16> motorCmds(4);
		if(mPcIsConnected)
		{
			mMutex_data.lock();
			for(int i=0; i<4; i++)
				motorCmds[i] = (uint16)(cmds[i]+0.5+1000)+mMotorTrim[i];
			mMutex_data.unlock();
		}
		else
			for(int i=0; i<4;i++)
				motorCmds[i] = 1000;
		mMutex_motorInterface.lock();
		mMotorInterface.sendCommand(motorCmds);
		mLastMotorCmds = motorCmds; // should copy all the data
		mMutex_motorInterface.unlock();
	
		mDoControl = false;

		for(int i=0; i<mListeners.size(); i++)
			mListeners[i]->onAttitudeThrustControllerCmdsSent(cmds);
	
		// Logging
		if(mQuadLogger != NULL)
		{
			String s1=String() + " " + mStartTime.getElapsedTimeMS() + "\t" + "-1000" +"\t";
			for(int i=0; i<4; i++)
				s1 = s1+cmds[i] + "\t";

			mMutex_data.lock();
			String s2=String() + " " + mStartTime.getElapsedTimeMS() + "\t" + "-1001" +"\t";
			for(int i=0; i<mDesAtt.dim1(); i++)
				s2 = s2 + mDesAtt[i][0] + "\t";
			for(int i=0; i<3; i++)
				s2 = s2+"0\t";

			String s3=String() + " " + mStartTime.getElapsedTimeMS() + "\t" + "-1002" +"\t";
			for(int i=0; i<curStateAngular.dim1(); i++)
				s3 = s3+curStateAngular[i][0] + "\t";
			mMutex_data.unlock();

			mQuadLogger->addLine(s1,MOTORS);
			mQuadLogger->addLine(s2,STATE_DES);
			mQuadLogger->addLine(s3,STATE);
		}
	}
	
	void AttitudeThrustController::onObserver_AngularUpdated(Array2D<double> const &att, Array2D<double> const &angularVel)
	{
		mMutex_data.lock();
		mCurAtt.inject(att);
		mCurAngularVel.inject(angularVel);
		mMutex_data.unlock();
	
		mDoControl = true;
	}
	
	void AttitudeThrustController::onTranslationControllerAccelCmdUpdated(Array2D<double> const &accelCmd)
	{
		double acc = norm2(accelCmd);
		if(abs(acc) > 1e-3)
		{
			mMutex_data.lock();

			// des roll and pitch will be based on current yaw to make sure
			// the force vector points in the correct direction even when there are
			// static yaw errors (can't really do much about static roll and pitch 
			// errors)
			double c = cos(mCurAtt[2][0]); double s = sin(mCurAtt[2][0]);
			double x =  accelCmd[0][0]*c+accelCmd[1][0]*s;
			double y = -accelCmd[0][0]*s+accelCmd[1][0]*c;
			double roll  = -asin(y/acc);
			double pitch =  asin(x/cos(roll)/acc);
			double yaw = 0;

			mDesAtt[0][0] = roll;
			mDesAtt[1][0] = pitch;
			mDesAtt[2][0] = yaw;
			mDesRotMat.inject(createRotMat_ZYX(yaw,pitch,roll));
			mDesRotMat_T.inject(transpose(mDesRotMat));
			mThrust = mMass*accelCmd[2][0]/cos(roll)/cos(pitch);
			mMutex_data.unlock();
		}
		else
		{
			Log::alert("AttitudeThrustController -- Acceleration command was too small");
			mMutex_data.lock();
			mDesAtt[0][0] = mDesAtt[1][0] = mDesAtt[2][0] = 0;
			mDesRotMat.inject(createIdentity(3));
			mDesRotMat_T.inject(createIdentity(3));
			mMutex_data.unlock();
		}
	}
	
	void AttitudeThrustController::onNewCommMotorOff()
	{
		mPcIsConnected = true;
		Log::alert(String("Turning motors off"));
		mMutex_motorInterface.lock();
		mMotorInterface.enableMotors(false);
		mMutex_motorInterface.unlock();
	}
	
	void AttitudeThrustController::onNewCommMotorOn()
	{
		mPcIsConnected = true;
		mMutex_motorInterface.lock();
		mMotorInterface.enableMotors(true);
		mMutex_motorInterface.unlock();
	}
	
	void AttitudeThrustController::onCommConnectionLost()
	{
		mPcIsConnected = false;
		mMutex_motorInterface.lock();
		mMotorInterface.enableMotors(false);
		mMutex_motorInterface.unlock();
	}
	
	void AttitudeThrustController::onNewCommForceScaling(float k)
	{
		mPcIsConnected = true;
		mMutex_data.lock();
		mForceScaling = k;
		mMutex_data.unlock();
		Log::alert(String()+"Force scaling set to "+k);
	}

	void AttitudeThrustController::onNewCommTorqueScaling(float k)
	{
		mPcIsConnected = true;
		mMutex_data.lock();
		mTorqueScaling = k;
		mMutex_data.unlock();
		Log::alert(String()+"Torque scaling set to "+k);
	}

	void AttitudeThrustController::onNewCommMotorTrim(int const trim[4])
	{
		mPcIsConnected = true;
		mMutex_data.lock();
		for(int i=0; i<4; i++)
			mMotorTrim[i] = trim[i];;
		mMutex_data.unlock();
	}

	void AttitudeThrustController::onNewCommMass(float m)
	{
		mPcIsConnected = true;
		mMutex_data.lock();
		mMass = m;
		mMutex_data.unlock();
	}

	void AttitudeThrustController::onNewCommAttitudeGains(Collection<float> const &gains)
	{
		mPcIsConnected = true;
		mMutex_data.lock();
		for(int i=0; i<3; i++)
			mGainRate[i][0] = gains[i];
		mGainAngle = gains[3];
		mMutex_data.unlock();
	}

	void AttitudeThrustController::enableMotors(bool enabled)
	{
		mMutex_motorInterface.lock();
		mMotorInterface.enableMotors(enabled);
		mMutex_motorInterface.unlock();
	}

	bool AttitudeThrustController::isMotorInterfaceConnected()
	{
		bool temp;
		mMutex_motorInterface.lock();
		temp = mMotorInterface.isConnected();
		mMutex_motorInterface.unlock();
		return temp;
	}

} // namespace Quadrotor
} // namespace ICSL