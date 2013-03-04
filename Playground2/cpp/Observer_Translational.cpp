#include "Observer_Translational.h"
#include "TNT/jama_lu.h"

using namespace toadlet::egg;
using namespace ICSL::Constants;

namespace ICSL{
namespace Quadrotor{
	Observer_Translational::Observer_Translational() :
		mRotViconToPhone(3,3,0.0),
		mAkf(6,6,0.0),
		mAkf_T(6,6,0.0),
		mBkf(6,3,0.0),
		mCkf(6,6,0.0),
		mCkf_T(6,6,0.0),
		mMeasCov(6,6,0.0),
		mDynCov(6,6,0.0),
		mErrCovKF(6,6,0.0),
		mStateKF(6,1,0.0),
		mAttBias(3,1,0.0),
		mAttBiasReset(3,1,0.0),
		mAttBiasAdaptRate(3,0.0),
		mAttitude(3,1,0.0),
		mLastMeas(6,1,0.0),
		mBarometerHeightState(2,1,0.0)
	{
		mRunning = false;
		mDone = true;

		mMass = 0.850;
		mForceGainReset = 0.0040;
		mForceGain = mForceGainReset;
		
		mLastMeasUpdateTime.setTimeMS(0);
		mLastPosReceiveTime.setTimeMS(0);
		mLastBarometerMeasTime.setTimeMS(0);
		
		mAkf.inject(createIdentity(6));
		mCkf.inject(createIdentity(6));
		mMeasCov[0][0] = mMeasCov[1][1] = mMeasCov[2][2] = 0.01*0.01;
		mMeasCov[3][3] = mMeasCov[4][4] = mMeasCov[5][5] = 0.3*0.3;
		mDynCov.inject(0.02*0.02*createIdentity(6));
		mDynCov[5][5] *= 10;
		mErrCovKF.inject(1e-4*createIdentity(6));

		mAkf_T.inject(transpose(mAkf));
		mCkf_T.inject(transpose(mCkf));
		mForceGainAdaptRate = 0;

		for(int i=0; i<4; i++)
			mMotorCmds[i] = 0;

		mZeroHeight = 76;
		
		mDoMeasUpdate = mDoMeasUpdate_xyOnly = mDoMeasUpdate_zOnly = false;
	}

	Observer_Translational::~Observer_Translational()
	{
	}

	void Observer_Translational::shutdown()
	{
		Log::alert("------------------------- Observer_Translational shutdown started  --------------------------------------------------");
		mRunning = false;
		System sys;
		while(!mDone)
			sys.msleep(10);

		Log::alert("------------------------- Observer_Translational shutdown done");
	}

	void Observer_Translational::initialize()
	{
	}

