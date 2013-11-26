#include "ObjectTracker.h"

namespace ICSL {
namespace Quadrotor {
using namespace std;
using namespace TNT;

ObjectTracker::ObjectTracker()
{
	mRunning = false;
	mFinished = true;
	mScheduler = SCHED_NORMAL;
	mThreadPriority = sched_get_priority_min(SCHED_NORMAL);

	mFeatureData = NULL;

	mObsvTranslation = NULL;

	mOldest = NULL;
}

ObjectTracker::~ObjectTracker()
{
	if(mRunning)
		shutdown();
}

void ObjectTracker::initialize()
{
}

void ObjectTracker::shutdown()
{
	Log::alert("-------------------------- Object tracker shutdown started ----------------------");
	mRunning = false;
	while(!mFinished)
		System::msleep(10);

	Log::alert("-------------------------- Object tracker shutdown done ----------------------");
}

void ObjectTracker::run()
{
	mFinished = false;
	mRunning = true;

	sched_param sp;
	sp.sched_priority = mThreadPriority;
	sched_setscheduler(0, mScheduler, &sp);

	Array2D<double> Sn = 2*5*5*createIdentity((double)2);
	Array2D<double> SnInv(2,2,0.0);
	SnInv[0][0] = 1.0/Sn[0][0];
	SnInv[1][1] = 1.0/Sn[1][1];
	double probNoCorr = 5e-4/2;

	shared_ptr<ImageFeatureData> featureData = NULL;
	shared_ptr<ImageFeatureData> prevFeatureData = NULL;

	double dt;
	Array2D<double> curState(9,1,0.0);
	Array2D<double> curErrCov(9,9,0.0);
	Array2D<double> mv(3,1,0.0);
	Array2D<double> Sv(3,3,0.0);
	double mz;
	double varz;

	SO3 curAtt, prevAtt, attChange = curAtt*prevAtt.inv();
	Array2D<double> omega(3,1);

	double f;
	cv::Point2f center;

	vector<shared_ptr<TrackedPoint>> curPoints;
	vector<shared_ptr<TrackedObject>> curObjects;
	while(mRunning)
	{
		mMutex_featureData.lock();
		featureData = mFeatureData;
		mFeatureData = NULL;
		mMutex_featureData.unlock();

		if(featureData != NULL)
		{
			Time curTime = featureData->imageData->timestamp;
			Time prevTime;
			if(prevFeatureData != NULL)
			{
				prevTime = prevFeatureData->imageData->timestamp;
				dt = Time::calcDiffNS(prevTime, curTime)/1.0e9;
			}
			else
				dt = 100;

			// make objects
			curPoints.clear();
			curPoints.resize(featureData->featurePoints.size());
			for(int i=0; i<curPoints.size(); i++)
			{
				curPoints[i] = shared_ptr<TrackedPoint>(new TrackedPoint(curTime, featureData->featurePoints[i]));
				curPoints[i]->setPosCov(Sn);
			}

			curObjects.clear();
			curObjects.resize(curPoints.size());
			for(int i=0; i<curPoints.size(); i++)
				curObjects[i] = curPoints[i];

			/////////////////// Get location priors for active objects ///////////////////////
			curState.inject(mObsvTranslation->estimateStateAtTime(curTime));
			curErrCov.inject(mObsvTranslation->estimateErrCovAtTime(curTime));
			mv.inject(submat(curState,3,5,0,0));
			Sv.inject(submat(curErrCov,3,5,3,5));
			mz = curState[2][0];
			varz= curErrCov[2][2];

			curAtt = mObsvAngular->estimateAttAtTime(curTime);
			prevAtt = mObsvAngular->estimateAttAtTime(prevTime);
			attChange = curAtt*prevAtt.inv();
			omega.inject(1.0/dt*attChange.log().toVector());

			f = featureData->imageData->focalLength;
			center = featureData->imageData->center;
			for(int i=0; i<mTrackedObjects.size(); i++)
			{
				shared_ptr<TrackedObject> to = mTrackedObjects[i];
				to->updatePositionDistribution(mv, Sv, mz, varz, f, center, omega, curTime);
			}

			/////////////////// make matches ///////////////////////
			vector<ObjectMatch> goodMatches;
			vector<shared_ptr<TrackedObject>> repeatObjects, newObjects;
			matchify(curObjects, goodMatches, repeatObjects, newObjects, Sn, SnInv, probNoCorr, curTime);

			// Find the oldest ... just cuz
			if(mTrackedObjects.size() > 0)
			{
				mOldest = mTrackedObjects[0];
				for(int i=1; i<mTrackedObjects.size(); i++)
					if(mTrackedObjects[i]->getCreateTime() < mOldest->getCreateTime())
						mOldest = mTrackedObjects[i];
			}

			// Tell the world
			shared_ptr<ObjectTrackerData> data(new ObjectTrackerData());
			data->timestamp.setTime(featureData->imageData->timestamp);
			data->trackedObjects.swap(repeatObjects);
			data->newObjects.swap(newObjects);
			data->imageData = featureData->imageData;
			data->trackedObjectLocs.resize(repeatObjects.size());
			data->newObjectLocs.resize(newObjects.size());
			for(int i=0; i<repeatObjects.size(); i++)
				data->trackedObjectLocs[i] = repeatObjects[i]->getLocation();
			for(int i=0; i<newObjects.size(); i++)
				data->newObjectLocs[i] = newObjects[i]->getLocation();

			for(int i=0; i<mListeners.size(); i++)
				mListeners[i]->onObjectsTracked(data);


			prevFeatureData = featureData;
		}

		System::msleep(1);
	}

	mFinished = true;
}

void ObjectTracker::onFeaturesFound(const shared_ptr<ImageFeatureData> &data)
{
	mMutex_featureData.lock();
	mFeatureData = data;
	mMutex_featureData.unlock();
}

void ObjectTracker::matchify(const std::vector<std::shared_ptr<ICSL::Quadrotor::TrackedObject>> &curObjects,
						     std::vector<ICSL::Quadrotor::ObjectMatch> &goodMatches,
						     std::vector<std::shared_ptr<ICSL::Quadrotor::TrackedObject>> &repeatPoints,
						     std::vector<std::shared_ptr<ICSL::Quadrotor::TrackedObject>> &newPoints,
						     const TNT::Array2D<double> Sn,
						     const TNT::Array2D<double> SnInv,
						     double probNoCorr,
						     const ICSL::Quadrotor::Time &imageTime)
{
	/////////////////// Establish correspondence based on postiion ///////////////////////
	Array2D<double> C = TrackedObject::calcCorrespondence(mTrackedObjects, curObjects, Sn, SnInv, probNoCorr);
//	Array2D<double> C = TrackedObject::calcCorrespondence2(mTrackedObjects, curObjects, probNoCorr);

	///////////////////  make matches ///////////////////////
	shared_ptr<TrackedObject> toPrev, toCur;
	int N1 = mTrackedObjects.size();
	int N2 = curObjects.size();
	vector<bool> prevMatched(N1, false);
	vector<bool> curMatched(N2, false);
	vector<cv::Point2f> offsets;
	float matchThreshold = 0.6;
	for(int i=0; i<N1; i++)
	{
		if(N2 == 0 || C[i][N2] > 1.0-matchThreshold)
			continue; // this object can't pass the match threshold

		toPrev = mTrackedObjects[i];

		int maxIndex = 0;
		float maxScore = 0;
		for(int j=0; j<N2; j++)
		{
			if(C[i][j] > maxScore && !curMatched[j])
			{
				maxScore = C[i][j];
				maxIndex =j;
			}
		}

		if(maxScore < 0.6)
			continue;

		curMatched[maxIndex] = true;
		toCur = curObjects[maxIndex];

		cv::Point offset = toCur->getLocation()-toPrev->getLocation();
		offsets.push_back(offset);

		int delay = Time::calcDiffMS(toPrev->getLastFoundTime(), toCur->getLastFoundTime()); 
		ObjectMatch m;
		m.prevPos= toPrev->getLocation();
		m.curPos= toCur->getLocation();
		m.score = C[i][maxIndex];
		goodMatches.push_back(m);
		toPrev->markFound(imageTime,toCur->getLocation());
		repeatPoints.push_back(toPrev);
		toCur->kill();
	}

	vector<int> dupIndices;
	for(int j=0; j<curObjects.size(); j++)
		if(!curMatched[j])
		{
			// first check to see if we didn't match because there
			// were too many similar objects
			bool addMe = true;
			if(C.dim1() > 0 && C.dim2() > 0 && C[N1][j] < 0.5)
			{
				// yup, now we should clear out the riff raff
				for(int i=0; i<N1; i++)
					if(C[i][j] > 0.1)
					{
						if(prevMatched[i]) // the dupe already found a good match so we shouldn't delete him
							addMe = false;
						else
							dupIndices.push_back(i);
					}
			}
			// Now add the new region which will be the 
			// only remaining one
			if(addMe)
				newPoints.push_back(curObjects[j]);
			else
				curObjects[j]->kill();
		}

	// sort and remove repeats 
	sort(dupIndices.begin(), dupIndices.end());
	vector<int>::const_iterator endIter = unique(dupIndices.begin(), dupIndices.end());
	vector<int>::const_iterator iter = dupIndices.begin();
	while(iter != endIter)
	{
		mTrackedObjects[(*iter)]->kill();
		iter++;
	}

	sort(mTrackedObjects.begin(), mTrackedObjects.end(), TrackedObject::sortAlivePredicate);
	while(mTrackedObjects.size() > 0 && !mTrackedObjects.back()->isAlive() )
		mTrackedObjects.pop_back();

	for(int i=0; i<newPoints.size(); i++)
		mTrackedObjects.push_back(newPoints[i]);
}


} // namespace Quadrotor 
} // namespace ICSL 
