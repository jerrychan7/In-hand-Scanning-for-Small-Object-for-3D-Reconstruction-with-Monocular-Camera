/****配置SLAM系统，初始化所有线程，配置模式与重置，结束SLAM系统并保存相机轨迹****/
/******mpTracker->GrabImageMonocluar(im,timestamp)*******/
/***调用 SLAM.mpTracker(System类中Tracking型的私有成员变量)->GrabImageMonocluar(Tracking类中成员函数)，传入图像和时间戳，进入Tracking。 ***/
#include "System.h"
#include "Converter.h"
  
#include <thread>
#include <pangolin/pangolin.h>
#include <iostream>
#include <iomanip>

bool has_suffix(const std::string &str, const std::string &suffix)
{

    std::size_t index = str.find(suffix, str.size() - suffix.size());
    return (index != std::string::npos);
}

namespace ORB_SLAM2
{

    // 构造函数。
    System::System(const string &strVocFile, const string &strSettingsFile, const eSensor sensor, const bool bUseViewer):
        mSensor(sensor), mbReset(false), mbActivateLocalizationMode(false), mbDeactivateLocalizationMode(false) 
    {
        // 输出系统欢迎词，传感器类型。
        cout << endl <<
            "ORB-SLAM2 Copyright (C) 2014-2016 Raul Mur-Artal, University of Zaragoza." << endl <<
            "This program comes with ABSOLUTELY NO WARRANTY;" << endl  <<
            "This is free software, and you are welcome to redistribute it" << endl <<
            "under certain conditions. See LICENSE.txt." << endl << endl;
        
        cout << "Input sensor was to set: ";
        if (mSensor == MONOCULAR)
            cout << "Monocular" << endl;
        else if (mSensor == STEREO)
            cout << "Stereo" << endl;
        else if (mSensor == RGBD)
            cout << "RGB-D" << endl;
        
        // 检查相机标定文件是否正常。
        cv::FileStorage fsSetting(strSettingsFile.c_str(), cv::FileStorage::READ);
        if(!fsSetting.isOpened())
        {
            cerr << "Failed to Open settings file at:" << strSettingsFile <<endl;
        }
        
        // 加载ORB词典。
        cout << endl << "Loading ORB Vocabulary. This could take a while..." << endl;
        // new一个ORBVocabulary类型，并且内置构造函数初始化。
        mpVocabulary = new ORBVocabulary();
        // bVocLoad加载成功标志。
        bool bVocLoad= false;
        if(has_suffix(strVocFile, ".txt"))
            bVocLoad = mpVocabulary->loadFromTextFile(strVocFile);
        else if(has_suffix(strVocFile, ".bin"))
            bVocLoad = mpVocabulary->loadFromBinaryFile(strVocFile);
        else 
            bVocLoad = false;
        
        if(!bVocLoad)
        {
            cerr << "Wrong path to vocabulary. " << endl;
            cerr << "Faied to open at: " << strVocFile << endl;
            exit(-1);
        }
        cout << "Vocabulary loaded!" << endl;


        // 创建关键帧库类的对象。
        // mpKeyFrameDatabase = new KeyFrameDatabase(*mpVocabulary);
        mpKeyFrameDatabase = new KeyFrameDatabase(*mpVocabulary);
        // 创建地图类的对象。
        mpMap = new Map();
        // 创建画图类的对象，用于显示。
        mpFrameDrawer = new FrameDrawer(mpMap);
        mpMapDrawer = new MapDrawer(mpMap, strSettingsFile);
        
        // 创建Tracking类的对象，初始化跟踪线程。
        // (这个在主线程main()中完成，称为构造器)。
        mpTracker = new Tracking(this, mpVocabulary, mpFrameDrawer, mpMapDrawer, mpMap, mpKeyFrameDatabase, strSettingsFile, mSensor);

        // 创建Local Mapping类的对象，初始化局部地图线程并启动。
        mpLocalMapper = new LocalMapping(mpMap, mSensor==MONOCULAR);
        // thread(&F, Args)表示，线程入口函数地址为F(可以直接是类的成员函数)，传递的类对象的参数为Args
        mptLocalMapping = new thread(&ORB_SLAM2::LocalMapping::Run, mpLocalMapper);

        // 创建Loop Closing类的对象，初始化闭环检测线程并启动。
        mpLoopCloser = new LoopClosing(mpMap, mpKeyFrameDatabase, mpVocabulary, mSensor!=MONOCULAR);
        mptLoopClosing = new thread(&ORB_SLAM2::LoopClosing::Run, mpLoopCloser);

		// 创建Semidense类的对象，初始化线程并启动
		mpSemiDenseMapping = new ProbabilityMapping(mpMap);
    	mptSemiDense = new thread(&ProbabilityMapping::Run, mpSemiDenseMapping);
        // 创建Viewe类的对象，初始化可视化绘制线程并启动。
        mpViewer = new Viewer(this, mpFrameDrawer, mpMapDrawer, mpTracker, strSettingsFile);
        if(bUseViewer)
            mptViewer = new thread (&Viewer::Run, mpViewer);

        mpTracker->SetViewer(mpViewer);

        // 设置线程间对象的指针,完成对象内该类对象的赋值。
        mpTracker->SetLocalMapper(mpLocalMapper);
        mpTracker->SetLoopClosing(mpLoopCloser);
        
        mpLocalMapper->SetTracker(mpTracker);
        mpLocalMapper->SetLoopCloser(mpLoopCloser);

        mpLoopCloser->SetTracker(mpTracker);
        mpLoopCloser->SetLocalMapper(mpLocalMapper);

    }