	void Observer_Translational::run()
	{
		mDone = false;
		mRunning = true;
		System sys;
		Time lastUpdateTime;
		Array2D<double> measTemp(6,1);
		Array2D<double> r(3,1);
		Array2D<double> accel(3,1);
		Array2D<double> pos(3,1),vel(3,1);
		double s1, s2, s3, c1, c2, c3;
		double dt;
		Time lastBattTempTime;
		while(mRunning)
		{
			double thrust = 0;
			mMutex_cmds.lock();
			for(int i=0; i<4; i++)
				thrust += mForceGain*mMotorCmds[i];
			mMutex_cmds.unlock();

			if(abs(thrust) >= 1e-6) 
			{
				// third column of orientation matrix, i.e. R*e3
				mMutex_att.lock();
				s1 = sin(mAttitude[2][0]); c1 = cos(mAttitude[2][0]);
				s2 = sin(mAttitude[1][0]-mAttBias[1][0]); c2 = cos(mAttitude[1][0]-mAttBias[1][0]);
				s3 = sin(mAttitude[0][0]-mAttBias[0][0]); c3 = cos(mAttitude[0][0]-mAttBias[0][0]);
				mMutex_att.unlock();
				r[0][0] = s1*s3+c1*c3*s2;
				r[1][0] = c3*s1*s2-c1*s3;
				r[2][0] = c2*c3;

				mMutex_data.lock();
				accel.inject(thrust/mMass*r);
				mMutex_data.unlock();
				accel[2][0] -= GRAVITY;
			}
			else // assume thrust = 0 means the motors are off and we're sitting still on the ground
			{
				for(int i=0; i<accel.dim1(); i++)
					accel[i][0] = 0;
				mMutex_data.lock();
				mAttBias.inject(mAttBiasReset);
				mForceGain = mForceGainReset;
				mMutex_data.unlock();
			}

			dt = lastUpdateTime.getElapsedTimeUS()/1.0e6;
			doTimeUpdateKF(accel, dt);
			lastUpdateTime.setTime();


			if(mDoMeasUpdate)
			{
				mMutex_meas.lock(); measTemp.inject(mLastMeas); mMutex_meas.unlock();
				doMeasUpdateKF(measTemp);
			}
			if(mDoMeasUpdate_xyOnly)
			{
				Array2D<double> xyTemp(4,1,0.0);
				mMutex_meas.lock(); 
				xyTemp[0][0] = mLastMeas[0][0];
				xyTemp[1][0] = mLastMeas[1][0];
				xyTemp[2][0] = mLastMeas[3][0];
				xyTemp[3][0] = mLastMeas[4][0];
				mMutex_meas.unlock();
				doMeasUpdateKF_xyOnly(xyTemp);
			}
			if(mDoMeasUpdate_zOnly)
			{
				Array2D<double> zTemp(2,1,0.0);
				mMutex_meas.lock(); zTemp.inject(mBarometerHeightState); mMutex_meas.unlock();
				doMeasUpdateKF_zOnly(zTemp);
			}

			mMutex_data.lock();
			for(int i=0; i<3; i++)
				pos[i][0] = mStateKF[i][0];
			for(int i=0; i<3; i++)
				vel[i][0] = mStateKF[i+3][0];
			mMutex_data.unlock();

			for(int i=0; i<mListeners.size(); i++)
				mListeners[i]->onObserver_TranslationalUpdated(pos, vel);

			sys.msleep(5); // maintain a (roughly) 200Hz update rate
		}

		mDone = true;
	}

	void Observer_Translational::setMotorCmds(double const cmds[4])
	{
		mMutex_cmds.lock();
		for(int i=0; i<4; i++)
			mMotorCmds[i] = cmds[i];
		mMutex_cmds.unlock();
	}

	void Observer_Translational::doTimeUpdateKF(TNT::Array2D<double> const &actuator, double dt)
	{
		mMutex_data.lock();
		for(int i=0; i<3; i++)
		{
			mAkf[i][i+3] = dt;
			mAkf_T[i+3][i] = dt;
			mBkf[i+3][i] = dt;
		}
		mStateKF.inject(matmult(mAkf,mStateKF)+matmult(mBkf, actuator));
		mErrCovKF.inject(matmult(mAkf, matmult(mErrCovKF, mAkf_T)) + mDynCov);
		mMutex_data.unlock();
	}

	void Observer_Translational::doMeasUpdateKF(TNT::Array2D<double> const &meas)
	{
		mMutex_data.lock();
		Array2D<double> m1_T = transpose(matmult(mErrCovKF, mCkf_T));
		Array2D<double> m2_T = transpose(matmult(mCkf, matmult(mErrCovKF, mCkf_T)) + mMeasCov);

		// I need to solve K = m1*inv(m2) which is the wrong order
		// so solve K^T = inv(m2^T)*m1^T
// The QR solver doesn't seem to give stable results. When I used it the resulting mErrCovKF at the end
// of this function was no longer symmetric
//		JAMA::QR<double> m2QR(m2_T); 
//	 	Array2D<double> gainKF = transpose(m2QR.solve(m1_T));
		JAMA::LU<double> m2LU(m2_T);
		Array2D<double> gainKF = transpose(m2LU.solve(m1_T));

		if(gainKF.dim1() == 0 || gainKF.dim2() == 0)
		{
			Log::alert("SystemControllerFeedbackLin::doMeasUpdateKF() -- Error computing Kalman gain");
			mMutex_data.unlock();
			return;
		}

		// \hat{x} = \hat{x} + K (meas - C \hat{x})
		Array2D<double> err = meas-matmult(mCkf, mStateKF);
		mStateKF += matmult(gainKF, err);

		// S = (I-KC) S
		mErrCovKF.inject(matmult(createIdentity(6)-matmult(gainKF, mCkf), mErrCovKF));

		// this is to ensure that mErrCovKF always stays symmetric even after small rounding errors
		mErrCovKF = 0.5*(mErrCovKF+transpose(mErrCovKF));

		// update bias and force scaling estimates
		if(mLastMeasUpdateTime.getMS() == 0)
			mLastMeasUpdateTime.setTime(); // this will effectively cause dt=0
		double dt = mLastMeasUpdateTime.getElapsedTimeUS()/1.0e6;
		mMutex_att.lock();
		double c = cos(mAttitude[2][0]);
		double s = sin(mAttitude[2][0]);
		mMutex_att.unlock();
		mAttBias[0][0] += mAttBiasAdaptRate[0]*dt*(c*err[1][0]-s*err[0][0]);
		mAttBias[1][0] += mAttBiasAdaptRate[1]*dt*(-s*err[1][0]-c*err[0][0]);
		mForceGain += mForceGainAdaptRate*err[2][0];
		mLastMeasUpdateTime.setTime();

		{
			String str1 = String()+mStartTime.getElapsedTimeMS()+"\t-710\t";
			for(int i=0; i<mAttBias.dim1(); i++)
				str1 = str1+mAttBias[i][0]+"\t";
			mQuadLogger->addLine(str1,LOG_FLAG_STATE);

			String str2 = String()+mStartTime.getElapsedTimeMS()+"\t-711\t";
			str2 = str2+mForceGain+"\t";
			mQuadLogger->addLine(str2,LOG_FLAG_STATE);
		}
		mMutex_data.unlock();

		mDoMeasUpdate = false;
	}

