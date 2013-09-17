#include "SensorManager.h"

namespace ICSL{
namespace Quadrotor{
	using namespace std;
	using namespace TNT;
	using namespace ICSL::Constants;

	SensorManager::SensorManager() :
		mLastAccel(3,1,0.0),
		mLastGyro(3,1,0.0),
		mLastMag(3,1,0.0),
		mLastPressure(0)
	{
		mRunning = false;
		mDone = true;

		mRotCamToPhone = matmult(createRotMat(2,-0.5*(double)PI),
								 createRotMat(0,(double)PI));
		mRotPhoneToCam = transpose(mRotCamToPhone);

		mTimestampOffsetNS = 0;

		mScheduler = SCHED_NORMAL;
		mThreadPriority = sched_get_priority_min(SCHED_NORMAL);

		mImageDT = 0;
		mImageCnt = 0;
		mLastImageTime.setTimeMS(0);

		mCameraMatrix_640x480 = mCameraMatrix_320x240 = NULL;
		mCameraDistortionCoeffs = NULL;

		mObsvAngular = NULL;

		mLastHeight = 0;
		mNewHeightAvailable = false;
	}

	SensorManager::~SensorManager()
	{
	}

	void SensorManager::initialize()
	{
		mSensorManager = ASensorManager_getInstance();
		ALooper *looper = ALooper_prepare(ALOOPER_PREPARE_ALLOW_NON_CALLBACKS);
		mSensorEventQueue = ASensorManager_createEventQueue(mSensorManager, looper, 0, NULL, NULL);

		mAccelSensor = ASensorManager_getDefaultSensor(mSensorManager, ASENSOR_TYPE_ACCELEROMETER);
		if(mAccelSensor != NULL)
		{
			const char* name = ASensor_getName(mAccelSensor);
			const char* vendor = ASensor_getVendor(mAccelSensor);
			float res = ASensor_getResolution(mAccelSensor);
			Log::alert(String()+"Accel sensor:\n\t"+name+"\n\t"+vendor+"\n\tresolution:"+res);
			ASensorEventQueue_enableSensor(mSensorEventQueue, mAccelSensor);
			ASensorEventQueue_setEventRate(mSensorEventQueue, mAccelSensor, 10*1000); // this is the best it actually achieves
		}

		mGyroSensor = ASensorManager_getDefaultSensor(mSensorManager, ASENSOR_TYPE_GYROSCOPE);
		if(mGyroSensor != NULL)
		{
			const char* name = ASensor_getName(mGyroSensor);
			const char* vendor = ASensor_getVendor(mGyroSensor);
			float res = ASensor_getResolution(mGyroSensor);
			Log::alert(String()+"Gyro sensor:\n\t"+name+"\n\t"+vendor+"\n\tresolution: "+res);
			ASensorEventQueue_enableSensor(mSensorEventQueue, mGyroSensor);
			ASensorEventQueue_setEventRate(mSensorEventQueue, mGyroSensor, 5*1000);
		}

		mMagSensor = ASensorManager_getDefaultSensor(mSensorManager, ASENSOR_TYPE_MAGNETIC_FIELD);
		if(mMagSensor != NULL)
		{
			const char* name = ASensor_getName(mMagSensor);
			const char* vendor = ASensor_getVendor(mMagSensor);
			float res = ASensor_getResolution(mMagSensor);
			Log::alert(String()+"Mag sensor:\n\t"+name+"\n\t"+vendor+"\n\tresolution: "+res);
			ASensorEventQueue_enableSensor(mSensorEventQueue, mMagSensor);
			ASensorEventQueue_setEventRate(mSensorEventQueue, mMagSensor, 10*1000); // this is the best it actually achieves
		}

		mPressureSensor = ASensorManager_getDefaultSensor(mSensorManager, ASENSOR_TYPE_PRESSURE);
		if(mPressureSensor != NULL)
		{
			const char* name = ASensor_getName(mPressureSensor);
			const char* vendor = ASensor_getVendor(mPressureSensor);
			float res = ASensor_getResolution(mPressureSensor);
			Log::alert(String()+"Pressure sensor:\n\t"+name+"\n\t"+vendor+"\n\tresolution: "+res);
			ASensorEventQueue_enableSensor(mSensorEventQueue, mPressureSensor);
			ASensorEventQueue_setEventRate(mSensorEventQueue, mPressureSensor, 10*1000); // this is the best it actually achieves
		}

		// list out all available sensors
//		const ASensor* const* sensorList;
//		int numSensors = ASensorManager_getSensorList(mSensorManager, &sensorList);
//		for(int i=1; i<numSensors; i++)
//		{
//			const char* name = ASensor_getName(sensorList[i]);
//			const char* vendor=  ASensor_getVendor(sensorList[i]);
//			float res = ASensor_getResolution(sensorList[i]);
//			int type = ASensor_getType(sensorList[i]);
//			String str = "Sensor \n";
//			str = str+"\t"+name+"\n";
//			str = str+"\t"+vendor+"\n";
//			str = str+"\t"+type+"\n";
//			str = str+"\tresolution: "+res;
//			Log::alert(str);
//		}

		// Now try load camera calibration params
		cv::FileStorage fs;
//		String filename = "sdcard/RoverService/s4Calib_640x480.yml";
		String filename = "sdcard/RoverService/s3Calib_640x480.yml";
		fs.open(filename.c_str(), cv::FileStorage::READ);
		if( fs.isOpened() )
		{
			mCameraMatrix_640x480 = shared_ptr<cv::Mat>(new cv::Mat());
			mCameraDistortionCoeffs = shared_ptr<cv::Mat>(new cv::Mat());

			String str = "Camera calib loaded from " + filename;
			fs["camera_matrix"] >> *mCameraMatrix_640x480;
			fs["distortion_coefficients"] >> *mCameraDistortionCoeffs;
			str = str+"\n\t"+"Focal length: " + mCameraMatrix_640x480->at<double>(0,0);
			str = str+"\n\t"+"centerX: " + mCameraMatrix_640x480->at<double>(0,2);
			str = str+"\n\t"+"centerY: " + mCameraMatrix_640x480->at<double>(1,2);
			Log::alert(str);

			mCameraMatrix_320x240 = shared_ptr<cv::Mat>(new cv::Mat());
			mCameraMatrix_640x480->copyTo( *mCameraMatrix_320x240 );
			(*mCameraMatrix_320x240) = (*mCameraMatrix_320x240)*0.5;
		}
		else
		{
			Log::alert("Failed to open " + filename);
		}
		fs.release();
	}