    // 程序入口,检测模式，将帧图像传入SLAM系统。
    cv::Mat System::TrackMonocular(const cv::Mat &im, const double &timestamp)
    {

        if(mSensor!=MONOCULAR)
        {
            cerr << "ERROR: you called TrackMonocular but input sensor was not set to Monocular. " << endl;
            exit(-1);
        }
        
        // 模式选择
        {
            // 线程锁定
            unique_lock<mutex> lock(mMutexMode);
            // 是否使能定位模式。
            if(mbActivateLocalizationMode)
            {
                // 使能定位模式，请求停止局部地图进程。
                mpLocalMapper->RequestStop();

                // 等待局部地图进程停止。
                while(!mpLocalMapper->isStopped())
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }   

                mpTracker->InformOnlyTracking(true);    // 定位时只跟踪。
                mbActivateLocalizationMode = false;     // 防止重复执行。 
            }
            // 是否关闭定位模式。
            if (mbDeactivateLocalizationMode)
            {
                mpTracker->InformOnlyTracking(false);
                mpLocalMapper->Release();
                mbDeactivateLocalizationMode = false;   // 防止重复执行。
            }
        }

        // SLAM系统重置检测。
        {
            
            unique_lock<mutex> lock(mMutexReset);
            if(mbReset)
            {
                mpTracker->Reset();
                mbReset = false;                        // 防止重复执行，复原标志位。 
            }
        }
       