	void Observer_Translational::doMeasUpdateKF_xyOnly(TNT::Array2D<double> const &meas)
	{
		Array2D<double> measCov(4,4);
		measCov[0][0] = mMeasCov[0][0];
		measCov[0][1] = measCov[1][0] = mMeasCov[0][1];
		measCov[0][2] = measCov[2][0] = mMeasCov[0][3];
		measCov[0][3] = measCov[3][0] = mMeasCov[0][4];
		measCov[1][1] = mMeasCov[1][1];
		measCov[1][2] = measCov[2][1] = mMeasCov[1][3];
		measCov[1][3] = measCov[3][1] = mMeasCov[1][4];
		measCov[2][2] = mMeasCov[3][3];
		measCov[2][3] = measCov[3][2] = mMeasCov[3][4];
		measCov[3][3] = mMeasCov[4][4];

		Array2D<double> C(4,6,0.0);
		C[0][0] = C[1][1] = C[2][3] = C[3][4] = 1;
		Array2D<double> C_T = transpose(C);
		Array2D<double> m1_T = transpose(matmult(mErrCovKF, C_T));
		Array2D<double> m2_T = transpose(matmult(C, matmult(mErrCovKF, C_T)) + measCov);

		// I need to solve K = m1*inv(m2) which is the wrong order
		// so solve K^T = inv(m2^T)*m1^T
// The QR solver doesn't seem to give stable results. When I used it the resulting mErrCovKF at the end
// of this function was no longer symmetric
//		JAMA::QR<double> m2QR(m2_T); 
//	 	Array2D<double> gainKF = transpose(m2QR.solve(m1_T));
		JAMA::LU<double> m2LU(m2_T);
		Array2D<double> gainKF = transpose(m2LU.solve(m1_T));

		if(gainKF.dim1() == 0 || gainKF.dim2() == 0)
		{
			Log::alert("SystemControllerFeedbackLin::doMeasUpdateKF() -- Error computing Kalman gain");
			mMutex_data.unlock();
			return;
		}

		// \hat{x} = \hat{x} + K (meas - C \hat{x})
		Array2D<double> err = meas-matmult(C, mStateKF);
		mStateKF += matmult(gainKF, err);

		// S = (I-KC) S
		mErrCovKF.inject(matmult(createIdentity(6)-matmult(gainKF, C), mErrCovKF));

		// this is to ensure that mErrCovKF always stays symmetric even after small rounding errors
		mErrCovKF = 0.5*(mErrCovKF+transpose(mErrCovKF));

		mMutex_data.unlock();
		mDoMeasUpdate_xyOnly = false;
	}

