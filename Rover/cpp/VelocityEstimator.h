#ifndef ICSL_VELOCITY_ESTIMATOR_H
#define ICSL_VELOCITY_ESTIMATOR_H
#include <memory>
#include <sched.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <thread>
#include <mutex>

#include <opencv2/core/core.hpp>
#include <opencv2/imgproc/imgproc.hpp>
#include <opencv2/highgui/highgui.hpp>

#include "TNT/tnt.h"
#include "TNT/jama_lu.h"
#include "TNT/jama_cholesky.h"

#include "Data.h"
#include "Time.h"
#include "DataLogger.h"
#include "Observer_Translational.h"
#include "FeatureFinder.h"
#include "Listeners.h"

#include "toadlet/egg.h"

namespace ICSL {
namespace Quadrotor {

class VelocityEstimator : public FeatureFinderListener,
						  public RegionFinderListener,
						  public CommManagerListener
{
	public:
	VelocityEstimator();
	virtual ~VelocityEstimator();

	void start(){ thread th(&VelocityEstimator::run, this); th.detach(); }
	void run();
	void shutdown();
	void initialize();

	void setThreadPriority(int sched, int priority){mScheduler = sched; mThreadPriority = priority;};
	void setThreadNice(int nice){mThreadNiceValue = nice;};

	void setStartTime(Time t){mStartTime.setTime(t);}
	void setDataLogger(DataLogger *log){mDataLogger = log;}
	void setObserverTranslational(Observer_Translational *obsv){mObsvTranslational = obsv;}
	void setRotPhoneToCam(const TNT::Array2D<double> &rot);

	void addListener(VelocityEstimatorListener *l){mListeners.push_back(l);}

	uint32_t getLastVisionDelayTimeUS(){mMutex_data.lock(); uint32_t temp=mLastDelayTimeUS; mMutex_data.unlock(); return temp;};

	// for CommManagerListener
	void onNewCommVelEstMeasCov(float measCov);
	void onNewCommVelEstProbNoCorr(float probNoCorr);

	// for FeatureFinderListener
	void onFeaturesFound(const shared_ptr<ImageFeatureData> &data);

	// for RegionFinderListener 
	void onRegionsFound(const shared_ptr<ImageRegionData> &data);

	protected:
	bool mRunning, mDone;
	int mThreadPriority, mScheduler, mThreadNiceValue;
	TNT::Array2D<double> mRotPhoneToCam, mRotCamToPhone;
	TNT::Array2D<double> mRotPhoneToCam2, mRotCamToPhone2;
	DataLogger *mDataLogger;
	Time mStartTime;

	Collection<VelocityEstimatorListener*> mListeners;

	bool mNewImageDataAvailable, mNewRegionDataAvailable;
	shared_ptr<ImageFeatureData> mLastImageFeatureData;
	shared_ptr<ImageRegionData> mLastRegionData;

	std::mutex mMutex_imageData, mMutex_data, mMutex_params;

	Observer_Translational *mObsvTranslational;

	uint32_t mLastDelayTimeUS;

	float mMeasCov, mProbNoCorr;

	bool doVelocityEstimate(const shared_ptr<ImageFeatureData> oldFeatureData,
							const shared_ptr<ImageFeatureData> curFeatureData,
							TNT::Array2D<double> &velEstOUT,
							double &heightEstOUT,
							double visionMeasCov,
							double probNoCorr,
							vector<cv::Point2f> &oldPoints,
							vector<cv::Point2f> &curPoints,
							vector<TNT::Array2D<double>> &mDeltaList,
							vector<TNT::Array2D<double>> &SDeltaList,
							vector<pair<TNT::Array2D<double>, TNT::Array2D<double>>> &priorDistList,
							vector<TNT::Array2D<double>> &SdInvmdList,
							vector<TNT::Array2D<double>> &SaInvList,
							vector<TNT::Array2D<double>> &SaList,
							TNT::Array2D<double> &C,
							vector<TNT::Array2D<double>> &LvList,
							vector<TNT::Array2D<double>> &q1HatList,
							vector<TNT::Array2D<double>> &AjList) const;

	bool doVelocityEstimate(const shared_ptr<ImageRegionData> oldRegionData,
						    const shared_ptr<ImageRegionData> curRegionData,
							TNT::Array2D<double> &velEst, 
							double &heightEst,
							double visionMeasCov,
							double probNoCorr) const;

	static inline double fact2ln(int n){return lgamma(2*n+1)-n*log(2)-lgamma(n+1);}

	static vector<pair<TNT::Array2D<double>, TNT::Array2D<double>>> calcPriorDistributions(
																	const vector<cv::Point2f> &points, 
																	const TNT::Array2D<double> &mv, const TNT::Array2D<double> &Sv, 
																	double mz, double varz, 
																	double focalLength, double dt,
																	const TNT::Array2D<double> &omega);

	static void  calcPriorDistributions(vector<TNT::Array2D<double>> &mDeltaList,
										vector<TNT::Array2D<double>> &SDeltaList,
										vector<pair<TNT::Array2D<double>, TNT::Array2D<double>>> &priorDistList,
										const vector<cv::Point2f> &points, 
										const TNT::Array2D<double> &mv, const TNT::Array2D<double> &Sv, 
										double mz, double varz, 
										double focalLength, double dt,
										const TNT::Array2D<double> &omega);

	static TNT::Array2D<double> calcCorrespondence(const vector<pair<TNT::Array2D<double>, TNT::Array2D<double>>> &priorDistList, 
											  const vector<cv::Point2f> &curPointList, 
											  const TNT::Array2D<double> &Sn, 
											  const TNT::Array2D<double> &SnInv,
											  float probNoCorr);

	static void calcCorrespondence(TNT::Array2D<double> &C,
							  const vector<pair<TNT::Array2D<double>, TNT::Array2D<double>>> &priorDistList, 
							  const vector<cv::Point2f> &curPointList, 
							  const TNT::Array2D<double> &Sn, 
							  const TNT::Array2D<double> &SnInv,
							  vector<TNT::Array2D<double>> &SdInvmdList,
							  vector<TNT::Array2D<double>> &SaInvList,
							  vector<TNT::Array2D<double>> &SaList,
							  float probNoCorr);
	
	static void computeMAPEstimate(TNT::Array2D<double> &velMAP /*out*/, TNT::Array2D<double> &covVel /*out*/, double &heightMAP /*out*/,
							const vector<cv::Point2f> &prevPoints,
							const vector<cv::Point2f> &curPoints, 
							const TNT::Array2D<double> &C, // correspondence matrix
							const TNT::Array2D<double> &mv, // velocity mean
							const TNT::Array2D<double> &Sv, // velocity covariance
							double mz, // height mean
							double vz, // height variance
							const TNT::Array2D<double> &Sn, // feature measurement covariance
							double focalLength, double dt, const TNT::Array2D<double> &omega);

	static void computeMAPEstimate(TNT::Array2D<double> &velMAP /*out*/, TNT::Array2D<double> &covVel /*out*/, double &heightMAP /*out*/,
							const vector<cv::Point2f> &prevPoints,
							const vector<cv::Point2f> &curPoints, 
							const TNT::Array2D<double> &C, // correspondence matrix
							const TNT::Array2D<double> &mv, // velocity mean
							const TNT::Array2D<double> &Sv, // velocity covariance
							vector<TNT::Array2D<double>> &LvList, // temp variable
							vector<TNT::Array2D<double>> &q1HatList,
							vector<TNT::Array2D<double>> &AjList,
							double mz, // height mean
							double vz, // height variance
							const TNT::Array2D<double> &Sn, // feature measurement covariance
							double focalLength, double dt, const TNT::Array2D<double> &omega);
	
	static void computeMAPEstimate(TNT::Array2D<double> &velMAP /*out*/, TNT::Array2D<double> &covVel /*out*/, double &heightMAP /*out*/,
							const vector<cv::Point2f> &prevPoints,
							const vector<cv::Point2f> &curPoints, 
							const TNT::Array2D<double> &C, // correspondence matrix
							const TNT::Array2D<double> &mv, // velocity mean
							const TNT::Array2D<double> &Sv, // velocity covariance
							double mz, // height mean
							double vz, // height variance
							const TNT::Array2D<double> &Sn, // feature measurement covariance
							double focalLength, double dt, const TNT::Array2D<double> &omega,
							int maxPointCnt);

	static void computeMAPEstimate(TNT::Array2D<double> &velMAP /*out*/, TNT::Array2D<double> &covVel /*out*/, double &heightMAP /*out*/,
							const vector<cv::Point2f> &prevPoints,
							const vector<cv::Point2f> &curPoints, 
							const TNT::Array2D<double> &C, // correspondence matrix
							const TNT::Array2D<double> &mv, // velocity mean
							const TNT::Array2D<double> &Sv, // velocity covariance
							vector<TNT::Array2D<double>> &LvList, // temp variable
							vector<TNT::Array2D<double>> &q1HatList,
							vector<TNT::Array2D<double>> &AjList,
							double mz, // height mean
							double vz, // height variance
							const TNT::Array2D<double> &Sn, // feature measurement covariance
							double focalLength, double dt, const TNT::Array2D<double> &omega,
							int maxPointCnt);
	
};

} // namespace Rover
} // namespace ICSL

#endif