        // 调用 SLAM.mpTracker(System类中Tracking型的私有成员变量)->GrabImageMonocluar(Tracking类中成员函数)，传入图像和时间戳，进入Tracking。 
        return mpTracker->GrabImageMonocular(im,timestamp);

    }

    
    // 双目入口
    cv::Mat System::TrackStereo(const cv::Mat &imLeft, const cv::Mat &imRight, const double &timestamp)
    {
        if(mSensor!=STEREO)
        {
            cerr << "ERROR: you called TrackStereo but input sensor was not set to STEREO." << endl;
            exit(-1);
        }   

        // Check mode change
        {
            unique_lock<mutex> lock(mMutexMode);
            if(mbActivateLocalizationMode)
            {
                mpLocalMapper->RequestStop();

                // Wait until Local Mapping has effectively stopped
                while(!mpLocalMapper->isStopped())
                {
                    //usleep(1000);
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }

                mpTracker->InformOnlyTracking(true);// 定位时，只跟踪
                mbActivateLocalizationMode = false;
            }
            if(mbDeactivateLocalizationMode)
            {
                mpTracker->InformOnlyTracking(false);
                mpLocalMapper->Release();
                mbDeactivateLocalizationMode = false;
            }
        }

        // Check reset
        {
            unique_lock<mutex> lock(mMutexReset);
            if(mbReset)
            {
                mpTracker->Reset();
                mbReset = false;
            }
        }

        return mpTracker->GrabImageStereo(imLeft,imRight,timestamp);
    }


    // RGB-D程序入口。
    cv::Mat System::TrackRGBD(const cv::Mat &im, const cv::Mat &depthmap, const double &timestamp)
    {
        if(mSensor!=RGBD)
        {
            cerr << "ERROR: you called TrackRGBD but input sensor was not set to RGBD." << endl;
            exit(-1);
        }    

        // Check mode change
        {
            unique_lock<mutex> lock(mMutexMode);
            if(mbActivateLocalizationMode)
            {
                mpLocalMapper->RequestStop();

                // Wait until Local Mapping has effectively stopped
                while(!mpLocalMapper->isStopped())
                {
                    //usleep(1000);
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }

                mpTracker->InformOnlyTracking(true);// 定位时，只跟踪
                mbActivateLocalizationMode = false;
            }
            if(mbDeactivateLocalizationMode)
            {
                mpTracker->InformOnlyTracking(false);
                mpLocalMapper->Release();
                mbDeactivateLocalizationMode = false;
            }
        }

        // Check reset
        {
        unique_lock<mutex> lock(mMutexReset);
        if(mbReset)
        {
            mpTracker->Reset();
            mbReset = false;
        }
        }

        return mpTracker->GrabImageRGBD(im,depthmap,timestamp);
    }

    
    


    /** 系统配置**/
    // 使能定位和解除定位标志位设置。
    void System::ActivateLocalizationMode()
    {
        unique_lock<mutex> lock(mMutexMode);
        mbActivateLocalizationMode = true;
    }
    void System::DeactivateLocalizationMode()
    {
        unique_lock<mutex> lock(mMutexMode);
        mbDeactivateLocalizationMode = true;
    }

    // 重置标志位设置。
    void System::Reset()
    {
        unique_lock<mutex> lock(mMutexMode);
        mbReset = true;
    }

    // 停止SLAM系统。
    void System::Shutdown()
    {
        // 请求结束局部见图，闭环检测和视图线程。
        mpLocalMapper->RequestFinish();
        mpLoopCloser->RequestFinish();
        mpViewer->RequestFinish();
	mpSemiDenseMapping->RequestFinish();
        // 延时等待，直到所有线程全部结束。
        while( !mpLocalMapper->isFinished() || !mpLoopCloser->isFinished() || !mpSemiDenseMapping->isFinished()||
               !mpViewer->isFinished() || mpLoopCloser->isRunningGBA() )
        {
            // 延时5000us
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }

        pangolin::BindToContext("ORB_SLAM2: Map Viewer");




    // mpLocalMapper->RequestFinish();
    // mpLoopCloser->RequestFinish();
    // if(mpViewer)
    // {
    //     mpViewer->RequestFinish();
    //     while(!mpViewer->isFinished())
    //         usleep(5000);
    // }

    // // Wait until all thread have effectively stopped
    // while(!mpLocalMapper->isFinished() || !mpLoopCloser->isFinished() || mpLoopCloser->isRunningGBA())
    // {
    //     usleep(5000);
    // }

    // if(mpViewer)
    //     pangolin::BindToContext("ORB-SLAM2: Map Viewer");

    }





    /***保存轨迹*****/
    // 保存Tum轨迹。
    void System::SaveTrajectoryTUM(const string &filename)
    {
        
        cout << endl << "Saving camera trajectory to " << filename << "..." << endl;
        
        vector<KeyFrame *> vpKFs = mpMap->GetAllKeyFrames();
        sort(vpKFs.begin(), vpKFs.end(), KeyFrame::lId);

        // 对关键帧位姿进行变换，使第一帧关键帧位于原点。
        // 在闭环检测后第一帧关键帧可能不在原点。
        cv::Mat Two = vpKFs[0]->GetPoseInverse();
        
        ofstream f;
        f.open(filename.c_str());
        f << fixed;

        // Frame位姿是相对于它的参考关键帧的位姿(根据BA和pose graph优化给出)。
        // 需要提取关键帧位姿，然后根据相对变换求帧位姿。
        // 跟踪失败的位姿没有保存。

        // 对于每一帧，记录了它的参考关键帧lRit，时间戳lT和一个标志位lBL(跟踪失败时true)
        list<ORB_SLAM2::KeyFrame *>::iterator lRit = mpTracker->mlpReferences.begin();
        list<double>::iterator lT = mpTracker->mlFrameTimes.begin();
        list<bool>::iterator lbL = mpTracker->mlbLost.begin();
        for(list<cv::Mat>::iterator lit=mpTracker->mlRelativeFramePoses.begin(),lend=mpTracker->mlRelativeFramePoses.end(); lit!=lend; lit++, lRit++, lT++, lbL++ )
        {
            if(*lbL)
                continue;

            KeyFrame *pKF = *lRit;

            cv::Mat Trw= cv::Mat::eye(4,4,CV_32F);

            // 如果参考关键帧被剔除，利用spanning树得到合适的关键帧。
            while(pKF->isBad())
            {
                Trw = Trw*pKF->mTcp;
                pKF = pKF->GetParent();
            }

            Trw= Trw*pKF->GetPose()*Two;
            
            cv::Mat Tcw = (*lit)*Trw;
            cv::Mat Rwc = Tcw.rowRange(0,3).colRange(0,3).t();
            cv::Mat twc = -Rwc*Tcw.rowRange(0,3).col(3);

            vector<float> q = Converter::toQuaternion(Rwc);

            f << setprecision(6) << *lT << " " << setprecision(9) << twc.at<float>(0) << " " << twc.at<float>(1) << " " << twc.at<float>(2) << " " << q[0] << " " << q[1] << " " << q[2] << " " << q[3] << endl;

        }

        f.close();
        cout << endl << "Trajectory saved!" << endl;

    }



    // 保存TUM关键帧路径。
    void System::SaveKeyFrameTrajectoryTUM(const string &filename)
    {
        cout << endl << "Saving KeyFrame trajectory to " << filename << "..." << endl;
        
        vector<KeyFrame *> vpKFs = mpMap->GetAllKeyFrames();
        sort(vpKFs.begin(), vpKFs.end(), KeyFrame::lId);

        // 对关键帧位姿进行变换，使第一帧关键帧位于原点。
        // 在闭环检测后第一帧关键帧可能不在原点。
        // cv::Mat Two = vpKFs[0]->GetPoseInverse();
        
        ofstream f;
        f.open(filename.c_str());
        f << fixed;
        for(size_t i=0; i<vpKFs.size(); i++)
        {

            KeyFrame* pKF = vpKFs[i];

            if(pKF->isBad())
                continue;
            
            cv::Mat R = pKF->GetRotation().t();
            vector<float> q = Converter::toQuaternion(R);
            cv::Mat t = pKF->GetCameraCenter();
            f << setprecision(6) << pKF->mTimeStamp << setprecision(7) << " " << t.at<float>(0) << " " << t.at<float>(1) << " " << t.at<float>(2) << " " << q[0] << " " << q[1] << " " << q[2] << " " << q[3] << endl;

        }

        f.close();
        cout << endl << "trajectory saved" <<endl;

    }



    // 保存KITTI数据集轨迹路径。
    void System::SaveTrajectoryKITTI(const string &filename)
    {

        cout << endl << "Saving camera trajectory to " << filename << " ..." << endl;

        vector<KeyFrame*> vpKFs = mpMap->GetAllKeyFrames();
        sort(vpKFs.begin(),vpKFs.end(),KeyFrame::lId);

        // 对关键帧位姿进行变换，使第一帧关键帧位于原点。
        // 在闭环检测后第一帧关键帧可能不在原点。
        cv::Mat Two = vpKFs[0]->GetPoseInverse();
        
        ofstream f;
        f.open(filename.c_str());
        f << fixed;

        // Frame位姿是相对于它的参考关键帧的位姿(根据BA和pose graph优化给出)。
        // 需要提取关键帧位姿，然后根据相对变换求帧位姿。
        // 跟踪失败的位姿没有保存。

        // 对于每一帧，记录了它的参考关键帧lRit，时间戳lT和一个标志位lBL(跟踪失败时true)
        list<ORB_SLAM2::KeyFrame *>::iterator lRit = mpTracker->mlpReferences.begin();
        list<double>::iterator lT = mpTracker->mlFrameTimes.begin();
        for(list<cv::Mat>::iterator lit=mpTracker->mlRelativeFramePoses.begin(),lend=mpTracker->mlRelativeFramePoses.end(); lit!=lend; lit++, lRit++, lT++ )
        {

            KeyFrame *pKF = *lRit;

            cv::Mat Trw = cv::Mat::eye(4,4,CV_32F);

            // 如果参考关键帧被剔除，利用spanning树得到合适的关键帧。
            while(pKF->isBad())
            {
                Trw = Trw*pKF->mTcp;
                pKF = pKF->GetParent();
            }

            Trw = Trw*pKF->GetPose()*Two;

            cv::Mat Tcw = (*lit)*Trw;
            cv::Mat Rwc = Tcw.rowRange(0,3).colRange(0,3).t();
            cv::Mat twc = -Rwc*Tcw.rowRange(0,3).col(3);

            f << setprecision(9) << Rwc.at<float>(0,0) << " " << Rwc.at<float>(0,1)  << " " << Rwc.at<float>(0,2) << " "  << twc.at<float>(0) << " " << 
                Rwc.at<float>(1,0) << " " << Rwc.at<float>(1,1)  << " " << Rwc.at<float>(1,2) << " "  << twc.at<float>(1) << " " << 
                Rwc.at<float>(2,0) << " " << Rwc.at<float>(2,1)  << " " << Rwc.at<float>(2,2) << " "  << twc.at<float>(2) << endl;

        }

        f.close();
        cout << endl << "trajectory saved!" << endl;
    }




}   // namespace ORB_SLAM2








