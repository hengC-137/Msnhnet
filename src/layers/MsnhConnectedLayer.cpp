﻿#include "Msnhnet/layers/MsnhConnectedLayer.h"
namespace Msnhnet
{
ConnectedLayer::ConnectedLayer(const int &batch, const int &steps, const int &inputNum,
                               const int &outputNum, const ActivationType &activation, const std::vector<float> &actParams, const int &batchNorm, const float &bnEps, const int &useBias)
{
    this->_totalBatch    =   batch*steps;
    this->_type          =   LayerType::CONNECTED;
    this->_layerName     =  "Connected       ";

    this->_inputNum      =   inputNum;
    this->_outputNum     =   outputNum;
    this->_batch         =   batch;
    this->_batchNorm     =   batchNorm;

    this->_height        =   1;
    this->_width         =   1;
    this->_channel       =   inputNum;

    this->_outHeight     =   1;
    this->_outWidth      =   1;
    this->_outChannel    =   outputNum;

    this->_num           =   this->_outChannel;
    this->_kSize         =   1;
    this->_stride        =   1;
    this->_padding       =   0;
    this->_bnEps         =   bnEps;

    this->_activation    =   activation;
    this->_actParams     =   actParams;

    this->_useBias       =   useBias;

    this->_nWeights      =   inputNum * outputNum;

    if(this->_useBias)
    {
        this->_nBiases   =   outputNum;
    }
    else
    {
        this->_nBiases   =   0;
    }

    if(batchNorm)
    {
        this->_nScales       =   outputNum;
        this->_nRollMean     =   outputNum;
        this->_nRollVariance =   outputNum;
    }

    if(activation==ActivationType::PRELU) 

    {
        this->_nPreluWeights = outputNum;
    }
    else
    {
        this->_nPreluWeights = 0;
    }

    this->_numWeights            =   static_cast<size_t>(this->_nWeights + this->_nScales + this->_nRollMean + this->_nRollVariance + this->_nBiases + this->_nPreluWeights);

    this->_inputSpaceSize        =   _inputNum;

    this->_maxOutputNum  = this->_batch*this->_outputNum;

#ifdef USE_OPENCL
    this->_kernel_con = clScheduler::get().buildKernel(LayerType::CONVOLUTIONAL, "IM2COL_GEMM_c");
    if (this->_activation != ActivationType::NONE) {
        this->_kernel_act = clScheduler::get().buildKernel(LayerType::ACTIVE, Activations::getActivationStr(this->_activation));
    }
    if (this->_batchNorm) {
        // _kernel_bn = clScheduler::get().buildKernel(LayerType::BATCHNORM, "batch_norm");
    }



#endif

    char msg[100];
#ifdef WIN32
    sprintf_s(msg, "connected                            %4d  ->  %4d\n", inputNum, outputNum);
#else
    sprintf(msg, "connected                            %4d  ->  %4d\n", inputNum, outputNum);
#endif
    this->_layerDetail       = msg;
}

ConnectedLayer::~ConnectedLayer()
{
    releaseArr(_weights);
    releaseArr(_biases);
    releaseArr(_scales);
    releaseArr(_rollMean);
    releaseArr(_rollVariance);
#ifdef USE_GPU
    Cuda::freeCuda(_gpuWeights);
    Cuda::freeCuda(_gpuBiases);
    Cuda::freeCuda(_gpuScales);
    Cuda::freeCuda(_gpuRollMean);
    Cuda::freeCuda(_gpuRollVariance);
#endif

    if(this->_activation==ActivationType::PRELU) 

    {
        releaseArr(this->_preluWeights);
#ifdef USE_GPU

        Cuda::freeCuda(this->_gpuPreluWeights);
#endif
    }
}

void ConnectedLayer::forward(NetworkState &netState)
{
    auto st = TimeUtil::startRecord();

    float* layerInput   = netState.getInput();
    float* layerOutput  = nullptr;

    /* 输入 */
    if(this->_isBranchLayer) 

    {
        if(this->_isFirstBranch)

        {
            layerInput      = netState.input;
        }
    }
    else
    {
        if(this->_layerIndex == 0) 

        {
            layerInput      = netState.input;
        }
        else 

        {
            if(netState.net->layers[this->_layerIndex - 1]->getMemReUse() == 0)

            {
                layerInput  = netState.input;
            }
        }
    }

    /* 输出 */
    if(this->_isBranchLayer) 

    {
        if(this->_isLastBranch)

        {
            layerOutput     = this->_output; 

        }
        else 

        {
            layerOutput     = netState.getOutput(); 

            netState.shuffleInOut();

        }
    }
    else
    {
        if(this->_memReUse==1) 

        {
            layerOutput     = netState.getOutput(); 

            netState.shuffleInOut();

        }
        else

        {
            layerOutput     = this->_output;
        }
    }

    Blas::cpuFill(this->_outputNum * this->_batch, 0, layerOutput, 1);
    int m       =   this->_batch;
    int k       =   this->_inputNum;
    int n       =   this->_outputNum;

    float *a    =   layerInput;
    float *b    =   this->_weights;
    float *c    =   layerOutput;

    Gemm::cpuGemm(0,1,m,n,k,1,a,k,b,k,1,c,n,this->supportAvx&&this->supportFma);

    if(this->_batchNorm == 1)
    {

        Blas::cpuNorm(layerOutput, this->_rollMean, this->_rollVariance, this->_batch, this->_outputNum, this->_bnEps, 1);

        ConvolutionalLayer::scaleBias(layerOutput, this->_scales, this->_batch, this->_outputNum, 1);

        for (int i = 0; i < this->_batch; ++i)
        {
            Blas::cpuAxpy(this->_outputNum, 1, this->_biases, 1, layerOutput+ i * this->_outputNum, 1);
        }
    }
    else
    {
        if(this->_useBias)
        {
            for (int i = 0; i < this->_batch; ++i)
            {
                Blas::cpuAxpy(this->_outputNum, 1, this->_biases, 1, layerOutput + i * this->_outputNum, 1);
            }
        }
    }

    if(     this->_activation==ActivationType::NORM_CHAN||
            this->_activation==ActivationType::NORM_CHAN_SOFTMAX||
            this->_activation==ActivationType::NORM_CHAN_SOFTMAX_MAXVAL||
            this->_activation==ActivationType::NONE)
    {

        this->_forwardTime  =   TimeUtil::getElapsedTime(st);
        return;
    }
    else if(this->_activation == ActivationType::PRELU)

    {
        Activations::activatePRelu(layerOutput,this->_batch, this->_outChannel,this->_preluWeights, this->_outWidth*this->_outHeight, this->supportAvx);
    }
    else
    {
        if(_actParams.size() > 0)
        {
            Activations::activateArray(layerOutput, this->_outputNum*this->_batch, this->_activation, this->supportAvx,_actParams[0]);
        }
        else
        {
            Activations::activateArray(layerOutput, this->_outputNum*this->_batch, this->_activation, this->supportAvx);
        }
    }

    this->_forwardTime =  TimeUtil::getElapsedTime(st);

}

void ConnectedLayer::mallocMemory()
{
    if(!this->_memoryMalloced)
    {
        if(!BaseLayer::isPreviewMode)
        {
            if(!BaseLayer::onlyUseGpu) 

            {

                this->_output   = MemoryManager::effcientNew<float>(static_cast<size_t>(this->_batch*this->_outputNum));
            }
#ifdef USE_GPU
            if(!BaseLayer::onlyUseCpu)

            {
                this->_gpuOutput     =   Cuda::mallocCudaArray(this->_totalBatch*this->_outputNum);
            }
#endif
            this->_memoryMalloced  =  true;
        }
    }
    this->_memReUse         =  0;
}

void ConnectedLayer::saveAllWeights(const int &mainIdx, const int &branchIdx, const int &branchIdx1)
{
    if(BaseLayer::isPreviewMode)
    {
        throw Exception(1,"connected preview mode can't save weights.", __FILE__, __LINE__, __FUNCTION__);
    }

    if(!this->_weightsLoaded)
    {
        throw Exception(1,"connected weights had not been loaded yet.", __FILE__, __LINE__, __FUNCTION__);
    }

    std::string name = "";

    if(branchIdx!=-1)
    {
        name = "_" + std::to_string(mainIdx) + "_" + std::to_string(branchIdx) + "_" + std::to_string(branchIdx1) +".txt";
    }
    else
    {
        name = std::to_string(this->_layerIndex)+".txt";
    }

#ifdef USE_GPU
    if(BaseLayer::onlyUseGpu) 

    {
        Cuda::pullCudaArray(this->_gpuWeights, this->_weights, this->_nWeights);

        if(this->_useBias)
        {
            Cuda::pullCudaArray(this->_gpuBiases, this->_biases, this->_nBiases);
        }

        if(this->_batchNorm)
        {
            Cuda::pullCudaArray(this->_gpuScales, this->_scales, this->_nScales);
            Cuda::pullCudaArray(this->_gpuRollMean, this->_rollMean, this->_nRollMean);
            Cuda::pullCudaArray(this->_gpuRollVariance, this->_rollVariance, this->_nRollVariance);
        }

        if(this->_activation == ActivationType::PRELU) 

        {
            Cuda::pullCudaArray(this->_gpuPreluWeights, this->_preluWeights, this->_nPreluWeights);
        }
    }
#endif

    if(this->_weights==nullptr)
    {
        throw Exception(1,"connected weights err.", __FILE__, __LINE__, __FUNCTION__);
    }
    std::vector<float> weightsVec(this->_weights,this->_weights+this->_nWeights);
    std::string weightsName = "weights"+name;
    Msnhnet::IO::saveVector<float>(weightsVec,weightsName.c_str(),"\n");

    if(this->_useBias)
    {
        if(this->_biases==nullptr)
        {
            throw Exception(1,"connected biases err.", __FILE__, __LINE__, __FUNCTION__);
        }
        std::vector<float> biasesVec(this->_biases,this->_biases+this->_nBiases);
        std::string biasName = "bias"+name;
        Msnhnet::IO::saveVector<float>(biasesVec,biasName.c_str(),"\n");
    }

    if(this->_batchNorm)
    {
        if(this->_scales==nullptr || this->_rollMean==nullptr || this->_rollVariance==nullptr)
        {
            throw Exception(1,"connected weights err.", __FILE__, __LINE__, __FUNCTION__);
        }

        std::vector<float> scalesVec(this->_scales,this->_scales+this->_nScales);
        std::vector<float> rollMeanVec(this->_rollMean,this->_rollMean+this->_nRollMean);
        std::vector<float> rollVarianceVec(this->_rollVariance,this->_rollVariance+this->_nRollVariance);

        std::string scaleName = "scale"+name;
        Msnhnet::IO::saveVector<float>(scalesVec,scaleName.c_str(),"\n");
        std::string rollMeanName = "rollMean"+name;
        Msnhnet::IO::saveVector<float>(rollMeanVec,rollMeanName.c_str(),"\n");
        std::string rollVarianceName = "rollVariance"+name;
        Msnhnet::IO::saveVector<float>(rollVarianceVec,rollVarianceName.c_str(),"\n");
    }

    if(this->_activation==ActivationType::PRELU) 

    {
        if(this->_preluWeights==nullptr)
        {
            throw Exception(1,"connected weights err.", __FILE__, __LINE__, __FUNCTION__);
        }
        std::vector<float> preluWeightsVec(this->_preluWeights,this->_preluWeights+this->_nPreluWeights);
        std::string preluWeightsName = "preluWeights"+name;
        Msnhnet::IO::saveVector<float>(preluWeightsVec,preluWeightsName.c_str(),"\n");
    }
}
#ifdef USE_GPU
void ConnectedLayer::forwardGPU(NetworkState &netState)
{
    this->recordCudaStart();

    float* layerGpuInput   = netState.getGpuInput();
    float* layerGpuOutput  = nullptr;

    /* 输入 */
    if(this->_isBranchLayer) 

    {
        if(this->_isFirstBranch)

        {
            layerGpuInput      = netState.input;
        }
    }
    else
    {
        if(this->_layerIndex == 0) 

        {
            layerGpuInput      = netState.input;
        }
        else 

        {
            if(netState.net->layers[this->_layerIndex - 1]->getMemReUse() == 0)

            {
                layerGpuInput  = netState.input;
            }
        }
    }

    /* 输出 */
    if(this->_isBranchLayer) 

    {
        if(this->_isLastBranch)

        {
            layerGpuOutput     = this->_gpuOutput; 

        }
        else 

        {
            layerGpuOutput     = netState.getGpuOutput(); 

            netState.shuffleGpuInOut();

        }
    }
    else
    {
        if(this->_memReUse==1) 

        {
            layerGpuOutput     = netState.getGpuOutput(); 

            netState.shuffleGpuInOut();

        }
        else

        {
            layerGpuOutput     = this->_gpuOutput;
        }
    }

    BlasGPU::gpuFill(this->_outputNum*this->_batch, 0, layerGpuOutput, 1);

    int m       =   this->_batch;
    int k       =   this->_inputNum;
    int n       =   this->_outputNum;

    float *a    =   layerGpuInput;
    float *b    =   this->_gpuWeights;
    float *c    =   layerGpuOutput;

    GemmGPU::gpuGemm(0,1,m,n,k,1,a,k,b,k,1,c,n);

    if(this->_batchNorm)
    {

        ConvolutionalLayerGPU::convBn(this->_batch, this->_outChannel, this->_outHeight, this->_outWidth, this->_gpuScales,
                                      this->_gpuRollMean, this->_gpuRollVariance, this->_gpuBiases, this->_bnEps, layerGpuOutput
                                      );
    }
    else
    {
        if(this->_useBias)
        {
            BlasGPU::gpuAddBias(layerGpuOutput, this->_gpuBiases, this->_batch, this->_outChannel, this->_outHeight*this->_outWidth);
        }
    }

    if(     this->_activation==ActivationType::NORM_CHAN||
            this->_activation==ActivationType::NORM_CHAN_SOFTMAX||
            this->_activation==ActivationType::NORM_CHAN_SOFTMAX_MAXVAL||
            this->_activation==ActivationType::NONE)
    {
        this->recordCudaStop();
        return;
    }
    else if(this->_activation == ActivationType::PRELU) 

    {
        ActivationsGPU::gpuActivatePRelu(layerGpuOutput,this->_batch, this->_outChannel, this->_gpuPreluWeights, this->_outHeight*this->_outWidth);
    }
    else
    {
        if(_actParams.size() > 0)
        {
            ActivationsGPU::gpuActivateArray(layerGpuOutput, this->_outputNum*this->_batch, this->_activation, _actParams[0]);
        }
        else
        {
            ActivationsGPU::gpuActivateArray(layerGpuOutput, this->_outputNum*this->_batch, this->_activation);
        }
    }

    this->recordCudaStop();

}
#endif

#ifdef USE_OPENCL 

void ConnectedLayer::forwardCL(NetworkState &netState) {
    std::cout << "connected layer fowardCL" << std::endl;

    float* layerInput   = netState.getInput();
    float* layerOutput  = nullptr;

    /* 输入 */
    if(this->_isBranchLayer) 
    {
        if(this->_isFirstBranch)
        {
            layerInput      = netState.input;
        }
    }
    else
    {
        if(this->_layerIndex == 0) 
        {
            layerInput      = netState.input;
        }
        else 
        {
            if(netState.net->layers[this->_layerIndex - 1]->getMemReUse() == 0)
            {
                layerInput  = netState.input;
            }
        }
    }

    /* 输出 */
    if(this->_isBranchLayer) 
    {
        if(this->_isLastBranch)
        {
            layerOutput     = this->_output; 
        }
        else 
        {
            layerOutput     = netState.getOutput(); 
            netState.shuffleInOut();
        }
    }
    else
    {
        if(this->_memReUse==1) 
        {
            layerOutput     = netState.getOutput(); 
            netState.shuffleInOut();
        }
        else
        {
            layerOutput     = this->_output;
        }
    }


    int m       =   this->_batch;
    int k       =   this->_inputNum;
    int n       =   this->_outputNum;
    cl_mem inputMem = clCreateBuffer(clScheduler::get().context(),  CL_MEM_READ_WRITE | CL_MEM_COPY_HOST_PTR, this->_batch * this->_inputNum * sizeof(float), layerInput, &status);
    cl_mem dstMem = clCreateBuffer(clScheduler::get().context(), CL_MEM_WRITE_ONLY, m * n * sizeof(float), NULL, &status);
    CHECKSTATUS(status, "create dst cl_mem");
    CHECKSTATUS(status, "create im_im2col");

    ConnectedCL::connectedCL(inputMem, m, n, k, this->_kernel_con, this->_clWeights, dstMem);
    status |= clEnqueueReadBuffer(clScheduler::get().queue(), dstMem, CL_TRUE, 0, m * n * sizeof(float), layerOutput, 0, NULL, NULL);

    std::cout << "m = " << m << "   \tn = " << n << "   \tk = " << k << std::endl;


    

    if(this->_batchNorm == 1)
    {
        std::cout << "do batchnorm, waiting for complete..." << std::endl;
    } else {
        if(this->_useBias)
        {
            for (int i = 0; i < this->_batch; ++i)
            {
                Blas::cpuAxpy(this->_outputNum, 1, this->_biases, 1, layerOutput + i * this->_outputNum, 1);
            }
        }

        
    }


    if(this->_activation == ActivationType::NORM_CHAN)
    {
        Activations::activateArrayNormCh(layerOutput, this->_outputNum*this->_batch, this->_batch, this->_outChannel,
                                         this->_outWidth*this->_outHeight, layerOutput);
    }
    else if(this->_activation == ActivationType::NORM_CHAN_SOFTMAX)
    {
        Activations::activateArrayNormChSoftMax(layerOutput, this->_outputNum*this->_batch, this->_batch, this->_outChannel,
                                                this->_outWidth*this->_outHeight, layerOutput,0);
    }
    else if(this->_activation == ActivationType::NORM_CHAN_SOFTMAX_MAXVAL)
    {
        Activations::activateArrayNormChSoftMax(layerOutput, this->_outputNum*this->_batch, this->_batch, this->_outChannel,
                                                this->_outWidth*this->_outHeight, layerOutput,1);
    }
    else if(this->_activation == ActivationType::PRELU)

    {
        Activations::activatePRelu(layerOutput,this->_batch, this->_outChannel,this->_preluWeights, this->_outWidth*this->_outHeight, this->supportAvx);
    }
    else if(this->_activation == ActivationType::NONE)
    {

    }
    else
    {
        if(_actParams.size() > 0)
        {
            ActivationsCL::activateArrayCL(layerOutput, this->_outputNum*this->_batch, this->_kernel_act, _actParams[0]);
        }
        else
        {
            ActivationsCL::activateArrayCL(layerOutput, this->_outputNum*this->_batch, this->_kernel_act);
        }
    }

}

#endif 

void ConnectedLayer::loadAllWeigths(std::vector<float> &weights)
{

    if(weights.size() != this->_numWeights)
    {
        throw Exception(1,"Connect weights load err. needed : " + std::to_string(this->_numWeights) + " given : " +  std::to_string(weights.size()), __FILE__, __LINE__, __FUNCTION__);
    }
    if(!BaseLayer::isPreviewMode)
    {
        int offset = 0;

        this->_weights   = MemoryManager::effcientNew<float>(static_cast<size_t>(this->_nWeights));

        loadWeights(weights.data(), _nWeights);
        offset += _nWeights;

#ifdef USE_OPENCL
        this->_clWeights           = clCreateBuffer(clScheduler::get().context(),  CL_MEM_READ_WRITE | CL_MEM_ALLOC_HOST_PTR, sizeof(float) * this->_nWeights, NULL, &status);
        if (status != CL_SUCCESS) { std::cout << "create clWeights failed" << std::endl; }
        float* clWeightsPtr = (float*)clEnqueueMapBuffer(clScheduler::get().queue(), this->_clWeights, CL_TRUE, CL_MAP_WRITE, 0, sizeof(float) * this->_nWeights, 0, NULL, NULL, &status);
        if (status != CL_SUCCESS) { std::cout << "map clWeights failed" << std::endl; }
        
        for (size_t i = 0; i < this->_nWeights; i++){
            clWeightsPtr[i] = weights[i];
        }
        status = clEnqueueUnmapMemObject(clScheduler::get().queue(), this->_clWeights, clWeightsPtr, 0, NULL, NULL);
        if (status != CL_SUCCESS) { std::cout << "unmap clWeights failed" << std::endl; }

#endif 

        if(this->_batchNorm)
        {

            this->_scales        =    MemoryManager::effcientNew<float>(static_cast<size_t>(this->_nScales));
            this->_rollMean      =    MemoryManager::effcientNew<float>(static_cast<size_t>(this->_nRollMean));
            this->_rollVariance  =    MemoryManager::effcientNew<float>(static_cast<size_t>(this->_nRollVariance));
            this->_biases        =    MemoryManager::effcientNew<float>(static_cast<size_t>(this->_nBiases));

            loadScales(weights.data() + this->_nWeights, this->_nScales);
            loadRollMean(weights.data() + this->_nWeights + this->_nScales, this->_nRollMean);
            loadRollVariance(weights.data() + this->_nWeights + this->_nScales + this->_nRollMean, this->_nRollVariance);
            loadBias(weights.data() + this->_nWeights + this->_nScales + this->_nRollMean + this->_nRollVariance, this->_nBiases);

            offset = offset + this->_nScales + this->_nRollMean + this->_nRollVariance + this->_nBiases;
        }
        else
        {
            if(this->_useBias)
            {

                this->_biases        =    MemoryManager::effcientNew<float>(static_cast<size_t>(this->_nBiases));

                loadBias(weights.data() + this->_nWeights, this->_nBiases);

                offset = offset + this->_nBiases;
            }
        }

        if(this->_activation == ActivationType::PRELU) 

        {

            this->_preluWeights       = MemoryManager::effcientNew<float>(static_cast<size_t>(this->_nPreluWeights));
            loadPreluWeights(weights.data() + offset, this->_nPreluWeights);
        }

#ifdef USE_GPU
        if(!BaseLayer::onlyUseCpu)
        {
            this->_gpuWeights    =   Cuda::makeCudaArray(this->_weights, this->_nWeights);
            if(this->_batchNorm)
            {
                this->_gpuScales        =   Cuda::makeCudaArray(this->_scales, this->_nScales);
                this->_gpuRollMean      =   Cuda::makeCudaArray(this->_rollMean, this->_nRollMean);
                this->_gpuRollVariance  =   Cuda::makeCudaArray(this->_rollVariance, this->_nRollVariance);
                this->_gpuBiases        =   Cuda::makeCudaArray(this->_biases, this->_nBiases);
            }
            else
            {
                this->_gpuBiases        =   Cuda::makeCudaArray(this->_biases, this->_nBiases);
            }

            if(this->_activation == ActivationType::PRELU) 

            {
                this->_gpuPreluWeights = Cuda::makeCudaArray(this->_preluWeights, this->_nPreluWeights);
            }
        }

        if(BaseLayer::onlyUseGpu)
        {
            releaseArr(this->_weights     );
            releaseArr(this->_scales      );
            releaseArr(this->_rollMean    );
            releaseArr(this->_rollVariance);
            releaseArr(this->_biases      );
            if(this->_activation == ActivationType::PRELU) 

            {
                releaseArr(this->_preluWeights);
            }
        }
#endif
    }

    this->_weightsLoaded = true;
}

void ConnectedLayer::loadScales(float * const &weights, const int &len)
{
    if(len != this->_nScales)
    {
        throw Exception(1, "load scales data len error ",__FILE__,__LINE__, __FUNCTION__);
    }
    Blas::cpuCopy(len, weights, 1, this->_scales,1);
}

void ConnectedLayer::loadBias(float * const &bias, const int &len)
{
    if(len != this->_nBiases)
    {
        throw Exception(1, "load bias data len error ",__FILE__,__LINE__, __FUNCTION__);
    }
    Blas::cpuCopy(len, bias, 1, this->_biases,1);
}

void ConnectedLayer::loadWeights(float * const &weights, const int &len)
{
    if(len != this->_nWeights)
    {
        throw Exception(1, "load weights data len error ",__FILE__,__LINE__, __FUNCTION__);
    }
    Blas::cpuCopy(len, weights, 1, this->_weights,1);
}

void ConnectedLayer::loadRollMean(float * const &rollMean, const int &len)
{
    if(len != this->_nRollMean)
    {
        throw Exception(1, "load roll mean data len error ",__FILE__,__LINE__, __FUNCTION__);
    }
    Blas::cpuCopy(len, rollMean, 1, this->_rollMean,1);
}

void ConnectedLayer::loadRollVariance(float * const &rollVariance, const int &len)
{
    if(len != this->_nRollVariance)
    {
        throw Exception(1, "load roll variance data len error ",__FILE__,__LINE__, __FUNCTION__);
    }
    Blas::cpuCopy(len, rollVariance, 1, this->_rollVariance,1);
}

void ConnectedLayer::loadPreluWeights(float * const &weights, const int &len)
{
    if(len != this->_nPreluWeights)
    {
        throw Exception(1, "load preluWeights data len error",__FILE__,__LINE__, __FUNCTION__);
    }
    Blas::cpuCopy(len, weights, 1, this->_preluWeights,1);
}

float *ConnectedLayer::getWeights() const
{
    return _weights;
}

float *ConnectedLayer::getBiases() const
{
    return _biases;
}

float *ConnectedLayer::getScales() const
{
    return _scales;
}

float *ConnectedLayer::getRollMean() const
{
    return _rollMean;
}

float *ConnectedLayer::getRollVariance() const
{
    return _rollVariance;
}

int ConnectedLayer::getNBiases() const
{
    return _nBiases;
}

int ConnectedLayer::getNWeights() const
{
    return _nWeights;
}

int ConnectedLayer::getNScales() const
{
    return _nScales;
}

int ConnectedLayer::getNRollMean() const
{
    return _nRollMean;
}

int ConnectedLayer::getNRollVariance() const
{
    return _nRollVariance;
}

int ConnectedLayer::getKSize() const
{
    return _kSize;
}

int ConnectedLayer::getStride() const
{
    return _stride;
}

int ConnectedLayer::getStrideX() const
{
    return _strideX;
}

int ConnectedLayer::getStrideY() const
{
    return _strideY;
}

int ConnectedLayer::getPadding() const
{
    return _padding;
}

int ConnectedLayer::getDilation() const
{
    return _dilation;
}

int ConnectedLayer::getBatchNorm() const
{
    return _batchNorm;
}
}