	void SensorManager::shutdown()
	{
		Log::alert("////////////////////// SensorManager shutdown started //////////////////");
		mRunning = false;
		while(!mDone) 
			System::msleep(10);

		if(mSensorEventQueue != NULL && mMagSensor != NULL)
			ASensorEventQueue_disableSensor(mSensorEventQueue, mMagSensor);
		if(mSensorEventQueue != NULL && mAccelSensor != NULL)
			ASensorEventQueue_disableSensor(mSensorEventQueue, mAccelSensor);
		if(mSensorEventQueue != NULL && mGyroSensor != NULL)
			ASensorEventQueue_disableSensor(mSensorEventQueue, mGyroSensor);
		if(mSensorEventQueue != NULL && mPressureSensor != NULL)
			ASensorEventQueue_disableSensor(mSensorEventQueue, mPressureSensor);

		if(mSensorManager != NULL && mSensorEventQueue != NULL)
			ASensorManager_destroyEventQueue(mSensorManager, mSensorEventQueue);
		Log::alert("////////////////////// SensorManager shutdown done //////////////////");
	}

	void SensorManager::run()
	{
		mRunning = true;
		thread tempMonitorTh(&SensorManager::runTemperatureMonitor, this);
		thread heightMonitorTh(&SensorManager::runHeightMonitor, this);

		double accelCal1X = 0.05;
		double accelCal1Y = 0.06;
		double accelCal1Z = 9.38;
		double accelCal2X = 0.06;
		double accelCal2Y = -0.16;
		double accelCal2Z = -10.10;

		double accelScaleZ = 0.5*(accelCal1Z-accelCal2Z)/GRAVITY;
		// don't have information on these so assume they are the same
		double accelScaleX = accelScaleZ;
		double accelScaleY = accelScaleZ;

		double accelOffX = 0.5*(accelCal1X+accelCal2X);
		double accelOffY = 0.5*(accelCal1Y+accelCal2Y);
		double accelOffZ = 0.5*(accelCal1Z+accelCal2Z);

		Array2D<double> accelCalibrated(3,1), gyroCalibrated(3,1), magCalibrated(3,1);

		mDone = false;
		sched_param sp;
		sp.sched_priority = mThreadPriority;
		sched_setscheduler(0, mScheduler, &sp);
		while(mRunning)
		{
			ASensorEvent event;
			while(ASensorEventQueue_getEvents(mSensorEventQueue, &event, 1) > 0)
			{
				if(mTimestampOffsetNS == 0)
					mTimestampOffsetNS = mStartTime.getNS()-event.timestamp;
				LogFlags logFlag = LOG_FLAG_OTHER;
				LogID logID = LOG_ID_UNKNOWN;
				shared_ptr<IData> data = NULL;
				switch(event.type)
				{
					case ASENSOR_TYPE_PRESSURE:
						{
							logFlag= LOG_FLAG_PRESSURE;
							logID = LOG_ID_PRESSURE;
//							data = shared_ptr<Data<double> >(new Data<double>(event.pressure, DATA_TYPE_PRESSURE));
							data = shared_ptr<IData>(new Data<double>());
							data->type = DATA_TYPE_PRESSURE;
							static_pointer_cast<Data<double> >(data)->data = event.pressure;
							static_pointer_cast<Data<double> >(data)->dataCalibrated = event.pressure;
							mLastPressure = event.pressure;
						}
						break;
					case ASENSOR_TYPE_ACCELEROMETER:
						{
							logFlag= LOG_FLAG_ACCEL;
							logID = LOG_ID_ACCEL;

							mMutex_data.lock();
							mLastAccel[0][0] = event.data[0];
							mLastAccel[1][0] = event.data[1];
							mLastAccel[2][0] = event.data[2];
							accelCalibrated[0][0] = (event.data[0]-accelOffX)/accelScaleX;
							accelCalibrated[1][0] = (event.data[1]-accelOffY)/accelScaleY;
							accelCalibrated[2][0] = (event.data[2]-accelOffZ)/accelScaleZ;
							data = shared_ptr<IData>(new DataVector<double>());
							data->type = DATA_TYPE_ACCEL;
							static_pointer_cast<DataVector<double> >(data)->data = mLastAccel.copy();
							static_pointer_cast<DataVector<double> >(data)->dataCalibrated = accelCalibrated.copy();
							mMutex_data.unlock();
						}
						break;
					case ASENSOR_TYPE_GYROSCOPE:
						{
							logFlag= LOG_FLAG_GYRO;
							logID = LOG_ID_GYRO;

							mMutex_data.lock();
							mLastGyro[0][0] = event.data[0];
							mLastGyro[1][0] = event.data[1];
							mLastGyro[2][0] = event.data[2];
							data = shared_ptr<IData>(new DataVector<double>());
							data->type = DATA_TYPE_GYRO;
							static_pointer_cast<DataVector<double> >(data)->data = mLastGyro.copy();
// TODO: Apply gyro bias estimate for the calibrated data
							static_pointer_cast<DataVector<double> >(data)->dataCalibrated = mLastGyro.copy();
							mMutex_data.unlock();
						}
						break;
					case ASENSOR_TYPE_MAGNETIC_FIELD:
						{
							logFlag= LOG_FLAG_MAGNOMETER;
							logID = LOG_ID_MAGNOMETER;
							mMutex_data.lock();
							mLastMag[0][0] = event.data[0];
							mLastMag[1][0] = event.data[1];
							mLastMag[2][0] = event.data[2];
							data = shared_ptr<IData>(new DataVector<double>());
							data->type = DATA_TYPE_MAG;
							static_pointer_cast<DataVector<double> >(data)->data = mLastMag.copy();
							static_pointer_cast<DataVector<double> >(data)->dataCalibrated = mLastMag.copy();
							mMutex_data.unlock();
						}
						break;
					default:
						Log::alert(String()+"Unknown sensor event: "+event.type);
				}
				data->timestamp.setTimeNS(event.timestamp+mTimestampOffsetNS);

				if(mQuadLogger != NULL && logFlag != -1 && data != NULL)
				{
					String s=String();
					s = s+Time::calcDiffMS(mStartTime, data->timestamp)+"\t";
					s = s+event.data[0]+"\t"+event.data[1]+"\t"+event.data[2]+"\t"+event.data[3];
					mMutex_logger.lock();
					mQuadLogger->addEntry(logID,s,logFlag);
					mMutex_logger.unlock();
				}

				if(data != NULL)
				{
					mMutex_listeners.lock();
					for(int i=0; i<mListeners.size(); i++)
						mListeners[i]->onNewSensorUpdate(data);
					mMutex_listeners.unlock();
				}
			}


			System::usleep(100);
		}

		tempMonitorTh.join();
		Log::alert("waiting on height monitor");
		heightMonitorTh.join();

		mDone = true;
	}

