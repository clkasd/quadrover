#include "Observer_Translational.h"
#include "TNT/jama_lu.h"
#include "TNT/jama_qr.h"

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
		mLastViconPos(3,1,0.0),
		mLastCameraPos(3,1,0.0),
		mBarometerHeightState(2,1,0.0),
		mOpticFlowVel(3,1,0.0),
		mLastViconVel(3,1,0.0),
		mLastCameraVel(3,1,0.0)
	{
		mRunning = false;
		mDone = true;

		mMass = 0.850;
		mForceGainReset = 0.0040;
		mForceGain = mForceGainReset;
		
		mLastForceGainUpdateTime.setTimeMS(0);
		mLastAttBiasUpdateTime.setTimeMS(0);
		mLastPosReceiveTime.setTimeMS(0);
		mLastBarometerMeasTime.setTimeMS(0);
		
		mAkf.inject(createIdentity(6));
		mCkf.inject(createIdentity(6));
		mMeasCov[0][0] = mMeasCov[1][1] = mMeasCov[2][2] = 0.01*0.01;
		mMeasCov[3][3] = mMeasCov[4][4] = mMeasCov[5][5] = 0.3*0.3;
		mMeasCov[2][2] = 0.05*0.05;
		mMeasCov[5][5] = 0.5*0.5;
		mDynCov.inject(0.02*0.02*createIdentity(6));
		mDynCov[5][5] *= 10;
		mErrCovKF.inject(1e-4*createIdentity(6));

		mAkf_T.inject(transpose(mAkf));
		mCkf_T.inject(transpose(mCkf));
		mForceGainAdaptRate = 0;

		for(int i=0; i<4; i++)
			mMotorCmds[i] = 0;

		mZeroHeight = 76;
		
		mDoMeasUpdate = false;
		mNewViconPosAvailable = mNewCameraPosAvailable = false;

		mNewImageResultsReady = false;

		mPhoneTempData = NULL;
		mImageMatchData = NULL;

		mRotCamToPhone = matmult(createRotMat(2,-0.5*(double)PI),
								 createRotMat(0,(double)PI));
		mRotPhoneToCam = transpose(mRotCamToPhone);

		mFlowCalcDone = true;
		mNewOpticFlowReady = false;

		mMotorOn = false;

		mScheduler = SCHED_NORMAL;
		mThreadPriority = sched_get_priority_min(SCHED_NORMAL);

		mUseCameraPos = false;
		mUseViconPos = true;
		mHaveFirstCameraPos = false;

		mUseIbvs = false;
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

		mPhoneTempData = NULL;
		mImageMatchData = NULL;
		Log::alert("------------------------- Observer_Translational shutdown done");
	}

	void Observer_Translational::initialize()
	{
	}

	void Observer_Translational::run()
	{
		mDone = false;
		mRunning = true;

		class : public Thread{
					public:
					void run(){imageMatchData->lock(); parent->calcOpticalFlow(imageMatchData); imageMatchData->unlock();}
					shared_ptr<ImageMatchData> imageMatchData;
					Observer_Translational *parent;
				} flowCalcThread;
		flowCalcThread.parent = this;

		Time lastUpdateTime;
		Array2D<double> measTemp(6,1);
		Array2D<double> r(3,1);
		Array2D<double> accel(3,1);
		Array2D<double> pos(3,1),vel(3,1);
		double s1, s2, s3, c1, c2, c3;
		double dt;
		Time lastBattTempTime;
		Array2D<double> flowVel(3,1,0.0);
		Array2D<double> errCov(12,1,0.0);

		sched_param sp;
		sp.sched_priority = mThreadPriority;
		sched_setscheduler(0, mScheduler, &sp);

		Time loopTime;
		while(mRunning)
		{
			loopTime.setTime();
			double thrust = 0;
			mMutex_cmds.lock();
			for(int i=0; i<4; i++)
				thrust += mForceGain*mMotorCmds[i];
			mMutex_cmds.unlock();

			mMutex_data.lock();
			if(mMotorOn)
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

				accel.inject(thrust/mMass*r);
				accel[2][0] -= GRAVITY;
			}
			else 
			{
				for(int i=0; i<accel.dim1(); i++)
					accel[i][0] = 0;
				mAttBias.inject(mAttBiasReset);
				mForceGain = mForceGainReset;
			}
			mAccelBuffer.push_back(accel.copy());
			mAccelTimeBuffer.push_back(Time());
			mMutex_data.unlock();

			dt = lastUpdateTime.getElapsedTimeUS()/1.0e6;
			doTimeUpdateKF(accel, dt);
			lastUpdateTime.setTime();

			mMutex_meas.lock();
			if(mHaveFirstCameraPos && mLastCameraPosTime.getElapsedTimeMS() > 500)
				mHaveFirstCameraPos = false;
			mMutex_meas.unlock();

//			if(mDoMeasUpdate)
//			{
//				mMutex_meas.lock(); measTemp.inject(mLastMeas); mMutex_meas.unlock();
//				doMeasUpdateKF(measTemp);
//			}
			if(mNewOpticFlowReady)
			{
				mNewOpticFlowReady = false;
				mMutex_meas.lock();
				Array2D<double> vel = mOpticFlowVel.copy();
				Time imgTime = mOpticFlowVelTime;
				mMutex_meas.unlock();

				// unwind the state back to the time when the picture was taken
				mMutex_data.lock();
				list<Time>::const_iterator stateTimeIter = mStateTimeBuffer.begin();
				list<Array2D<double> >::const_iterator stateIter = mStateBuffer.begin();
				while(stateTimeIter != mStateTimeBuffer.end() && (*stateTimeIter).getUS() < imgTime.getUS())
				{
					stateTimeIter++;
					stateIter++;
				}
				if(stateIter != mStateBuffer.end())
					mStateKF.inject(*stateIter);

				list<Time>::const_iterator errCovTimeIter = mErrCovKFTimeBuffer.begin();
				list<Array2D<double> >::const_iterator errCovIter = mErrCovKFBuffer.begin();
				while(errCovTimeIter != mErrCovKFTimeBuffer.end() && (*errCovTimeIter).getUS() < imgTime.getUS())
				{
					errCovTimeIter++;
					errCovIter++;
				}
				if(errCovTimeIter != mErrCovKFTimeBuffer.end())
					mErrCovKF.inject(*errCovIter);

				Array2D<double> velMeasCov(3,3);
				velMeasCov[0][0] = mMeasCov[3][3];
				velMeasCov[0][1] = velMeasCov[1][0] = mMeasCov[3][4];
				velMeasCov[0][2] = velMeasCov[2][0] = mMeasCov[3][5];
				velMeasCov[1][1] = mMeasCov[4][4];
				velMeasCov[1][2] = velMeasCov[2][1] = mMeasCov[4][5];
				mMutex_data.unlock();

				doMeasUpdateKF_velOnly(vel, velMeasCov);

				// now apply forward back to present time
				mMutex_data.lock();
				while(mPosMeasTimeBuffer.front().getUS() < imgTime.getUS())
				{
					mPosMeasTimeBuffer.pop_front();
					mPosMeasBuffer.pop_front();
				}
				while(mAccelTimeBuffer.front().getUS() < imgTime.getUS())
				{
					mAccelTimeBuffer.pop_front();
					mAccelBuffer.pop_front();
				}
				
				mStateBuffer.clear();
				mStateTimeBuffer.clear();
				mErrCovKFBuffer.clear();
				mErrCovKFTimeBuffer.clear();
				list<Time>::const_iterator accelTimeIter = mAccelTimeBuffer.begin();
				list<Time>::const_iterator accelTimeIterNext = mAccelTimeBuffer.begin();
				accelTimeIterNext++;
				list<Array2D<double> >::const_iterator accelIter = mAccelBuffer.begin();
				list<Time>::const_iterator posMeasTimeIter = mPosMeasTimeBuffer.begin();
				list<Array2D<double> >::const_iterator posMeasIter = mPosMeasBuffer.begin();
				Array2D<double> accel(3,1), pos(3,1);
				double dt;
				Array2D<double> posMeasCov = submat(mMeasCov,0,2,0,2);
				while(accelTimeIterNext != mAccelTimeBuffer.end())
				{
					accel.inject(*accelIter);
					dt = Time::calcDiffUS(*accelTimeIter, (*accelTimeIterNext))/1.0e6;
					mMutex_data.unlock();

					doTimeUpdateKF(accel, dt);

					mMutex_data.lock();

					if( posMeasTimeIter != mPosMeasTimeBuffer.end() && (*posMeasTimeIter).getUS() > (*accelTimeIter).getUS())
					{
						pos.inject(*posMeasIter);
						posMeasTimeIter++;
						posMeasIter++;

						mMutex_data.unlock();
						doMeasUpdateKF_posOnly(pos, posMeasCov);
						mMutex_data.lock();
					}


					mStateBuffer.push_back(mStateKF.copy());
					mStateTimeBuffer.push_back(*accelTimeIter);
					mErrCovKFBuffer.push_back(mErrCovKF.copy());
					mErrCovKFTimeBuffer.push_back(*accelTimeIter);

					accelTimeIter++;
					accelTimeIterNext++;
					accelIter++;
				}

				mMutex_data.unlock();
			}

			if(mUseIbvs && mHaveFirstCameraPos)
			{
				if(mUseViconPos)
				{ // first time in here after switch
					mPosMeasTimeBuffer.clear();
					mPosMeasBuffer.clear();
				}
				mUseViconPos = false;
				mUseCameraPos = true;
			}
			else
			{
				if(mUseCameraPos)
				{ // first time in here after switch
					mPosMeasTimeBuffer.clear();
					mPosMeasBuffer.clear();
				}
				mUseViconPos = true;
				mUseCameraPos = false;
			}

			if(mNewViconPosAvailable && mUseViconPos)
			{
				mNewViconPosAvailable = false;
				mMutex_meas.lock();
				Array2D<double> pos = mLastViconPos.copy();
				Array2D<double> vel = mLastViconVel.copy();
				mMutex_meas.unlock();

				mMutex_data.lock();
				Array2D<double> posMeasCov = submat(mMeasCov,0,2,0,2);
				mMutex_data.unlock();
				doMeasUpdateKF_posOnly(pos, posMeasCov);
mMutex_data.lock();
Array2D<double> velMeasCov(3,3);
velMeasCov[0][0] = mMeasCov[3][3];
velMeasCov[0][1] = velMeasCov[1][0] = mMeasCov[3][4];
velMeasCov[0][2] = velMeasCov[2][0] = mMeasCov[3][5];
velMeasCov[1][1] = mMeasCov[4][4];
velMeasCov[1][2] = velMeasCov[2][1] = mMeasCov[4][5];
velMeasCov = 50.0*velMeasCov;
mMutex_data.unlock();
				doMeasUpdateKF_velOnly(vel, velMeasCov);

			}
			if(mNewCameraPosAvailable && mUseCameraPos)
			{
				mNewCameraPosAvailable = false;
				mMutex_meas.lock();
				Array2D<double> pos = mLastCameraPos.copy();
				Array2D<double> vel = mLastCameraVel.copy();
				mMutex_meas.unlock();

				mMutex_data.lock();
				Array2D<double> posMeasCov = submat(mMeasCov,0,2,0,2);
				mMutex_data.unlock();
				doMeasUpdateKF_posOnly(pos, posMeasCov);
mMutex_data.lock();
Array2D<double> velMeasCov(3,3);
velMeasCov[0][0] = mMeasCov[3][3];
velMeasCov[0][1] = velMeasCov[1][0] = mMeasCov[3][4];
velMeasCov[0][2] = velMeasCov[2][0] = mMeasCov[3][5];
velMeasCov[1][1] = mMeasCov[4][4];
velMeasCov[1][2] = velMeasCov[2][1] = mMeasCov[4][5];
velMeasCov = 50.0*velMeasCov;
mMutex_data.unlock();
				doMeasUpdateKF_velOnly(vel, velMeasCov);
			}

			if(mNewImageResultsReady && mFlowCalcDone
//					&& mMotorOn // sometimes the blurry images when sitting close to the ground creates artificial matches
					)
			{
				// if we're here then the previous thread should already be finished
				flowCalcThread.imageMatchData = mImageMatchData;
				mFlowCalcDone = false;
				flowCalcThread.start();
				mNewImageResultsReady = false;
			}

			mMutex_data.lock();
			mStateBuffer.push_back(mStateKF.copy());
			mStateTimeBuffer.push_back(Time());
			mErrCovKFBuffer.push_back(mErrCovKF.copy());
			mErrCovKFTimeBuffer.push_back(Time());

			while(mAccelBuffer.size() > 500)
			{
				mAccelBuffer.pop_front();
				mAccelTimeBuffer.pop_front();
			}
			while(mStateBuffer.size() > 1000)
			{
				mStateBuffer.pop_front();
				mStateTimeBuffer.pop_front();
			}
			while(mErrCovKFBuffer.size() > 1000)
			{
				mErrCovKFBuffer.pop_front();
				mErrCovKFTimeBuffer.pop_front();
			}
			while(mPosMeasBuffer.size() > 1000)
			{
				mPosMeasBuffer.pop_front();
				mPosMeasTimeBuffer.pop_front();
			}

			for(int i=0; i<3; i++)
				pos[i][0] = mStateKF[i][0];
			for(int i=0; i<3; i++)
				vel[i][0] = mStateKF[i+3][0];
			for(int i=0; i<3; i++)
			{
				errCov[i][0] = mErrCovKF[i][i];
				errCov[i+3][0] = mErrCovKF[i][i+3];
				errCov[i+6][0] = mErrCovKF[i+3][i+3];
			}
			mMutex_data.unlock();

			{
				String str = String()+mStartTime.getElapsedTimeMS() + "\t"+LOG_ID_KALMAN_ERR_COV+"\t";
				for(int i=0; i<errCov.dim1(); i++)
					str = str+errCov[i][0]+"\t";
				mQuadLogger->addLine(str,LOG_FLAG_STATE);
			}

			for(int i=0; i<mListeners.size(); i++)
				mListeners[i]->onObserver_TranslationalUpdated(pos, vel);

			uint64 t = loopTime.getElapsedTimeUS();
			if(t < 5e3)
				System::usleep(5e3-t); // maintain a (roughly) 200Hz update rate
		}

		mDone = true;
	}

	// See eqn 98 in the Feb 25, 2013 notes
	void  Observer_Translational::calcOpticalFlow(shared_ptr<ImageMatchData> const matchData)
	{
		if(matchData->featurePoints[0].size() < 5)
		{
			String str = String()+mStartTime.getElapsedTimeMS() + "\t"+LOG_ID_OPTIC_FLOW_INSUFFICIENT_POINTS+"\t";
			mQuadLogger->addLine(str,LOG_FLAG_CAM_RESULTS);
			mFlowCalcDone = true;
			return;
		}

		double dt = matchData->dt;
		if(dt < 1e-3)
			return;
		mMutex_data.lock();
		Array2D<double> mu_v = submat(mStateKF,3,5,0,0);
		Array2D<double> Sn = 300*300*createIdentity(2);
		Array2D<double> SnInv(2,2,0.0);
		SnInv[0][0] = 1.0/Sn[0][0]; SnInv[1][1] = 1.0/Sn[1][1];

		Array2D<double> Sv = submat(mErrCovKF,3,5,3,5);
		JAMA::LU<double> SvLU(Sv);
		Array2D<double> SvInv = SvLU.solve(createIdentity(3));
		double z = mStateKF[2][0];
		z -= 0.060; // offset between markers and camera

		// Rotate prior velocity information to camera coords
		mu_v = matmult(mRotPhoneToCam, mu_v);
		SvInv = matmult(mRotPhoneToCam, matmult(SvInv, mRotCamToPhone));

		mOpticFlowVelTime = matchData->imgData1->timestamp;
		mMutex_data.unlock();
//Log::alert("++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++");

		Array2D<double> A(1,3,0.0);
		Array2D<double> B(3,3,0.0);
		Array2D<double> R1 = createRotMat_ZYX(matchData->imgData0->att[2][0], matchData->imgData0->att[1][0], matchData->imgData0->att[0][0]);
		Array2D<double> R2 = createRotMat_ZYX(matchData->imgData1->att[2][0], matchData->imgData1->att[1][0], matchData->imgData1->att[0][0]);
		Array2D<double> R1_T = transpose(R1);
		Array2D<double> R2_T = transpose(R2);
		Array2D<double> R = matmult(R1, transpose(R2));
		Array2D<double> q1a(3,1), q2a(3,1);
		Array2D<double> q1(2,1), q2(2,1);
		Array2D<double> Lv(2,3), Lv1(2,3), Lv2(2,3);
		Array2D<double> Lw(2,3), Lw1(2,3), Lw2(2,3);
		Array2D<double> angularVel(3,1,0.0);
		double f1= matchData->imgData0->focalLength;
		double f2 = matchData->imgData1->focalLength;
		double cx = matchData->imgData0->img->cols/2;
		double cy = matchData->imgData0->img->rows/2;
		for(int i=0; i<matchData->featurePoints[0].size(); i++)
		{
			q1[0][0] = matchData->featurePoints[0][i].x-cx;
			q1[1][0] = matchData->featurePoints[0][i].y-cy;
			q2[0][0] = matchData->featurePoints[1][i].x-cx;
			q2[1][0] = matchData->featurePoints[1][i].y-cy;

			// Unrotate
			q1a[0][0] = q1[0][0];
			q1a[1][0] = q1[1][0];
			q1a[2][0] = f1;
			q2a[0][0] = q2[0][0];
			q2a[1][0] = q2[1][0];
			q2a[2][0] = f2;
//			// change q2 points to q1 attitude
//			q2a = matmult(R, q2a); 
			q1a = matmult(R1_T, q1a);
			q2a = matmult(R2_T, q2a);

			// project back onto the focal plane
			q1a = f1/q1a[2][0]*q1a;
			q2a = f2/q2a[2][0]*q2a;

			// back to 2d points
			q1[0][0] = q1a[0][0]; q1[1][0] = q1a[1][0];
			q2[0][0] = q2a[0][0]; q2[1][0] = q2a[1][0];

			// Velocity jacobian
			Lv1[0][0] = -f1; Lv1[0][1] = 0; Lv1[0][2] = q1[0][0];
			Lv1[1][0] = 0; Lv1[1][1] = -f1; Lv1[1][2] = q1[1][0];

			Lv2[0][0] = -f2; Lv2[0][1] = 0; Lv2[0][2] = q2[0][0];
			Lv2[1][0] = 0; Lv2[1][1] = -f2; Lv2[1][2] = q2[1][0];

			Lv = Lv1.copy();

//			Lw1[0][0] = q1[0][0]*q1[1][0]; Lw1[0][1] = -(1+pow(q1[0][0],2)); Lw1[0][2] = q1[1][0];
//			Lw1[1][0] = 1+pow(q1[1][0],2); Lw1[1][1] = -Lw1[0][0];			 Lw1[1][2] = -q1[0][0];
//			Lw1 = 1.0/matchData->imgData0->focalLength*Lw1;
//
//			Lw2[0][0] = q2[0][0]*q2[1][0]; Lw2[0][1] = -(1+pow(q2[0][0],2)); Lw2[0][2] = q2[1][0];
//			Lw2[1][0] = 1+pow(q2[1][0],2); Lw2[1][1] = -Lw2[0][0];			 Lw2[1][2] = -q2[0][0];
//			Lw2 = 1.0/matchData->imgData1->focalLength*Lw2;
//
//			Lw = Lw1.copy();

//			angularVel.inject(0.5*(matchData->imgData0->startAngularVel+matchData->imgData1->endAngularVel));

//			A += matmult(transpose(q2-q1-matmult(Lw,matmult(mRotPhoneToCam,angularVel))),matmult(SnInv, Lv));
			A += matmult(transpose(q2-q1),matmult(SnInv, Lv));
			B += matmult(transpose(Lv), matmult(SnInv, Lv));
		}
		int maxPoints = 50;
		int numPoints = matchData->featurePoints[0].size();
		double scale = min((double)maxPoints, (double)numPoints)/numPoints;
		A = scale*A;
		B = scale*B;
		Array2D<double> temp1 = (dt/z)*A+matmult(transpose(mu_v), SvInv);
		Array2D<double> temp2 = ((dt*dt)/(z*z))*B+SvInv;
		JAMA::LU<double> temp2_TQR(transpose(temp2));
		Array2D<double> vel1 = temp2_TQR.solve(transpose(temp1));

		JAMA::LU<double> B_TLU(transpose(B));
		Array2D<double> velLS1 = z/dt*B_TLU.solve(transpose(A)); // least squares

		// Finally, convert the velocity from camera to phone coords
		Array2D<double> vel = matmult(mRotCamToPhone, vel1);
		Array2D<double> velLS = matmult(mRotCamToPhone, velLS1);

		if(vel.dim1() == 3)
		{
			String str = String()+mStartTime.getElapsedTimeMS() + "\t"+LOG_ID_OPTIC_FLOW+"\t";
			for(int i=0; i<vel.dim1(); i++)
				str = str+vel[i][0]+"\t";
			str = str+matchData->imgData0->timestamp.getElapsedTimeMS()+"\t";
			mQuadLogger->addLine(str,LOG_FLAG_CAM_RESULTS);

			mNewOpticFlowReady = true;

			mMutex_meas.lock();
			mOpticFlowVel.inject(vel);
			mMutex_meas.unlock();
		}
		else
		{
			Log::alert("Why is the optical flow vel corrupted?");
Log::alert("++++++++++++++++++++++++++++++++++++++++++++++++++");
Log::alert(String()+"dt: "+dt);
Log::alert(String()+"z: "+z);
printArray("A:\n",A);
printArray("B:\n",B);
printArray("SvInv:\n",SvInv);
printArray("temp1:\n",temp1);
printArray("temp2:\n",temp2);
printArray("mu_v:\n",mu_v);
printArray("vel1:\n",vel1);
printArray("vel:\n",vel);
		}
		mFlowCalcDone = true;
	
		String str2 = String()+mStartTime.getElapsedTimeMS() + "\t"+LOG_ID_OPTIC_FLOW_LS+"\t";
		for(int i=0; i<velLS.dim1(); i++)
			str2 = str2+velLS[i][0]+"\t";
		mQuadLogger->addLine(str2,LOG_FLAG_CAM_RESULTS);

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
		if(mLastAttBiasUpdateTime.getMS() == 0)
			mLastAttBiasUpdateTime.setTime(); // this will effectively cause dt=0
		double dt = mLastAttBiasUpdateTime.getElapsedTimeUS()/1.0e6;
		mAttBias[0][0] += mAttBiasAdaptRate[0]*dt*err[1][0];
		mAttBias[1][0] += mAttBiasAdaptRate[1]*dt*(-err[0][0]);
		if(mLastForceGainUpdateTime.getMS() == 0)
			mLastForceGainUpdateTime.setTime();
		dt = mLastForceGainUpdateTime.getElapsedTimeUS()/1.0e6;
		if(meas[2][0] > 0.4) // doing this too low runs into problems with ground effect
			mForceGain += mForceGainAdaptRate*dt*err[2][0];
		mLastAttBiasUpdateTime.setTime();
		mLastForceGainUpdateTime.setTime();

		{
			String str1 = String()+mStartTime.getElapsedTimeMS()+"\t"+LOG_ID_OBSV_TRANS_ATT_BIAS+"\t";
			for(int i=0; i<mAttBias.dim1(); i++)
				str1 = str1+mAttBias[i][0]+"\t";
			mQuadLogger->addLine(str1,LOG_FLAG_STATE);

			String str2 = String()+mStartTime.getElapsedTimeMS()+"\t"+LOG_ID_OBSV_TRANS_FORCE_GAIN+"\t";
			str2 = str2+mForceGain+"\t";
			mQuadLogger->addLine(str2,LOG_FLAG_STATE);
		}
		mMutex_data.unlock();

		mDoMeasUpdate = false;
	}

	void Observer_Translational::doMeasUpdateKF_velOnly(TNT::Array2D<double> const &meas, TNT::Array2D<double> const &measCov)
	{
		if(norm2(meas) > 10)
			return; // screwy measuremnt
//		mNewOpticFlowReady = false;
		mMutex_data.lock();
//		Array2D<double> measCov(3,3);
//		measCov[0][0] = mMeasCov[3][3];
//		measCov[0][1] = measCov[1][0] = mMeasCov[3][4];
//		measCov[0][2] = measCov[2][0] = mMeasCov[3][5];
//		measCov[1][1] = mMeasCov[4][4];
//		measCov[1][2] = measCov[2][1] = mMeasCov[4][5];
		Array2D<double> C(3,6,0.0);
		C[0][3] = C[1][4] = C[2][5] = 1;
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

		// update bias and force scaling estimates
		if(mLastAttBiasUpdateTime.getMS() == 0)
			mLastAttBiasUpdateTime.setTime(); // this will effectively cause dt=0
		double dt = min(0.05,mLastAttBiasUpdateTime.getElapsedTimeUS()/1.0e6);
		mAttBias[0][0] += mAttBiasAdaptRate[0]*dt*err[1][0];
		mAttBias[1][0] += mAttBiasAdaptRate[1]*dt*(-err[0][0]);
		mLastAttBiasUpdateTime.setTime();

		{
			String str1 = String()+mStartTime.getElapsedTimeMS()+"\t"+LOG_ID_OBSV_TRANS_ATT_BIAS+"\t";
			for(int i=0; i<mAttBias.dim1(); i++)
				str1 = str1+mAttBias[i][0]+"\t";
			mQuadLogger->addLine(str1,LOG_FLAG_STATE);
		}

		mMutex_data.unlock();
	}

	void Observer_Translational::doMeasUpdateKF_posOnly(Array2D<double> const &meas, Array2D<double> const &measCov)
	{
//		mNewViconPosAvailable = mNewCameraPosAvailable = false;
		mMutex_data.lock();
//		Array2D<double> measCov = submat(mMeasCov,0,2,0,2);
		Array2D<double> C(3,6,0.0);
		C[0][0] = C[1][1] = C[2][2] = 1;
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

		// update bias and force scaling estimates
		if(mLastForceGainUpdateTime.getMS() == 0)
			mLastForceGainUpdateTime.setTime(); // this will effectively cause dt=0
		double dt = min(0.40,mLastForceGainUpdateTime.getElapsedTimeUS()/1.0e6);
		mForceGain += mForceGainAdaptRate*err[2][0];
		mLastForceGainUpdateTime.setTime();
		{
			String str2 = String()+mStartTime.getElapsedTimeMS()+"\t"+LOG_ID_OBSV_TRANS_FORCE_GAIN+"\t";
			str2 = str2+mForceGain+"\t";
			mQuadLogger->addLine(str2,LOG_FLAG_STATE);
		}

		mMutex_data.unlock();
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
// ////////////////////// HACK ///////////////////////
// pos[0][0] -= 0.1;
// ////////////////////// HACK ///////////////////////
		mMutex_meas.lock();
		Array2D<double> vel(3,1,0.0);
		double dt = mLastViconPosTime.getElapsedTimeUS()/1.0e6;
		mLastViconPosTime.setTime();
		if(dt < 0.2)
		{
			// This velocity ``measurement'' is very noisy, but it helps to correct some
			// of the bias error that occurs when the attitude estimate is wrong
			if(dt > 1.0e-3) // reduce the effect of noise
				vel.inject(1.0/dt*(pos-mLastViconPos));
			else
				for(int i=0; i<3; i++)
					vel[i][0] = 0;
		}
		mLastViconPos.inject(pos);
		mLastViconVel.inject(vel);
		if(mUseViconPos)
		{
			mMutex_data.lock();
			mPosMeasBuffer.push_back(mLastViconPos.copy());
			mPosMeasTimeBuffer.push_back(Time());
			mMutex_data.unlock();
		}
		mMutex_meas.unlock();
		mLastPosReceiveTime.setTime();


		{
			String s = String()+mStartTime.getElapsedTimeMS()+"\t"+LOG_ID_RECEIVE_VICON+"\t";
			for(int i=0; i<data.size(); i++)
				s = s+data[i]+"\t";
			mQuadLogger->addLine(s, LOG_FLAG_STATE);
		}

		mNewViconPosAvailable = true;
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

	void Observer_Translational::onNewSensorUpdate(shared_ptr<SensorData> const &data)
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
				if(mPhoneTempData == NULL) 
				{
					mMutex_phoneTempData.unlock();
					return;
				}
				mPhoneTempData->lock();
				float tmuTemp = mPhoneTempData->tmuTemp;
				mPhoneTempData->unlock();
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

//				mDoMeasUpdate_zOnly = true;
				if(mQuadLogger != NULL)
				{
					String s = String() + mStartTime.getElapsedTimeMS() + "\t"+LOG_ID_BAROMETER_HEIGHT+"\t" + h + "\t" + hComp;
					mQuadLogger->addLine(s,LOG_FLAG_PC_UPDATES);
				}
			}
			break;
			case SENSOR_DATA_TYPE_PHONE_TEMP:
			{
				mMutex_phoneTempData.lock();
				mPhoneTempData = static_pointer_cast<SensorDataPhoneTemp>(data);
				mMutex_phoneTempData.unlock();
			}
			break;
		}
	}

	void Observer_Translational::onImageProcessed(shared_ptr<ImageMatchData> const data)
	{
		mMutex_imageData.lock();
//		data.copyTo(mImageData);
		mImageMatchData = data;
		mMutex_imageData.unlock();

		mNewImageResultsReady = true;
	}

	void Observer_Translational::onImageTargetFound(shared_ptr<ImageTargetFindData> const data)
	{
//		double nomLength = 0.111; // m
		double nomRadius = 0.097; // m
		double f = data->imgData->focalLength;

		Array2D<double> att = data->imgData->att.copy();
		Array2D<double> R = createRotMat_ZYX(att[2][0], att[1][0], att[0][0]);
		Array2D<double> R_T = transpose(R);

		float cx = data->imgData->img->cols/2.0;
		float cy = data->imgData->img->rows/2.0;
		Array2D<double> point(3,1);
		point[0][0] = -(data->circleBlobs[0].locationCorrected.x-cx);
		point[1][0] = -(data->circleBlobs[0].locationCorrected.y-cx);
		point[2][0] = -f;
		point = matmult(R_T, matmult(mRotCamToPhone, point));

		double radius = data->circleBlobs[0].radiusCorrected;
		double z = nomRadius*f/radius;
		double x = point[0][0]/f*abs(z);
		double y = point[1][0]/f*abs(z);

		Array2D<double> pos(3,1);
		pos[0][0] = x; 
		pos[1][0] = y; 
		pos[2][0] = z;
		pos[2][0] += 0.030; // offset between camera estimate and Vicon
		mMutex_meas.lock();
///////////////// HACK ////////////////////
pos[2][0] = mLastViconPos[2][0];
///////////////// HACK ////////////////////
		bool doLog = false;
		mMutex_data.lock();
		if( !mHaveFirstCameraPos ||
			(mHaveFirstCameraPos && 
			 norm2(mLastCameraPos-pos) < 0.2) &&
			 norm2(submat(mStateKF,0,2,0,0)-pos) < 0.5
			)
		{
			mHaveFirstCameraPos = true;
			double dt = mLastCameraPosTime.getElapsedTimeUS()/1.0e6;
			if(dt < 0.2)
				mLastCameraVel.inject( 1.0/dt*(pos-mLastCameraPos));
			else
				for(int i=0; i<3; i++)
					mLastCameraVel[i][0] = 0;
			mLastCameraPos.inject(pos);
			mLastCameraPosTime.setTime();
			if(mUseCameraPos)
			{
				mPosMeasBuffer.push_back(mLastCameraPos.copy());
				mPosMeasTimeBuffer.push_back(data->imgData->timestamp);
			}

			doLog = true;
		}
		mMutex_data.unlock();
		
		mMutex_meas.unlock();

		if(doLog)
		{
			String s = String()+mStartTime.getElapsedTimeMS()+"\t"+LOG_ID_CAMERA_POS+"\t";
			s = s+x+"\t"+y+"\t"+z;
			mQuadLogger->addLine(s, LOG_FLAG_CAM_RESULTS);
		}

		mNewCameraPosAvailable = true;
	}

	void Observer_Translational::onNewCommUseIbvs(bool useIbvs)
	{
		mUseIbvs = useIbvs;
		String s;
		if(useIbvs)
			s = String()+mStartTime.getElapsedTimeMS()+"\t"+LOG_ID_IBVS_ENABLED+"\t";
		else
			s = String()+mStartTime.getElapsedTimeMS()+"\t"+LOG_ID_IBVS_DISABLED+"\t";
		mQuadLogger->addLine(s, LOG_FLAG_PC_UPDATES);
	}
} // namespace Quadrotor
} // namespace ICSL
