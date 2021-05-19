﻿#include <iostream>
#include "Msnhnet/Msnhnet.h"

#ifdef USE_OPENCV 
void yoloface500kGPUOpencv(const std::string& msnhnetPath, const std::string& msnhbinPath, const std::string& imgPath, const std::string& labelsPath)
{
	try
	{
		Msnhnet::NetBuilder  msnhNet;
		Msnhnet::NetBuilder::setOnlyGpu(true);
		msnhNet.buildNetFromMsnhNet(msnhnetPath);
		std::cout << msnhNet.getLayerDetail();
		msnhNet.loadWeightsFromMsnhBin(msnhbinPath);
		std::vector<std::string> labels;
		Msnhnet::IO::readVectorStr(labels, labelsPath.data(), "\n");
		Msnhnet::Point2I inSize = msnhNet.getInputSize();

		std::vector<float> img;
		std::vector<std::vector<Msnhnet::YoloBox>> result;

		img = Msnhnet::OpencvUtil::getPaddingZeroF32C3(imgPath, cv::Size(inSize.x, inSize.y));
		for (size_t i = 0; i < 10; i++)
		{
			auto st = Msnhnet::TimeUtil::startRecord();
			result = msnhNet.runYoloGPU(img);
			std::cout << "time  : " << Msnhnet::TimeUtil::getElapsedTime(st) << "ms" << std::endl << std::flush;
		}

		cv::Mat org = cv::imread(imgPath);
		Msnhnet::OpencvUtil::drawYoloBox(org, labels, result, inSize);
		cv::imshow("test", org);
		cv::waitKey();
	}
	catch (Msnhnet::Exception ex)
	{
		std::cout << ex.what() << " " << ex.getErrFile() << " " << ex.getErrLine() << " " << ex.getErrFun() << std::endl;
	}
}
#endif

void yoloface500kGPUMsnhCV(const std::string& msnhnetPath, const std::string& msnhbinPath, const std::string& imgPath, const std::string& labelsPath)
{
	try
	{
		Msnhnet::NetBuilder  msnhNet;
		Msnhnet::NetBuilder::setOnlyGpu(true);
		msnhNet.buildNetFromMsnhNet(msnhnetPath);
		std::cout << msnhNet.getLayerDetail();
		msnhNet.loadWeightsFromMsnhBin(msnhbinPath);
		std::vector<std::string> labels;
		Msnhnet::IO::readVectorStr(labels, labelsPath.data(), "\n");
		Msnhnet::Point2I inSize = msnhNet.getInputSize();

		std::vector<float> img;
		std::vector<std::vector<Msnhnet::YoloBox>> result;

		img = Msnhnet::CVUtil::getPaddingZeroF32C3(imgPath, { inSize.x,inSize.y });
		for (size_t i = 0; i < 10; i++)
		{
			auto st = Msnhnet::TimeUtil::startRecord();
			result = msnhNet.runYoloGPU(img);
			std::cout << "time  : " << Msnhnet::TimeUtil::getElapsedTime(st) << "ms" << std::endl << std::flush;
		}

		Msnhnet::Mat org(imgPath);
		Msnhnet::CVUtil::drawYoloBox(org, labels, result, inSize);
		
		#ifdef USE_MSNHCV_GUI
        Msnhnet::Gui::imShow("yoloface500k_gpu",org);
		Msnhnet::Gui::wait();
        #else
        org.saveImage("yoloface500k_gpu.jpg");
        #endif
	}
	catch (Msnhnet::Exception ex)
	{
		std::cout << ex.what() << " " << ex.getErrFile() << " " << ex.getErrLine() << " " << ex.getErrFun() << std::endl;
	}
}


int main(int argc, char** argv) 
{
    if(argc != 2)
    {
        std::cout<<"\nYou need to give models dir path.\neg: yolov3 /your/models/dir/path/ \n\nModels folder must be like this:\nmodels\n  |-Lenet5\n    |-Lenet5.msnhnet\n    |-Lenet5.msnhbin";
        getchar();
        return 0;
    }

    std::string msnhnetPath = std::string(argv[1]) + "/yoloface500k/yoloface500k.msnhnet";
    std::string msnhbinPath = std::string(argv[1]) + "/yoloface500k/yoloface500k.msnhbin";
    std::string labelsPath  = "../labels/face.names";
    std::string imgPath = "../images/500face.jpg";
#ifdef USE_OPENCV
	yoloface500kGPUOpencv(msnhnetPath, msnhbinPath, imgPath, labelsPath);
#else
	yoloface500kGPUMsnhCV(msnhnetPath, msnhbinPath, imgPath, labelsPath);
#endif
    return 0;
}