	void SensorManager::runTemperatureMonitor()
	{
		sched_param sp;
		sp.sched_priority = mThreadPriority-1;
		sched_setscheduler(0, mScheduler, &sp);
		while(mRunning)
		{
			float battTemp = getBatteryTemp()/10.0;
			float secTemp = getSecTemp()/10.0;
			float fgTemp= getFuelgaugeTemp()/10.0;

			shared_ptr<DataPhoneTemp<double> > data = shared_ptr<DataPhoneTemp<double> >(new DataPhoneTemp<double>());
			data->battTemp = battTemp;
			data->secTemp = secTemp;
			data->fgTemp = fgTemp;
			mMutex_listeners.lock();
			for(int i=0; i<mListeners.size(); i++)
				mListeners[i]->onNewSensorUpdate(data);
			mMutex_listeners.unlock();

			if(mQuadLogger != NULL)
			{
				String s = String();
				s = s+battTemp+"\t";
				s = s+secTemp+"\t";
				s = s+fgTemp+"\t";
				mMutex_logger.lock();
				mQuadLogger->addEntry(LOG_ID_PHONE_TEMP,s,LOG_FLAG_PHONE_TEMP);
				mMutex_logger.unlock();
			}

			System::msleep(500);
		}
	}

	void SensorManager::runHeightMonitor()
	{
		sched_param sp;
		sp.sched_priority = mThreadPriority-1;
		sched_setscheduler(0, mScheduler, &sp);
		double lastHeight=0;
		mNewHeightAvailable = false;
		while(mRunning)
		{
			if(mNewHeightAvailable)
			{
				mMutex_vicon.lock();
				lastHeight = mLastHeight;
				mMutex_vicon.unlock();

				shared_ptr<HeightData<double>> data(new HeightData<double>);
				data->type = DATA_TYPE_HEIGHT;
				data->heightRaw = lastHeight;
				data->height = lastHeight;

				mMutex_listeners.lock();
				for(int i=0; i<mListeners.size(); i++)
					mListeners[i]->onNewSensorUpdate(data);
				mMutex_listeners.unlock();
			}

			// simulate a 20Hz update rate for now
			// in the future this will be small but
			// mNewHeightAvailable is the flag for new
			// data
			System::msleep(50);
		}
	}