	void Observer_Translational::doMeasUpdateKF_zOnly(TNT::Array2D<double> const &meas)
	{
		mMutex_data.lock();
		Array2D<double> measCov(2,2);
		measCov[0][0] = mMeasCov[2][2];
		measCov[0][1] = measCov[1][0] = mMeasCov[2][5];
		measCov[1][1] = mMeasCov[5][5];
		Array2D<double> C(2,6,0.0);
		C[0][2] = C[1][5] = 1;
		Array2D<double> C_T = transpose(C);
		Array2D<double> m1_T = transpose(matmult(mErrCovKF, C_T));
		Array2D<double> m2_T = transpose(matmult(C, matmult(mErrCovKF, C_T)) + measCov);

		// I need to solve K = m1*inv(m2) which is the wrong order
		// so solve K^T = inv(m2^T)*m1^T
// The QR solver doesn't seem to give stable results. When I used it the resulting mErrCovKF at the end
// of this function was no longer symmetric
//		JAMA::QR<double> m2QR(m2_T); 
//	 	Array2D<double> gainKF = transpose(m2QR.solve(m1_T));
		JAMA::LU<double> m2LU(m2_T);
		Array2D<double> gainKF = transpose(m2LU.solve(m1_T));

		if(gainKF.dim1() == 0 || gainKF.dim2() == 0)
		{
			Log::alert("SystemControllerFeedbackLin::doMeasUpdateKF() -- Error computing Kalman gain");
			mMutex_data.unlock();
			return;
		}

		// \hat{x} = \hat{x} + K (meas - C \hat{x})
		Array2D<double> err = meas-matmult(C, mStateKF);
		mStateKF += matmult(gainKF, err);

		// S = (I-KC) S
		mErrCovKF.inject(matmult(createIdentity(6)-matmult(gainKF, C), mErrCovKF));

		// this is to ensure that mErrCovKF always stays symmetric even after small rounding errors
		mErrCovKF = 0.5*(mErrCovKF+transpose(mErrCovKF));

		mMutex_data.unlock();
		mDoMeasUpdate_zOnly = false;
	}

	void Observer_Translational::onObserver_AngularUpdated(Array2D<double> const &att, Array2D<double> const &angularVel)
	{
		mMutex_att.lock();
		mAttitude.inject(att);
		mMutex_att.unlock();
	}

	void Observer_Translational::onNewCommStateVicon(Collection<float> const &data)
	{
		Array2D<double> pos(3,1);
		for(int i=0; i<3; i++)
			pos[i][0] = data[i+6];
		pos = matmult(mRotViconToPhone, pos);
		mMutex_meas.lock();
		Array2D<double> vel(3,1,0.0);
		if(mLastPosReceiveTime.getMS() != 0)
		{
			// This velocity ``measurement'' is very noisy, but it helps to correct some
			// of the bias error that occurs when the attitude estimate is wrong
			double dt = mLastPosReceiveTime.getElapsedTimeUS()/1.0e6;
			if(dt > 1.0e-3) // reduce the effect of noise
				vel.inject(1.0/dt*(pos-submat(mLastMeas,0,2,0,0)));
			else
			{
				for(int i=0; i<3; i++)
					vel[i][0] = mLastMeas[i+3][0];
			}
		}
		for(int i=0; i<3; i++)
			mLastMeas[i][0] = pos[i][0];
		for(int i=3; i<6; i++)
			mLastMeas[i][0] = vel[i-3][0];
		mMutex_meas.unlock();
		mLastPosReceiveTime.setTime();

		// use this to signal the run() thread to avoid tying up the 
		// CommManager thread calling this function
//		mDoMeasUpdate = true;
		mDoMeasUpdate_xyOnly = true;
	}

	void Observer_Translational::onNewCommMass(float m)
	{
		mMutex_data.lock();
		mMass = m;
		mMutex_data.unlock();
	}

	void Observer_Translational::onNewCommForceGain(float k)
	{
		mMutex_data.lock();
		mForceGainReset = k;
		mForceGain = k;
		Log::alert(String()+"Force gain updated: \t"+mForceGain);
		mMutex_data.unlock();
	}

	void Observer_Translational::onNewCommAttBias(float roll, float pitch, float yaw)
	{
		mMutex_data.lock();
		mAttBiasReset[0][0] = roll;
		mAttBiasReset[1][0] = pitch;
		mAttBiasReset[2][0] = yaw;
		mAttBias.inject(mAttBiasReset);
		printArray("att bias: \t",transpose(mAttBias));
		mMutex_data.unlock();
	}

