﻿#ifndef MSNHADDBLOCKLAYER_H
#define MSNHADDBLOCKLAYER_H

#include "Msnhnet/config/MsnhnetCfg.h"
#include "Msnhnet/layers/MsnhActivationLayer.h"
#include "Msnhnet/layers/MsnhBatchNormLayer.h"
#include "Msnhnet/layers/MsnhBaseLayer.h"
#include "Msnhnet/layers/MsnhConnectedLayer.h"
#include "Msnhnet/layers/MsnhConvolutionalLayer.h"
#include "Msnhnet/layers/MsnhCropLayer.h"
#include "Msnhnet/layers/MsnhDeConvolutionalLayer.h"
#include "Msnhnet/layers/MsnhLocalAvgPoolLayer.h"
#include "Msnhnet/layers/MsnhMaxPoolLayer.h"
#include "Msnhnet/layers/MsnhRouteLayer.h"
#include "Msnhnet/layers/MsnhSoftMaxLayer.h"
#include "Msnhnet/layers/MsnhUpSampleLayer.h"
#include "Msnhnet/layers/MsnhEmptyLayer.h"
#include "Msnhnet/layers/MsnhPaddingLayer.h"
#include "Msnhnet/net/MsnhNetBuilder.h"
#include "Msnhnet/io/MsnhParser.h"
#include "Msnhnet/utils/MsnhExport.h"

namespace Msnhnet
{
class NetBuildParams;

class MsnhNet_API AddBlockLayer:public BaseLayer
{
public:
    AddBlockLayer(const int &batch, NetBuildParams &params, std::vector<std::vector<BaseParams*>> &branchParams, ActivationType &activation, const std::vector<float> &actParams);

    std::vector<std::vector<BaseLayer *>> branchLayers;
    float       *activationInput    =   nullptr;

    void loadAllWeigths(std::vector<float> &weights);

    virtual void saveAllWeights(const int &mainIdx, const int &branchIdx=-1, const int &branchIdx1=-1);
    virtual void mallocMemory();
    virtual void forward(NetworkState &netState);

#ifdef USE_GPU
    virtual void forwardGPU(NetworkState &netState);
#endif

#ifdef USE_OPENCL
    virtual void forwardCL(NetworkState &netState);
#endif

    ~AddBlockLayer();

};
}

#endif 