	int SensorManager::getBatteryTemp()
	{
		int temp = 0;
		string filename = "/sys/class/power_supply/battery/temp";
		ifstream file(filename.c_str());
		if(file.is_open())
		{
			string line;
			getline(file,line);
			file.close();

			stringstream ss(line);
			ss >> temp;
		}
		else
			Log::alert("Failed to open "+String(filename.c_str()));

		return temp;
	}

	int SensorManager::getSecTemp()
	{
		int temp = 0;
		// this path is for the SIII
		string filename = "/sys/devices/platform/sec-thermistor/temperature";
		ifstream file(filename.c_str());
		if(file.is_open())
		{
			string line;
			getline(file,line);
			file.close();

			stringstream ss(line);
			ss >> temp;
		}
		else
			Log::alert("Failed to open "+String(filename.c_str()));

		return temp;
	}

	int SensorManager::getFuelgaugeTemp()
	{
		int temp = 0;
		string filename = "/sys/class/power_supply/max17047-fuelgauge/temp";
		ifstream file(filename.c_str());
		if(file.is_open())
		{
			string line;
			getline(file,line);
			file.close();

			stringstream ss(line);
			ss >> temp;
		}
		else
			(("Failed to open "+filename).c_str());

		return temp;
	}

	void SensorManager::passNewImage(const cv::Mat *imageYUV, int64 timestampNS)
	{
		shared_ptr<DataImage> data(new DataImage());
		data->type = DATA_TYPE_IMAGE;
		data->timestamp.setTimeNS(timestampNS);

		// do this now just so the log time is a bit closer to real
		if(mQuadLogger != NULL)
		{
			String str = String();
			str = str+Time::calcDiffMS(mStartTime, data->timestamp)+"\t";
			str = str+data->imageId;
			mMutex_logger.lock();
			mQuadLogger->addEntry(LOG_ID_IMAGE,str,LOG_FLAG_CAM_RESULTS);
			mMutex_logger.unlock();
		}

		shared_ptr<cv::Mat> imageBGR(new cv::Mat);
		cv::cvtColor(*imageYUV, *imageBGR, CV_YUV420sp2BGR);

		if(mObsvAngular != NULL)
			data->att = mObsvAngular->estimateAttAtTime( data->timestamp );

		data->image = imageBGR;
		shared_ptr<cv::Mat> gray(new cv::Mat());
		cv::cvtColor(*imageBGR, *gray, CV_BGR2GRAY);
		data->imageGray = gray;
		data->imageFormat = IMG_FORMAT_BGR;
		data->cap = NULL;
		if(imageBGR->rows = 240)
			data->cameraMatrix = mCameraMatrix_320x240;
		else
			data->cameraMatrix = mCameraMatrix_640x480;
		data->focalLength = data->cameraMatrix->at<double>(0,0);
		data->centerX = data->cameraMatrix->at<double>(0,2);
		data->centerY = data->cameraMatrix->at<double>(1,2);
		data->distCoeffs = mCameraDistortionCoeffs;

		mMutex_listeners.lock();
		for(int i=0; i<mListeners.size(); i++)
			mListeners[i]->onNewSensorUpdate(static_pointer_cast<IData>(data));
		mMutex_listeners.unlock();
	}

	void SensorManager::onNewCommStateVicon(const Collection<float> &data)
	{
		mMutex_vicon.lock();
		mLastHeight = data[8];
		mMutex_vicon.unlock();

		mNewHeightAvailable = true;
	}

} // namespace Quadrotor
} // namespace ICSL