	void Observer_Translational::onNewCommAttBiasAdaptRate(Collection<float> const &rate)
	{
		mMutex_data.lock();
		for(int i=0; i<3; i++)
			mAttBiasAdaptRate[i] = rate[i];
		mMutex_data.unlock();
		{
			String s = "Att bias adapt rate updated: ";
			for(int i=0; i<rate.size(); i++)
				s = s+rate[i]+"\t";
			Log::alert(s);
		}
	}

	void Observer_Translational::onNewCommForceGainAdaptRate(float rate)
	{
		mMutex_data.lock();
		mForceGainAdaptRate = rate;
		Log::alert(String()+"force gain adapt rate: \t"+mForceGainAdaptRate);
		mMutex_data.unlock();
	}

	void Observer_Translational::onNewCommKalmanMeasVar(Collection<float> const &var)
	{
		mMutex_data.lock();
		for(int i=0; i<6; i++)
			mMeasCov[i][i] = var[i];
		String s = "Meas var update -- diag(mMeasCov): \t";
		for(int i=0; i<mMeasCov.dim1(); i++)
			s = s+mMeasCov[i][i]+"\t";
		mMutex_data.unlock();
		Log::alert(s);
	}

	void Observer_Translational::onNewCommKalmanDynVar(Collection<float> const &var)
	{
		mMutex_data.lock();
		for(int i=0; i<6; i++)
			mDynCov[i][i] = var[i];
		String s = "Dyn var update -- diag(mDynCov): \t";
		for(int i=0; i<mDynCov.dim1(); i++)
			s = s+mDynCov[i][i]+"\t";
		mMutex_data.unlock();
		Log::alert(s);
	}

	void Observer_Translational::onNewCommBarometerZeroHeight(float h)
	{
		Log::alert(String()+"Barometer zero height set to " + h + " m");
		mMutex_meas.lock();
		mZeroHeight = h;
		mMutex_meas.unlock();
		return;
	}

	void Observer_Translational::onAttitudeThrustControllerCmdsSent(double const cmds[4])
	{
		mMutex_cmds.lock();
		for(int i=0; i<4; i++)
			mMotorCmds[i] = cmds[i];
		mMutex_cmds.unlock();
	}

	void Observer_Translational::onNewSensorUpdate(SensorData const *data)
	{
		switch(data->type)
		{
			case SENSOR_DATA_TYPE_PRESSURE:
			{
				// equation taken from wikipedia
				double pressure = data->data;
				double Rstar = 8.31432; // N·m /(mol·K)
				double Tb = 288.15; // K
				double g0 = 9.80665; // m/s^2
				double M = 0.0289644; // kg/mol
				double Pb = 1013.25; // milliBar

				mMutex_phoneTempData.lock();
				if(mPhoneTempData.tmuTemp == 0) // no updates yet
					return;
				float tmuTemp = mPhoneTempData.tmuTemp;
				mMutex_phoneTempData.unlock();
				double k = (999.5-1000.0)/(45.0-37.0); // taken from experimental data
				double pressComp = pressure-k*(tmuTemp-37.0);

				mMutex_meas.lock();
				double h = -Rstar*Tb/g0/M*log(pressure/Pb);
				double hComp = -Rstar*Tb/g0/M*log(pressComp/Pb);

				if(mLastBarometerMeasTime.getMS() > 0)
				{
					double dt = mLastBarometerMeasTime.getElapsedTimeUS()/1.0e6;
					if(dt > 1.0e-3)
						mBarometerHeightState[1][0] = 1.0/dt*(h-mZeroHeight-mBarometerHeightState[0][0]);
					else
						mBarometerHeightState[1][0] = mBarometerHeightState[1][0];
				}
				else
					mBarometerHeightState[1][0] = 0;
				mBarometerHeightState[0][0]= h-mZeroHeight;
				mMutex_meas.unlock();

				mDoMeasUpdate_zOnly = true;
				if(mQuadLogger != NULL)
				{
					String s = String() + mStartTime.getElapsedTimeMS() + "\t1234\t" + h + "\t" + hComp;
					mQuadLogger->addLine(s,LOG_FLAG_PC_UPDATES);
				}
			}
			break;
			case SENSOR_DATA_TYPE_PHONE_TEMP:
			{
				mMutex_phoneTempData.lock();
				((SensorDataPhoneTemp*)data)->copyTo(mPhoneTempData);
				mMutex_phoneTempData.unlock();
			}
			break;
		}
	}
} // namespace Quadrotor
} // namespace ICSL