/* Pco.h
 *
 * Revamped PCO area detector driver.
 *
 * Author:  Giles Knap
 *          Jonathan Thompson
 *
 */
#include "Pco.h"
#include "DllApi.h"
#include <cstdio>
#include <cstring>
#include <cstdarg>
#include "epicsExport.h"
#include "epicsThread.h"
#include "iocsh.h"
#include "db_access.h"
#include <iostream>
#include "GangServer.h"
#include "GangConnection.h"
#include "TakeLock.h"
#include "FreeLock.h"

/** Constants
 */
const int Pco::traceFlagsDllApi = 0x0100;
const int Pco::traceFlagsGang = 0x0400;
const int Pco::traceFlagsPcoState = 0x0200;
const int Pco::requestQueueCapacity = 10;
const int Pco::numHandles = 300;
const double Pco::reconnectPeriod = 5.0;
const double Pco::rebootPeriod = 10.0;
const double Pco::connectPeriod = 5.0;
const double Pco::statusPollPeriod = 2.0;
const double Pco::acquisitionStatusPollPeriod = 5.0;
const int Pco::bitsPerShortWord = 16;
const int Pco::bitsPerNybble = 4;
const long Pco::nybbleMask = 0x0f;
const long Pco::bcdDigitValue = 10;
const int Pco::bcdPixelLength = 4;
const int Pco::defaultHorzBin = 1;
const int Pco::defaultVertBin = 1;
const int Pco::defaultRoiMinX = 1;
const int Pco::defaultRoiMinY = 1;
const int Pco::defaultExposureTime = 50;
const int Pco::defaultDelayTime = 0;
const int Pco::edgeXSizeNeedsReducedCamlink = 1920;
const int Pco::edgePixRateNeedsReducedCamlink = 286000000;
const int Pco::edgeBaudRate = 115200;
const double Pco::timebaseNanosecondsThreshold = 0.001;
const double Pco::timebaseMicrosecondsThreshold = 1.0;
const double Pco::oneNanosecond = 1e-9;
const double Pco::oneMillisecond = 1e-3;
const double Pco::triggerRetryPeriod = 0.01;
const int Pco::statusMessageSize = 256;


/** The PCO object map
 */
std::map<std::string, Pco*> Pco::thePcos;

/**
 * Constructor
 * \param[in] portName ASYN Port name
 * \param[in] maxSizeX frame size width
 * \param[in] maxSizeY frame size height
 * \param[in] maxBuffers The maximum number of NDArray buffers that the NDArrayPool for this driver is
 *            allowed to allocate. Set this to -1 to allow an unlimited number of buffers.
 * \param[in] maxMemory The maximum amount of memory that the NDArrayPool for this driver is
 *            allowed to allocate. Set this to -1 to allow an unlimited amount of memory.
 */
Pco::Pco(const char* portName, int maxBuffers, size_t maxMemory)
: ADDriverEx(portName, 1, maxBuffers, maxMemory)
, paramPixRate(this, "PCO_PIX_RATE", 0)
, paramAdcMode(this, "PCO_ADC_MODE", 2)
, paramCamRamUse(this, "PCO_CAM_RAM_USE", 0)
, paramElectronicsTemp(this, "PCO_ELECTRONICS_TEMP", 0.0)
, paramPowerTemp(this, "PCO_POWER_TEMP", 0.0)
, paramStorageMode(this, "PCO_STORAGE_MODE", 0)
, paramRecorderSubmode(this, "PCO_RECORDER_SUBMODE", 0)
, paramTimestampMode(this, "PCO_TIMESTAMP_MODE", 2)
, paramAcquireMode(this, "PCO_ACQUIRE_MODE", 0)
, paramDelayTime(this, "PCO_DELAY_TIME", 0.0)
, paramArmMode(this, "PCO_ARM_MODE", 0, new AsynParam::Notify<Pco>(this, &Pco::onArmMode))
, paramImageNumber(this, "PCO_IMAGE_NUMBER", 0)
, paramCameraSetup(this, "PCO_CAMERA_SETUP", 1)
, paramBitAlignment(this, "PCO_BIT_ALIGNMENT", 1)
, paramStateRecord(this, "PCO_STATERECORD", "")
, paramClearStateRecord(this, "PCO_CLEARSTATERECORD", 0, new AsynParam::Notify<Pco>(this, &Pco::onClearStateRecord))
, paramOutOfNDArrays(this, "PCO_OUTOFNDARRAYS", 0)
, paramBufferQueueReadFailures(this, "PCO_BUFFERQUEUEREADFAILURES", 0)
, paramBuffersWithNoData(this, "PCO_BUFFERSWITHNODATA", 0)
, paramMisplacedBuffers(this, "PCO_MISPLACEDBUFFERS", 0)
, paramMissingFrames(this, "PCO_MISSINGFRAMES", 0)
, paramDriverLibraryErrors(this, "PCO_DRIVERLIBRARYERRORS", 0)
, paramHwBinX(this, "PCO_HWBINX", 0)
, paramHwBinY(this, "PCO_HWBINY", 0)
, paramHwRoiX1(this, "PCO_HWROIX1", 0)
, paramHwRoiY1(this, "PCO_HWROIY1", 0)
, paramHwRoiX2(this, "PCO_HWROIX2", 0)
, paramHwRoiY2(this, "PCO_HWROIY2", 0)
, paramXCamSize(this, "PCO_XCAMSIZE", 1280)
, paramYCamSize(this, "PCO_YCAMSIZE", 1024)
, paramCamlinkClock(this, "PCO_CAMLINKCLOCK", 0)
, paramMinCoolingSetpoint(this, "PCO_MINCOOLINGSETPOINT", 0)
, paramMaxCoolingSetpoint(this, "PCO_MAXCOOLINGSETPOINT", 0)
, paramDefaultCoolingSetpoint(this, "PCO_DEFAULTCOOLINGSETPOINT", 0)
, paramCoolingSetpoint(this, "PCO_COOLINGSETPOINT", 0, new AsynParam::Notify<Pco>(this, &Pco::onCoolingSetpoint))
, paramDelayTimeMin(this, "PCO_DELAYTIMEMIN", 0.0)
, paramDelayTimeMax(this, "PCO_DELAYTIMEMAX", 0.0)
, paramDelayTimeStep(this, "PCO_DELAYTIMESTEP", 0.0)
, paramExpTimeMin(this, "PCO_EXPTIMEMIN", 0.0)
, paramExpTimeMax(this, "PCO_EXPTIMEMAX", 0.0)
, paramExpTimeStep(this, "PCO_EXPTIMESTEP", 0.0)
, paramMaxBinHorz(this, "PCO_MAXBINHORZ", 0)
, paramMaxBinVert(this, "PCO_MAXBINVERT", 0)
, paramBinHorzStepping(this, "PCO_BINHORZSTEPPING", 0)
, paramBinVertStepping(this, "PCO_BINVERTSTEPPING", 0)
, paramRoiHorzSteps(this, "PCO_ROIHORZSTEPS", 0)
, paramRoiVertSteps(this, "PCO_ROIVERTSTEPS", 0)
, paramReboot(this, "PCO_REBOOT", 1, new AsynParam::Notify<Pco>(this, &Pco::onReboot))
, paramCamlinkLongGap(this, "PCO_CAMLINKLONGGAP", 1)
, paramArm(this, "PCO_ARM", 0, new AsynParam::Notify<Pco>(this, &Pco::onArm))
, paramDisarm(this, "PCO_DISARM", 0, new AsynParam::Notify<Pco>(this, &Pco::onDisarm))
, paramGangMode(this, "PCO_GANGMODE", gangModeNone)
, paramADAcquire(ADDriverEx::paramADAcquire, new AsynParam::Notify<Pco>(this, &Pco::onAcquire))
, paramADTemperature(ADDriverEx::paramADTemperature, new AsynParam::Notify<Pco>(this, &Pco::onADTemperature))
, stateMachine(NULL)
, triggerTimer(NULL)
, api(NULL)
, errorTrace(getAsynUser(), ASYN_TRACE_ERROR)
, apiTrace(getAsynUser(), Pco::traceFlagsDllApi)
, gangTrace(getAsynUser(), Pco::traceFlagsGang)
, stateTrace(getAsynUser(), Pco::traceFlagsPcoState)
, receivedFrameQueue(maxBuffers, sizeof(NDArray*))
, gangServer(NULL)
, gangConnection(NULL)
{
    // Put in global map
    Pco::thePcos[portName] = this;

    // Initialise some base class parameters
    paramNDDataType = NDUInt16;
    paramADNumExposures = 1;
    paramADManufacturer = "PCO";
    paramADModel = "Unknown";
    paramADMaxSizeX = 0;
    paramADMaxSizeY = 1024;
    paramNDArraySize = 0;
    // We are not connected to a camera
    camera = NULL;
    // Initialise the buffers
    for(int i=0; i<Pco::numApiBuffers; i++)
    {
        buffers[i].bufferNumber = DllApi::bufferUnallocated;
        buffers[i].buffer = NULL;
        buffers[i].eventHandle = NULL;
        buffers[i].ready = false;
    }
    // Initialise the enum strings
    for(int i=0; i<DllApi::descriptionNumPixelRates; i++)
    {
        pixRateEnumValues[i] = 0;
        pixRateEnumStrings[i] = (char *)calloc(MAX_ENUM_STRING_SIZE, sizeof(char));
        pixRateEnumSeverities[i] = 0;
    }
    // Create the state machine
    stateMachine = new StateMachine("Pco", this,
            &paramStateRecord, &stateTrace, Pco::requestQueueCapacity);
    // States
	stateUninitialised = stateMachine->state("Uninitialised");
	stateUnconnected = stateMachine->state("Unconnected");
	stateIdle = stateMachine->state("Idle");
    stateArmed = stateMachine->state("Armed");
    stateAcquiring = stateMachine->state("Acquiring");
	statedUnarmedAcquiring = stateMachine->state("UnarmedAcquiring");
	stateExternalAcquiring = stateMachine->state("ExternalAcquiring");
	// Events
    requestInitialise = stateMachine->event("Initialise");
	requestTimerExpiry = stateMachine->event("TimerExpiry");
	requestAcquire = stateMachine->event("Acquire");
	requestStop = stateMachine->event("Stop");
	requestArm = stateMachine->event("Arm");
	requestImageReceived = stateMachine->event("ImageReceived");
	requestDisarm = stateMachine->event("Disarm");
	requestTrigger = stateMachine->event("Trigger");
	requestReboot = stateMachine->event("Reboot");
	requestMakeImages = stateMachine->event("MakeImages");
	// Transitions
	stateMachine->transition(stateUninitialised, requestInitialise, new StateMachine::Act<Pco>(this, &Pco::smInitialiseWait), stateUnconnected);
	stateMachine->transition(stateUnconnected, requestTimerExpiry, new StateMachine::Act<Pco>(this, &Pco::smConnectToCamera), stateIdle, stateUnconnected);
	stateMachine->transition(stateIdle, requestTimerExpiry, new StateMachine::Act<Pco>(this, &Pco::smPollWhileIdle), stateIdle);
	stateMachine->transition(stateIdle, requestArm, new StateMachine::Act<Pco>(this, &Pco::smRequestArm), stateArmed, stateIdle);
	stateMachine->transition(stateIdle, requestAcquire, new StateMachine::Act<Pco>(this, &Pco::smArmAndAcquire), statedUnarmedAcquiring, stateIdle);
	stateMachine->transition(stateIdle, requestImageReceived, new StateMachine::Act<Pco>(this, &Pco::smDiscardImages), stateIdle);
	stateMachine->transition(stateIdle, requestReboot, new StateMachine::Act<Pco>(this, &Pco::smRequestReboot), stateUnconnected);
	stateMachine->transition(stateArmed, requestTimerExpiry, new StateMachine::Act<Pco>(this, &Pco::smPollWhileAcquiring), stateArmed);
	stateMachine->transition(stateArmed, requestAcquire, new StateMachine::Act<Pco>(this, &Pco::smAcquire), stateAcquiring);
	stateMachine->transition(stateArmed, requestImageReceived, new StateMachine::Act<Pco>(this, &Pco::smFirstImageWhileArmed), stateExternalAcquiring, stateIdle, stateArmed, stateArmed);
	stateMachine->transition(stateArmed, requestDisarm, new StateMachine::Act<Pco>(this, &Pco::smDisarmAndDiscard), stateIdle);
	stateMachine->transition(stateArmed, requestStop, new StateMachine::Act<Pco>(this, &Pco::smDisarmAndDiscard), stateIdle);
	stateMachine->transition(stateAcquiring, requestTimerExpiry, new StateMachine::Act<Pco>(this, &Pco::smPollWhileAcquiring), stateAcquiring);
	stateMachine->transition(stateAcquiring, requestImageReceived, new StateMachine::Act<Pco>(this, &Pco::smAcquireImage), stateAcquiring, stateIdle, stateArmed);
	stateMachine->transition(stateAcquiring, requestMakeImages, new StateMachine::Act<Pco>(this, &Pco::smMakeGangedImage), stateAcquiring, stateIdle, stateArmed);
	stateMachine->transition(stateAcquiring, requestTrigger, new StateMachine::Act<Pco>(this, &Pco::smTrigger), stateAcquiring);
	stateMachine->transition(stateAcquiring, requestStop, new StateMachine::Act<Pco>(this, &Pco::smStopAcquisition), stateIdle, stateArmed);
	stateMachine->transition(stateExternalAcquiring, requestTimerExpiry, new StateMachine::Act<Pco>(this, &Pco::smPollWhileAcquiring), stateExternalAcquiring);
	stateMachine->transition(stateExternalAcquiring, requestImageReceived, new StateMachine::Act<Pco>(this, &Pco::smExternalAcquireImage), stateExternalAcquiring, stateIdle, stateArmed);
	stateMachine->transition(stateExternalAcquiring, requestMakeImages, new StateMachine::Act<Pco>(this, &Pco::smMakeGangedImage), stateExternalAcquiring, stateIdle, stateArmed);
	stateMachine->transition(stateExternalAcquiring, requestStop, new StateMachine::Act<Pco>(this, &Pco::smExternalStopAcquisition), stateIdle);
	stateMachine->transition(statedUnarmedAcquiring, requestTimerExpiry, new StateMachine::Act<Pco>(this, &Pco::smPollWhileAcquiring), statedUnarmedAcquiring);
	stateMachine->transition(statedUnarmedAcquiring, requestImageReceived, new StateMachine::Act<Pco>(this, &Pco::smUnarmedAcquireImage), statedUnarmedAcquiring, stateIdle);
	stateMachine->transition(statedUnarmedAcquiring, requestMakeImages, new StateMachine::Act<Pco>(this, &Pco::smUnarmedMakeGangedImage), statedUnarmedAcquiring, stateIdle);
	stateMachine->transition(statedUnarmedAcquiring, requestTrigger, new StateMachine::Act<Pco>(this, &Pco::smTrigger), statedUnarmedAcquiring);
	stateMachine->transition(statedUnarmedAcquiring, requestStop, new StateMachine::Act<Pco>(this, &Pco::smExternalStopAcquisition), stateIdle);
	// State machine starting state
	stateMachine->initialState(stateUninitialised);
	// A timer for the trigger
    triggerTimer = new StateMachine::Timer(stateMachine);
}

/**
 * Destructor
 */
Pco::~Pco()
{
    try
    {
        api->setRecordingState(this->camera, DllApi::recorderStateOff);
        api->cancelImages(this->camera);
        for(int i=0; i<Pco::numApiBuffers; i++)
        {
            api->freeBuffer(camera, i);
        }
        api->closeCamera(camera);
    }
    catch(PcoException&)
    {
    }
    for(int i=0; i<Pco::numApiBuffers; i++)
    {
        if(buffers[i].buffer != NULL)
        {
            delete[] buffers[i].buffer;
        }
    }
    delete triggerTimer;
    delete stateMachine;
}

/**
 * Connects the DLL API to the main PCO class.  This call triggers
 * the initialisation of the camera.
 */
void Pco::registerDllApi(DllApi* api)
{
    this->api = api;
    post(requestInitialise);
}

/**
 * Return the pco corresponding to the port name
 * \param[in] p The port name
 * \return The pco object, NULL if not found
 */
Pco* Pco::getPco(const char* portName)
{
    Pco* result = NULL;
    std::map<std::string, Pco*>::iterator pos = Pco::thePcos.find(portName);
    if(pos != Pco::thePcos.end())
    {
        result = pos->second;
    }
    return result;
}

/**
 * Reboot the camera
 */
void Pco::doReboot()
{
	api->setTimeouts(this->camera, 2000, 3000, 250);
	switch(this->camType)
	{
	case DllApi::cameraTypeEdge:
	case DllApi::cameraTypeEdgeGl:
		api->rebootCamera(this->camera);
		break;
	default:
		break;
	}
    api->closeCamera(this->camera);
    camera = NULL;
}

/**
 * Output a message to the status PV.
 * \param[in] text The message to output
 */
void Pco::outputStatusMessage(const char* text)
{
	TakeLock takeLock(this);
    paramADStatusMessage = text;
}

/**
 * Trigger the wait before we try to connect to the camera.
 * returns: firstState: always
 */
StateMachine::StateSelector Pco::smInitialiseWait()
{
    stateMachine->startTimer(Pco::connectPeriod, Pco::requestTimerExpiry);
    return StateMachine::firstState;
}

/**
 * Connect to the camera
 * returns: firstState: success
 *          secondState: failure
 */
StateMachine::StateSelector Pco::smConnectToCamera()
{
	StateMachine::StateSelector result;
	TakeLock takeLock(this);
    // Close the camera if we think it might be open
    if(camera != NULL)
    {
        try
        {
            api->closeCamera(camera);
        }
        catch(PcoException&)
        {
            // Swallow errors from this
        }
    }
    // Now try to open it again
    try
    {
        // Open and initialise the camera
        camera = NULL;
        api->openCamera(&camera, 0);
        initialiseCamera(takeLock);
        discardImages();
        stateMachine->startTimer(Pco::statusPollPeriod, Pco::requestTimerExpiry);
        result = StateMachine::firstState;
    }
    catch(PcoException&)
    {
        stateMachine->startTimer(Pco::reconnectPeriod, Pco::requestTimerExpiry);
        result = StateMachine::secondState;
    }
    return result;
}

/**
 * Poll the camera while it is not taking image.
 * returns: firstState: always
 */
StateMachine::StateSelector Pco::smPollWhileIdle()
{
    pollCameraNoAcquisition();
    pollCamera();
    stateMachine->startTimer(Pco::statusPollPeriod, Pco::requestTimerExpiry);
    return StateMachine::firstState;
}

/**
 * Poll the camera while it is taking images (or is armed).
 * returns: firstState: always
 */
StateMachine::StateSelector Pco::smPollWhileAcquiring()
{
    pollCamera();
    stateMachine->startTimer(Pco::acquisitionStatusPollPeriod, Pco::requestTimerExpiry);
    return StateMachine::firstState;
}

/**
 * Try to arm the camera
 * returns: firstState: success
 *          secondState: failure
 */
StateMachine::StateSelector Pco::smRequestArm()
{
	StateMachine::StateSelector result;
    try
    {
        doArm();
        stateMachine->startTimer(Pco::statusPollPeriod, Pco::requestTimerExpiry);
        outputStatusMessage("");
        result = StateMachine::firstState;
    }
    catch(std::bad_alloc& e)
    {
        acquisitionComplete();
        doDisarm();
        errorTrace << "Failed to arm due to out of memory, " << e.what() << std::endl;
        outputStatusMessage(e.what());
        result = StateMachine::secondState;
    }
    catch(PcoException& e)
    {
        acquisitionComplete();
        doDisarm();
        errorTrace << "Failed to arm due DLL error, " << e.what() << std::endl;
        outputStatusMessage(e.what());
        result = StateMachine::secondState;
    }
    return result;
}

/**
 * Arm the camera and start acquiring images
 * returns: firstState: success
 *          secondState: failure
 */
StateMachine::StateSelector Pco::smArmAndAcquire()
{
	StateMachine::StateSelector result;
    try
    {
        doArm();
        nowAcquiring();
        startCamera();
        stateMachine->startTimer(Pco::acquisitionStatusPollPeriod, Pco::requestTimerExpiry);
        outputStatusMessage("");
        result = StateMachine::firstState;
    }
    catch(std::bad_alloc& e)
    {
        acquisitionComplete();
        doDisarm();
        errorTrace << "Failed to arm due to out of memory, " << e.what() << std::endl;
        outputStatusMessage(e.what());
        result = StateMachine::secondState;
    }
    catch(PcoException& e)
    {
        acquisitionComplete();
        doDisarm();
        errorTrace << "Failed to arm due DLL error, " << e.what() << std::endl;
        outputStatusMessage(e.what());
        result = StateMachine::secondState;
    }
    return result;
}

/**
 * Start an already armed camera.
 * returns firstState: always
 */
StateMachine::StateSelector Pco::smAcquire()
{
    this->nowAcquiring();
    this->startCamera();
    this->stateMachine->startTimer(Pco::acquisitionStatusPollPeriod, Pco::requestTimerExpiry);
    return StateMachine::firstState;
}

/**
 * Discard all queued images
 * Returns: firstState: always
 */
StateMachine::StateSelector Pco::smDiscardImages()
{
    discardImages();
    return StateMachine::firstState;
}

/**
 * Start the reboot of a camera
 * Returns: firstState: always
 */
StateMachine::StateSelector Pco::smRequestReboot()
{
	// We need to stop the poll timer and discard any events that have
	// already been passed to the state machine
	stateMachine->stopTimer();
	stateMachine->clear();
	// Now do the reboot
	doReboot();
    stateMachine->startTimer(Pco::rebootPeriod, Pco::requestTimerExpiry);
    return StateMachine::firstState;
}

/**
 * Handle the first image received once the camera is armed.
 * Returns: firstState: further images to be acquired
 *          secondState: acquisition complete and disarmed
 *          thirdState: acquisition complete and still armed
 *          fourthState: image discarded and still armed
 */
StateMachine::StateSelector Pco::smFirstImageWhileArmed()
{
	StateMachine::StateSelector result;
    if(triggerMode != DllApi::triggerSoftware)
    {
        nowAcquiring();
        if(!receiveImages())
        {
        	result = StateMachine::firstState;
        }
        else if(triggerMode == DllApi::triggerAuto)
        {
            acquisitionComplete();
            doDisarm();
            result = StateMachine::secondState;
        }
        else
        {
            this->acquisitionComplete();
            result = StateMachine::thirdState;
        }
    }
    else
    {
        discardImages();
        result = StateMachine::fourthState;
    }
    return result;
}

/**
 * Handle an image during an acquisition.
 * Returns: firstState: further images to be acquired
 *          secondState: acquisition complete and disarmed
 *          thirdState: acquisition complete and still armed
 */
StateMachine::StateSelector Pco::smAcquireImage()
{
	StateMachine::StateSelector result;
    if(!receiveImages())
    {
        startCamera();
        result = StateMachine::firstState;
    }
    else if(triggerMode != DllApi::triggerSoftware)
    {
        acquisitionComplete();
        doDisarm();
        result = StateMachine::secondState;
    }
    else
    {
        acquisitionComplete();
        result = StateMachine::thirdState;
    }
    return result;
}

/**
 * Handle an image during an unarmed acquisition.
 * Returns: firstState: further images to be acquired
 *          secondState: acquisition complete and disarmed
 *          thirdState: acquisition complete and still armed
 */
StateMachine::StateSelector Pco::smUnarmedAcquireImage()
{
	StateMachine::StateSelector result;
    if(!receiveImages())
    {
        startCamera();
        result = StateMachine::firstState;
    }
    else
    {
        acquisitionComplete();
        doDisarm();
        discardImages();
        result = StateMachine::secondState;
    }
    return result;
}

/**
 * Handle an image during an externally triggered acquisition.
 * Returns: firstState: further images to be acquired
 *          secondState: acquisition complete and disarmed
 *          thirdState: acquisition complete and still armed
 */
StateMachine::StateSelector Pco::smExternalAcquireImage()
{
	StateMachine::StateSelector result;
    if(!receiveImages())
    {
        result = StateMachine::firstState;
    }
    else if(triggerMode == DllApi::triggerAuto)
    {
        acquisitionComplete();
        doDisarm();
        result = StateMachine::secondState;
    }
    else
    {
        acquisitionComplete();
        result = StateMachine::thirdState;
    }
    return result;
}

/**
 * Try and make stitched images in the full control ganged mode.
 * Returns: firstState: further images to be acquired
 *          secondState: acquisition complete and disarmed
 *          thirdState: acquisition complete and still armed
 */
StateMachine::StateSelector Pco::smMakeGangedImage()
{
	StateMachine::StateSelector result;
	if(!makeImages())
	{
        result = StateMachine::firstState;
	}
    else if(triggerMode != DllApi::triggerSoftware)
    {
        acquisitionComplete();
        doDisarm();
        result = StateMachine::secondState;
    }
    else
    {
        acquisitionComplete();
        result = StateMachine::thirdState;
    }
    return result;
}

/**
 * Try and make stiched images in the full control ganged mode during unarmed acquisition.
 * Returns: firstState: further images to be acquired
 *          secondState: acquisition complete and disarmed
 */
StateMachine::StateSelector Pco::smUnarmedMakeGangedImage()
{
	StateMachine::StateSelector result;
	if(!makeImages())
	{
        result = StateMachine::firstState;
	}
    else
    {
        acquisitionComplete();
        doDisarm();
        discardImages();
        result = StateMachine::secondState;
    }
    return result;
}

/**
 * Disarm the camera and discard any images in the queues.
 * Returns: firstState: always
 */
StateMachine::StateSelector Pco::smDisarmAndDiscard()
{
    doDisarm();
    discardImages();
    return StateMachine::firstState;
}

/**
 * Software trigger the camera
 * Returns: firstState: always
 */
StateMachine::StateSelector Pco::smTrigger()
{
    startCamera();
    return StateMachine::firstState;
}

/**
 * Stop the camera acquiring
 * Returns: firstState: camera is stopped and disarmed
 *          secondState: camera is stopped but still armed
 */
StateMachine::StateSelector Pco::smStopAcquisition()
{
	StateMachine::StateSelector result;
    if(triggerMode != DllApi::triggerSoftware)
    {
        acquisitionComplete();
        doDisarm();
        result = StateMachine::firstState;
    }
    else
    {
        acquisitionComplete();
        result = StateMachine::secondState;
    }
    return result;
}

/**
 * Stop the camera acquiring when triggered by an external trigger
 * Returns: firstState: always
 */
StateMachine::StateSelector Pco::smExternalStopAcquisition()
{
	acquisitionComplete();
	doDisarm();
	discardImages();
	return StateMachine::firstState;
}


/** Initialise the camera
 */
void Pco::initialiseCamera(TakeLock& takeLock)
{
	// Get various camera data
	api->getGeneral(camera);
	api->getCameraType(camera, &camType);
	api->getSensorStruct(camera);
	api->getCameraDescription(camera, &camDescription);
	api->getStorageStruct(camera, &camRamSize, &camPageSize);
	api->getRecordingStruct(camera);

	// Corrections for values that appear to be incorrectly returned by the SDK
	switch(this->camType)
	{
	case DllApi::cameraTypeDimaxStd:
	case DllApi::cameraTypeDimaxTv:
	case DllApi::cameraTypeDimaxAutomotive:
		camDescription.roiVertSteps = 4;
		break;
	default:
		break;
	}

	// reset the camera
	try
	{
		api->setRecordingState(camera, DllApi::recorderStateOff);
	}
	catch(PcoException&)
	{
		// Swallow errors from this
	}
	try
	{
		api->resetSettingsToDefault(camera);
	}
	catch(PcoException&)
	{
		// Swallow errors from this
	}

	// Record binning and roi capabilities
	paramMaxBinHorz = (int)camDescription.maxBinHorz;
	paramMaxBinVert = (int)camDescription.maxBinVert;
	paramBinHorzStepping = (int)camDescription.binHorzStepping;
	paramBinVertStepping = (int)camDescription.binVertStepping;
	paramRoiHorzSteps = (int)camDescription.roiHorSteps;
	paramRoiVertSteps = (int)camDescription.roiVertSteps;

	// Build the set of binning values
	setValidBinning(availBinX, camDescription.maxBinHorz,
			camDescription.binHorzStepping);
	setValidBinning(availBinY, camDescription.maxBinVert,
			camDescription.binVertStepping);

	// Get more camera information
	this->api->getTransferParameters(this->camera, &this->camTransfer);
	this->api->getSizes(this->camera, &this->camSizes);
	paramADMaxSizeX = (int)this->camSizes.xResActual;
	paramADMaxSizeY = (int)this->camSizes.yResActual;
	paramADSizeX = (int)this->camSizes.xResActual;
	paramADSizeY = (int)this->camSizes.yResActual;
	paramCamlinkClock = (int)this->camTransfer.clockFrequency;

	// Initialise the cooling setpoint information
	paramMinCoolingSetpoint = this->camDescription.minCoolingSetpoint;
	paramMaxCoolingSetpoint = this->camDescription.maxCoolingSetpoint;
	paramDefaultCoolingSetpoint = this->camDescription.defaultCoolingSetpoint;
	paramCoolingSetpoint = this->camDescription.defaultCoolingSetpoint;
	this->onCoolingSetpoint(takeLock);

	// Acquisition timing parameters
	paramDelayTimeMin = (double)this->camDescription.minDelayNs * 1e-9;
	paramDelayTimeMax = (double)this->camDescription.maxDelayMs * 1e-3;
	paramDelayTimeStep = (double)this->camDescription.minDelayStepNs * 1e-9;
	paramExpTimeMin = (double)this->camDescription.minExposureNs * 1e-9;
	paramExpTimeMax = (double)this->camDescription.maxExposureMs * 1e-3;
	paramExpTimeStep = (double)this->camDescription.minExposureStepNs * 1e-9;

	// Update area detector information strings
	switch(this->camType)
	{
	case DllApi::cameraType1200Hs:
		paramADModel = "PCO.Camera 1200";
		break;
	case DllApi::cameraType1300:
		paramADModel = "PCO.Camera 1300";
		break;
	case DllApi::cameraType1600:
		paramADModel = "PCO.Camera 1600";
		break;
	case DllApi::cameraType2000:
		paramADModel = "PCO.Camera 2000";
		break;
	case DllApi::cameraType4000:
		paramADModel = "PCO.Camera 4000";
		break;
	case DllApi::cameraTypeEdge:
	case DllApi::cameraTypeEdgeGl:
		paramADModel = "PCO.Camera Edge";
		break;
	case DllApi::cameraTypeDimaxStd:
	case DllApi::cameraTypeDimaxTv:
	case DllApi::cameraTypeDimaxAutomotive:
		paramADModel = "PCO.Camera Dimax";
		break;
	default:
		paramADModel = "PCO.Camera Unknown";
		break;
	}
	paramADManufacturer = "PCO";

	// Work out how to decode the BCD frame number in the image
	this->shiftLowBcd = Pco::bitsPerShortWord - this->camDescription.dynResolution;
	this->shiftHighBcd = this->shiftLowBcd + Pco::bitsPerNybble;

	// Set the camera clock
	this->setCameraClock();

	// Handle the pixel rates
	this->initialisePixelRate();

	// Make Edge specific function calls
	if(this->camType == DllApi::cameraTypeEdge || this->camType == DllApi::cameraTypeEdgeGl)
	{
		// Get Edge camera setup mode
		unsigned long setupData[DllApi::cameraSetupDataSize];
		unsigned short setupDataLen = DllApi::cameraSetupDataSize;
		unsigned short setupType;
		this->api->getCameraSetup(this->camera, &setupType, setupData, &setupDataLen);
		paramCameraSetup = setupData[0];
	}

	// Set the default binning
	this->api->setBinning(this->camera, Pco::defaultHorzBin, Pco::defaultVertBin);
	paramADBinX = Pco::defaultHorzBin;
	paramADBinY = Pco::defaultVertBin;

	// Set the default ROI (apparently a must do step)
	int roix1, roix2, roiy1, roiy2; // region of interest
	// to maximise in x dimension
	roix1 = Pco::defaultRoiMinX;
	roix2 = this->camDescription.maxHorzRes/Pco::defaultHorzBin/
			this->camDescription.roiHorSteps;
	roix2 *= this->camDescription.roiHorSteps;
	// to maximise in y dimension
	roiy1 = Pco::defaultRoiMinY;
	roiy2 = this->camDescription.maxVertRes/Pco::defaultVertBin/
			this->camDescription.roiVertSteps;
	roiy2 *= this->camDescription.roiVertSteps;
	this->api->setRoi(this->camera,
			(unsigned short)roix1, (unsigned short)roiy1,
			(unsigned short)roix2, (unsigned short)roiy2);
	paramADMinX = roix1-1;
	paramADMinY = roiy1-1;
	paramADSizeX = roix2-roix1+1;
	paramADSizeY = roiy2-roiy1+1;

	// Set initial trigger mode to auto
	this->api->setTriggerMode(this->camera, DllApi::triggerExternal);

	// Set the storage mode to FIFO
	this->api->setStorageMode(this->camera, DllApi::storageModeFifoBuffer);

	// Set our preferred time stamp mode.
	if((this->camDescription.generalCaps & DllApi::generalCapsNoTimestamp) != 0)
	{
		this->api->setTimestampMode(this->camera, DllApi::timestampModeOff);
	}
	else if((this->camDescription.generalCaps & DllApi::generalCapsTimestampAsciiOnly) != 0)
	{
		this->api->setTimestampMode(this->camera, DllApi::timestampModeAscii);
	}
	else
	{
		this->api->setTimestampMode(this->camera, DllApi::timestampModeBinaryAndAscii);
	}

	// Set the acquire mode.
	this->api->setAcquireMode(this->camera, DllApi::acquireModeAuto);
	paramAcquireMode = DllApi::acquireModeAuto;

	// Set the delay and exposure times
	this->api->setDelayExposureTime(this->camera,
			Pco::defaultDelayTime, Pco::defaultExposureTime,
			DllApi::timebaseMilliseconds, DllApi::timebaseMilliseconds);
	paramADAcquireTime = Pco::defaultExposureTime * Pco::oneMillisecond;

	// Set the gain
	if(this->camDescription.convFact > 0)
	{
		this->api->setConversionFactor(this->camera, this->camDescription.convFact);
		paramADGain = this->camDescription.convFact;
	}

	// Set the ADC mode for the cameras that support it
	if(this->camType == DllApi::cameraType1600 ||
			this->camType == DllApi::cameraType2000 ||
			this->camType == DllApi::cameraType4000)
	{
		this->api->setAdcOperation(this->camera, DllApi::adcModeSingle);
	}

	// Default data type
	paramNDDataType = NDUInt16;

	// Camera booted
	paramReboot = 0;

	// Lets have a look at the status of the camera
	unsigned short recordingState;
	this->api->getRecordingState(this->camera, &recordingState);

	// refresh everything
	this->pollCameraNoAcquisition();
	this->pollCamera();

	// Inform server if we have one
	if(gangConnection != NULL)
	{
		gangConnection->sendMemberConfig(takeLock);
	}
}

/**
 * Initialise the pixel rate information.
 * The various members are used as follows:
 *   camDescription.pixelRate[] contains the available pixel rates in Hz, zeroes
 *                              for unused locations.
 *   pixRateEnumValues[] contains indices into the camDescription.pixelRate[] array
 *                       for the mbbx PV values.
 *   pixRateEnumStrings[] contains the mbbx strings
 *   pixRateEnumSeverities[] contains the severity codes for the mbbx PV.
 *   pixRate contains the current setting in Hz.
 *   pixRateValue contains the mbbx value of the current setting
 *   pixRateMax contains the maximum available setting in Hz.
 *   pixRateMaxValue contains the mbbx value of the maximum setting.
 *   pixRateNumEnums is the number of valid rates
 */
void Pco::initialisePixelRate()
{
    // Get the current rate
    unsigned long r;
    this->api->getPixelRate(this->camera, &r);
    this->pixRate = (int)r;
    this->pixRateValue = 0;
    // Work out the information
    this->pixRateMax = 0;
    this->pixRateMaxValue = 0;
    this->pixRateNumEnums = 0;
    for(int i = 0; i<DllApi::descriptionNumPixelRates; i++)
    {
        if(this->camDescription.pixelRate[i] > 0)
        {
            epicsSnprintf(this->pixRateEnumStrings[this->pixRateNumEnums], MAX_ENUM_STRING_SIZE,
                "%ld Hz", this->camDescription.pixelRate[i]);
            this->pixRateEnumValues[this->pixRateNumEnums] = i;
            this->pixRateEnumSeverities[this->pixRateNumEnums] = 0;
            if((int)this->camDescription.pixelRate[i] > this->pixRateMax)
            {
                this->pixRateMax = this->camDescription.pixelRate[i];
                this->pixRateMaxValue = this->pixRateNumEnums;
            }
            this->pixRateNumEnums++;
            if((int)this->camDescription.pixelRate[i] == this->pixRate)
            {
            	this->pixRateValue = i;
            }

        }
    }
    // Give the enum strings to the PV
    this->doCallbacksEnum(this->pixRateEnumStrings, this->pixRateEnumValues, this->pixRateEnumSeverities,
        this->pixRateNumEnums, paramPixRate.getHandle(), 0);
    paramPixRate = this->pixRateValue;
}

/**
 * Populate a binning validity set
 */
void Pco::setValidBinning(std::set<int>& valid, int max, int step) throw()
{
    valid.clear();
    int bin = 1;
    while(bin <= max)
    {
        valid.insert(bin);
        if(step == DllApi::binSteppingLinear)
        {
            bin += 1;
        }
        else
        {
            bin *= 2;
        }
    }
}

/**
 * Poll the camera for status information.  This function may only be called
 * while the camera is not acquiring.
 */
bool Pco::pollCameraNoAcquisition()
{
    bool result = true;
    try
    {
        unsigned short storageMode;
        unsigned short recorderSubmode;
        this->api->getStorageMode(this->camera, &storageMode);
        this->api->getRecorderSubmode(this->camera, &recorderSubmode);
        TakeLock takeLock(this);
        paramStorageMode = (int)storageMode;
        paramRecorderSubmode = (int)recorderSubmode;
    }
    catch(PcoException& e)
    {
        this->errorTrace << "Failure: " << e.what() << std::endl;
        result = false;
    }
    return result;
}

/**
 * Poll the camera for status information that can be gathered at any time
 */
bool Pco::pollCamera()
{
    bool result = true;
    try
    {
        // Get the temperature information
        short ccdtemp, camtemp, powtemp;
        this->api->getTemperature(this->camera, &ccdtemp, &camtemp, &powtemp);
        // Get memory usage
        int ramUse = this->checkMemoryBuffer();
        // Update EPICS
        TakeLock takeLock(this);
        paramADTemperature = (double)ccdtemp/DllApi::ccdTemperatureScaleFactor;
        paramElectronicsTemp = (double)camtemp;
        paramPowerTemp = (double)powtemp;
        paramCamRamUse = ramUse;
    }
    catch(PcoException& e)
    {
        this->errorTrace << "Failure: " << e.what() << std::endl;
        result = false;
    }
    return result;
}

/**
 * Report the percentage of camera on board memory that contains images.
 * For cameras without on board memory this will always return 0%.
 * Note for a camera with a single image in memory the percentage returned will
 * be at least 1% even if the camera has a massive memory containing a small image.
 */
int Pco::checkMemoryBuffer() throw(PcoException)
{
    int percent = 0;
    if(this->camRamSize > 0)
    {
        unsigned short segment;
        unsigned long validImages;
        unsigned long maxImages;
        try
        {
            this->api->getActiveRamSegment(this->camera, &segment);
            this->api->getNumberOfImagesInSegment(this->camera, segment, &validImages,
                    &maxImages);
            if(maxImages > 0)
            {
                percent = (validImages*100)/maxImages;
                if(validImages > 0 && percent == 0)
                {
                    percent = 1;
                }
            }
        }
        catch(PcoException&)
        {
        }
    }
    return percent;
}

/**
 * Handle a change to the ADAcquire parameter.
 */
void Pco::onAcquire(TakeLock& takeLock)
{
    if(paramADAcquire)
    {
        // Start an acquisition
        this->post(Pco::requestAcquire);
    	if(gangServer)
    	{
    		gangServer->start();
    	}
    }
    else
    {
        // Stop the acquisition
        this->post(Pco::requestStop);
    	if(gangServer)
    	{
    		gangServer->stop();
    	}
    }
}

/**
 * Handle a change to the ArmMode parameter
 */
void Pco::onArmMode(TakeLock& takeLock)
{
    // Perform an arm/disarm
    if(paramArmMode)
    {
        this->post(Pco::requestArm);
    	if(gangServer)
    	{
    		gangServer->arm();
    	}
    }
    else
    {
        this->post(Pco::requestDisarm);
    	if(gangServer)
    	{
    		gangServer->disarm();
    	}
    }
}

/**
 * Handle a change to the Arm parameter
 */
void Pco::onArm(TakeLock& takeLock)
{
    // Perform an arm
    if(paramArm)
    {
        this->post(Pco::requestArm);
    	if(gangServer)
    	{
    		gangServer->arm();
    	}
    }
}

/**
 * Handle a change to the disarm parameter
 */
void Pco::onDisarm(TakeLock& takeLock)
{
    // Perform a disarm
    if(paramDisarm)
    {
        this->post(Pco::requestDisarm);
    	if(gangServer)
    	{
    		gangServer->disarm();
    	}
    }
}

/**
 * Handle a change to the ClearStateRecord parameter
 */
void Pco::onClearStateRecord(TakeLock& takeLock)
{
    if(paramClearStateRecord)
    {
        paramStateRecord = "";
        paramClearStateRecord = 0;
    }
}

/**
 * Handle a change to the Reboot parameter
 */
void Pco::onReboot(TakeLock& takeLock)
{
	this->post(Pco::requestReboot);
}

/**
 * Interpret an attempt to set the temperature as setting the cooling set point
 */
void Pco::onADTemperature(TakeLock& takeLock)
{
    paramCoolingSetpoint = (int)paramADTemperature;
    onCoolingSetpoint(takeLock);
}

/**
 * Post a request
 */
void Pco::post(const StateMachine::Event* req)
{
    this->stateMachine->post(req);
}

/**
 * Allocate an ND array
 */
NDArray* Pco::allocArray(int sizeX, int sizeY, NDDataType_t dataType)
{
    size_t maxDims[] = {sizeX, sizeY};
    NDArray* image = this->pNDArrayPool->alloc(sizeof(maxDims)/sizeof(size_t),
            maxDims, dataType, 0, NULL);
    if(image == NULL)
    {
        // Out of area detector NDArrays
    	TakeLock takeLock(this);
        paramOutOfNDArrays = ++this->outOfNDArrays;
    }
    return image;
}

/**
 * A frame has been received
 */
void Pco::frameReceived(int bufferNumber)
{
	// JAT: Is this optimisation necessary?  I think it is also a little unsafe.
	//      I wonder why I put it in here in the first place?
    // Drop frames if we are idle
    //if(!this->stateMachine->isState(stateIdle))
    {
        // Get an ND array
    	NDArray* image = allocArray(this->xCamSize, this->yCamSize, NDUInt16);
        if(image != NULL)
        {
            // Copy the image into an NDArray
            ::memcpy(image->pData, this->buffers[bufferNumber].buffer,
                    this->xCamSize*this->yCamSize*sizeof(unsigned short));
            // Post the NDarray to the state machine thread
            if(this->receivedFrameQueue.send(&image, sizeof(NDArray*)) != 0)
            {
            	// Failed to put on queue, better free the NDArray to avoid leaks
            	image->release();
            }
            this->post(Pco::requestImageReceived);
        }
    }
    // Give the buffer back to the API
    this->lock();  // JAT: Why am I locking here?
    this->api->addBufferEx(this->camera, /*firstImage=*/0,
        /*lastImage=*/0, bufferNumber,
        this->xCamSize, this->yCamSize, this->camDescription.dynResolution);
    this->unlock();
}

/**
 * Return my asyn user object for use in tracing etc.
 */
asynUser* Pco::getAsynUser()
{
    return this->pasynUserSelf;
}

/**
 * Allocate image buffers and give them to the SDK.  We allocate actual memory here,
 * rather than using the NDArray memory because the SDK hangs onto the buffers, it only
 * shows them to us when there is a frame ready.  We must copy the frame out of the buffer
 * into an NDArray for use by the rest of the system.
 */
void Pco::allocateImageBuffers() throw(std::bad_alloc, PcoException)
{
    // How big?
	int bufferSize = this->camSizes.xResActual * this->camSizes.yResActual;
    // Now allocate the memory and tell the SDK
    try
    {
        for(int i=0; i<Pco::numApiBuffers; i++)
        {
            if(this->buffers[i].buffer != NULL)
            {
                delete[] this->buffers[i].buffer;
            }
            this->buffers[i].buffer = new unsigned short[bufferSize];
            this->buffers[i].bufferNumber = DllApi::bufferUnallocated;
            this->buffers[i].eventHandle = NULL;
            this->api->allocateBuffer(this->camera, &this->buffers[i].bufferNumber,
                    bufferSize * sizeof(short), &this->buffers[i].buffer,
                    &this->buffers[i].eventHandle);
            this->buffers[i].ready = true;
            assert(this->buffers[i].bufferNumber == i);
        }
    }
    catch(std::bad_alloc& e)
    {
        // Recover from memory allocation failure
        this->freeImageBuffers();
        throw e;
    }
    catch(PcoException& e)
    {
        // Recover from PCO camera failure
        this->freeImageBuffers();
        throw e;
    }
}

/**
 * Free the image buffers
 */
void Pco::freeImageBuffers() throw()
{
    // Free the buffers in the camera.  Since we are recovering,
    // ignore any SDK error this may cause.
    try
    {
        this->api->cancelImages(this->camera);
        // JAT: We didn't originally free the buffers from the DLL routinely.
        //      However, for the Dimax it seems to be essential.
        for(int i=0; i<Pco::numApiBuffers; i++)
        {
            this->api->freeBuffer(this->camera, i);
        }
    }
    catch(PcoException& e)
    {
        this->errorTrace << "Failure: " << e.what() << std::endl;
    }
}

/**
 * Depending on the camera, pixel rate and x image size we may have to adjust the transfer parameters
 * in order to achieve the frame rate required across camlink. 
 * The Edge in rolling shutter mode and > 50fps we have to select 12 bit transfer and a look up 
 * table to do the compression.
 * By experiment the following formats appear to work/not work with the Edge:
 *  Global shutter  : PCO_CL_DATAFORMAT_5x12 works
 *                  : PCO_CL_DATAFORMAT_5x16 PCO_CL_DATAFORMAT_5x12L doesn't work
 *  Rolling Shutter : PCO_CL_DATAFORMAT_5x12L PCO_CL_DATAFORMAT_5x12R PCO_CL_DATAFORMAT_5x16 PCO_CL_DATAFORMAT_5x12 works
 */
void Pco::adjustTransferParamsAndLut() throw(PcoException)
{
	unsigned short lutIdentifier = 0;
    // Configure according to camera type
    switch(this->camType)
    {
    case DllApi::cameraTypeEdge:
    case DllApi::cameraTypeEdgeGl:
        // Set the camlink transfer parameters, reading them back
        // again to make sure.
        if(this->cameraSetup == DllApi::edgeSetupGlobalShutter)
        {
            // Works in global and rolling modes
            this->camTransfer.dataFormat = DllApi::camlinkDataFormat5x12 |
                DllApi:: sccmosFormatTopCenterBottomCenter;
            lutIdentifier = DllApi::camlinkLutNone;
        }
        else 
        {
            if(this->xCamSize>=Pco::edgeXSizeNeedsReducedCamlink &&
                    this->pixRate>=Pco::edgePixRateNeedsReducedCamlink)
            {
                // Options for edge are PCO_CL_DATAFORMAT_5x12L (uses sqrt LUT) and 
                // PCO_CL_DATAFORMAT_5x12 (data shifted 2 LSBs lost)
                this->camTransfer.dataFormat = DllApi::camlinkDataFormat5x12L |
                    DllApi::sccmosFormatTopCenterBottomCenter;
                lutIdentifier = DllApi::camLinkLutSqrt;
            } 
            else 
            {
                // Doesn't work in global, works in rolling
                this->camTransfer.dataFormat = DllApi::camlinkDataFormat5x16 |
                    DllApi::sccmosFormatTopCenterBottomCenter;
                lutIdentifier = DllApi::camlinkLutNone;
            }
        }
        this->camTransfer.baudRate = Pco::edgeBaudRate;
        this->camTransfer.transmit = DllApi::transferTransmitEnable;
        if(this->camlinkLongGap)
        {
        	this->camTransfer.transmit |= DllApi::transferTransmitLongGap;
        }
        this->api->setTransferParameters(this->camera, &this->camTransfer);
        this->api->getTransferParameters(this->camera, &this->camTransfer);
        this->api->setActiveLookupTable(this->camera, lutIdentifier);
        break;
    default:
        break;
    }
}

/**
 * Set the camera clock to match EPICS time.
 */
void Pco::setCameraClock() throw(PcoException)
{
    epicsTimeStamp epicsTime;
    epicsTimeGetCurrent(&epicsTime);
    unsigned long nanoSec;
    struct tm currentTime;
    epicsTimeToTM(&currentTime, &nanoSec, &epicsTime);
    this->api->setDateTime(this->camera, &currentTime);
    // Record the year for timestamp correction purposes
    this->cameraYear = currentTime.tm_year;
}

/**
 * Set the camera cooling set point.
 */
void Pco::onCoolingSetpoint(TakeLock& takeLock)
{
    if(paramCoolingSetpoint == 0 && paramMaxCoolingSetpoint == 0)
    {
        // Min and max = 0 means there is no cooling available for this camera.
    }
    else
    {
        try
        {
            this->api->setCoolingSetpoint(this->camera, (short)paramCoolingSetpoint);
        }
        catch(PcoException&)
        {
            // Not much we can do with this error
        }
        try
        {
            short actualCoolingSetpoint;
            this->api->getCoolingSetpoint(this->camera, &actualCoolingSetpoint);
            paramCoolingSetpoint = (int)actualCoolingSetpoint;
        }
        catch(PcoException&)
        {
            // Not much we can do with this error
        }
    }
}

/**
 * Pass buffer to SDK so it can populate it 
 */
void Pco::addAvailableBuffer(int index) throw(PcoException)
{
    if(this->buffers[index].ready)
    {
        this->api->addBufferEx(this->camera, /*firstImage=*/0,
            /*lastImage=*/0, this->buffers[index].bufferNumber,
            this->xCamSize, this->yCamSize, this->camDescription.dynResolution);
        this->buffers[index].ready = false;
    }
}

/**
 * Pass all buffers to the SDK so it can populate them 
 */
void Pco::addAvailableBufferAll() throw(PcoException)
{
    for(int i=0; i<Pco::numApiBuffers; i++)
    {
        addAvailableBuffer(i);
    }
}

/**
 * Arm the camera, ie. prepare the camera for acquisition.
 * Throws exceptions on failure.
 */
void Pco::doArm() throw(std::bad_alloc, PcoException)
{
	TakeLock takeLock(this);
    paramArm = 0;
	// Camera now busy
	paramADStatus = ADStatusReadout;
	// Get configuration information
	this->triggerMode = paramADTriggerMode;
	this->numImages = paramADNumImages;
	this->imageMode = paramADImageMode;
	this->timestampMode = paramTimestampMode;
	this->xMaxSize = paramADMaxSizeX;
	this->yMaxSize = paramADMaxSizeY;
	this->reqRoiStartX = paramADMinX;
	this->reqRoiStartY = paramADMinY;
	this->reqRoiSizeX = paramADSizeX;
	this->reqRoiSizeY = paramADSizeY;
	this->reqBinX = paramADBinX;
	this->reqBinY = paramADBinY;
	this->adcMode = paramAdcMode;
	this->bitAlignmentMode = paramBitAlignment;
	this->acquireMode = paramAcquireMode;
	this->pixRateValue = paramPixRate;
	this->pixRate = this->camDescription.pixelRate[this->pixRateEnumValues[this->pixRateValue]];
	this->exposureTime = paramADAcquireTime;
	this->acquisitionPeriod = paramADAcquirePeriod;
	this->delayTime = paramDelayTime;
	this->cameraSetup = paramCameraSetup;
	this->dataType = paramNDDataType;
	this->reverseX = paramADReverseX;
	this->reverseY = paramADReverseY;
	this->minExposureTime = paramExpTimeMin;
	this->maxExposureTime = paramExpTimeMax;
	this->minDelayTime = paramDelayTimeMin;
	this->maxDelayTime = paramDelayTimeMax;
	this->camlinkLongGap = paramCamlinkLongGap;

	// Configure the camera (reading back the actual settings)
	this->cfgBinningAndRoi();    // Also sets camera image size
	this->cfgTriggerMode();
	this->cfgTimestampMode();
	this->cfgAcquireMode();
	this->cfgAdcMode();
	this->cfgBitAlignmentMode();
	this->cfgPixelRate();
	this->cfgAcquisitionTimes();
	this->allocateImageBuffers();
	this->adjustTransferParamsAndLut();

	// Update what we have really set
	paramADMinX = this->reqRoiStartX;
	paramADMinY = this->reqRoiStartY;
	paramADSizeX = this->reqRoiSizeX;
	paramADSizeY = this->reqRoiSizeY;
	paramADTriggerMode = this->triggerMode;
	paramTimestampMode = this->timestampMode;
	paramAcquireMode = this->acquireMode;
	paramAdcMode = this->adcMode;
	paramBitAlignment = this->bitAlignmentMode;
	paramPixRate = this->pixRateValue;
	paramADAcquireTime = this->exposureTime;
	paramADAcquirePeriod = this->acquisitionPeriod;
	paramDelayTime = this->delayTime;
	paramHwBinX = this->hwBinX;
	paramHwBinY = this->hwBinY;
	paramHwRoiX1 = this->hwRoiX1;
	paramHwRoiY1 = this->hwRoiY1;
	paramHwRoiX2 = this->hwRoiX2;
	paramHwRoiY2 = this->hwRoiY2;
	paramXCamSize = this->xCamSize;
	paramYCamSize = this->yCamSize;
	// Inform server if we have one
	if(gangConnection != NULL)
	{
		gangConnection->sendMemberConfig(takeLock);
	}

	// Set the image parameters for the image buffer transfer inside the CamLink and GigE interface.
	// While using CamLink or GigE this function must be called, before the user tries to get images
	// from the camera and the sizes have changed. With all other interfaces this is a dummy call.
	this->api->camlinkSetImageParameters(this->camera, this->xCamSize, this->yCamSize);

	// Make sure the pco camera clock is correct
	this->setCameraClock();

	// Give the buffers to the camera
	this->addAvailableBufferAll();
	this->lastImageNumber = 0;
	this->lastImageNumberValid = false;

	// Now Arm the camera, so it is ready to take images, all settings should have been made by now
	this->api->arm(this->camera);

	// Start the camera recording
	this->api->setRecordingState(this->camera, DllApi::recorderStateOn);

	// The PCO4000 appears to output 1,2 or 3 dodgy frames immediately on
	// getting the arm.  This bit of code tries to drop them.
	if(this->camType == DllApi::cameraType4000)
	{
		FreeLock freeLock(takeLock);
		epicsThreadSleep(0.3);
		this->discardImages();        // Dump any images
	}
}

/**
 * Configure the ADC mode
 */
void Pco::cfgAdcMode() throw(PcoException)
{
    unsigned short v;
    if(this->camType == DllApi::cameraType1600 ||
            this->camType == DllApi::cameraType2000 ||
            this->camType == DllApi::cameraType4000)
    {
        this->api->setAdcOperation(this->camera, this->adcMode);
        this->api->getAdcOperation(this->camera, &v);
        this->adcMode = v;
    }
    else
    {
        this->adcMode = DllApi::adcModeSingle;
    }
}

/**
 * Configure the acquire mode
 */
void Pco::cfgAcquireMode() throw(PcoException)
{
    unsigned short v;
    this->api->setAcquireMode(this->camera, this->acquireMode);
    this->api->getAcquireMode(this->camera, &v);
    this->acquireMode = v;
}

/**
 * Configure the bit alignment mode
 */
void Pco::cfgBitAlignmentMode() throw(PcoException)
{
    unsigned short v;
    this->api->setBitAlignment(this->camera, this->bitAlignmentMode);
    this->api->getBitAlignment(this->camera, &v);
    this->bitAlignmentMode = v;
}

/**
 * Configure the timestamp mode
 */
void Pco::cfgTimestampMode() throw(PcoException)
{
    unsigned short v;
    if(this->camDescription.generalCaps & DllApi::generalCapsNoTimestamp)
    {
        // No timestamp available
        this->timestampMode = DllApi::timestampModeOff;
    }
    else if(this->camDescription.generalCaps & DllApi::generalCapsTimestampAsciiOnly)
    {
        // All timestamp modes are available
        this->api->setTimestampMode(this->camera, this->timestampMode);
        this->api->getTimestampMode(this->camera, &v);
        this->timestampMode = v;
    }
    else
    {
        // No ASCII only timestamps available
        if(this->timestampMode == DllApi::timestampModeAscii)
        {
            this->timestampMode = DllApi::timestampModeBinaryAndAscii;
        }
        this->api->setTimestampMode(this->camera, this->timestampMode);
        this->api->getTimestampMode(this->camera, &v);
        this->timestampMode = v;
    }
}

/**
 * Configure the trigger mode.
 * Handle the external only trigger mode by translating to the
 * regular external trigger mode.
 */
void Pco::cfgTriggerMode() throw(PcoException)
{
    unsigned short v;
    if(this->triggerMode == DllApi::triggerExternalOnly)
    {
        this->api->setTriggerMode(this->camera, DllApi::triggerExternal);
        this->api->getTriggerMode(this->camera, &v);
        if(v != DllApi::triggerExternal)
        {
            this->triggerMode = (int)v;
        }
    }
    else
    {
        this->api->setTriggerMode(this->camera, this->triggerMode);
        this->api->getTriggerMode(this->camera, &v);
        this->triggerMode = (int)v;
    }
}

/**
 * Configure the binning and region of interest.
 */
void Pco::cfgBinningAndRoi() throw(PcoException)
{
    // Work out the software and hardware binning
    if(this->availBinX.find(this->reqBinX) == this->availBinX.end())
    {
        // Not a binning the camera can do
        this->hwBinX = Pco::defaultHorzBin;
        this->swBinX = this->reqBinX;
    }
    else
    {
        // A binning the camera can do
        this->hwBinX = this->reqBinX;
        this->swBinX = Pco::defaultHorzBin;
    }
    if(this->availBinY.find(this->reqBinY) == this->availBinY.end())
    {
        // Not a binning the camera can do
        this->hwBinY = Pco::defaultVertBin;
        this->swBinY = this->reqBinY;
    }
    else
    {
        // A binning the camera can do
        this->hwBinY = this->reqBinY;
        this->swBinY = Pco::defaultHorzBin;
    }
    this->api->setBinning(this->camera, this->hwBinX, this->hwBinY);
    this->xCamSize = this->camSizes.xResActual / this->hwBinX;
    this->yCamSize = this->camSizes.yResActual / this->hwBinY;

    // Make requested ROI valid
    this->reqRoiStartX = std::max(this->reqRoiStartX, 0);
    this->reqRoiStartX = std::min(this->reqRoiStartX, this->xCamSize-1);
    this->reqRoiStartY = std::max(this->reqRoiStartY, 0);
    this->reqRoiStartY = std::min(this->reqRoiStartY, this->yCamSize-1);
    this->reqRoiSizeX = std::max(this->reqRoiSizeX, 0);
    this->reqRoiSizeX = std::min(this->reqRoiSizeX, this->xCamSize-this->reqRoiStartX);
    this->reqRoiSizeY = std::max(this->reqRoiSizeY, 0);
    this->reqRoiSizeY = std::min(this->reqRoiSizeY, this->yCamSize-this->reqRoiStartY);

    // Get the desired hardware ROI (zero based, end not inclusive)
    this->hwRoiX1 = this->reqRoiStartX;
    this->hwRoiX2 = this->reqRoiStartX+this->reqRoiSizeX;
    this->hwRoiY1 = this->reqRoiStartY;
    this->hwRoiY2 = this->reqRoiStartY+this->reqRoiSizeY;

    // Enforce horizontal symmetry requirements
    if(this->adcMode == DllApi::adcModeDual ||
            this->camType == DllApi::cameraTypeDimaxStd ||
            this->camType == DllApi::cameraTypeDimaxTv ||
            this->camType == DllApi::cameraTypeDimaxAutomotive)
    {
        if(this->hwRoiX1 <= this->xCamSize-this->hwRoiX2)
        {
            this->hwRoiX2 = this->xCamSize - this->hwRoiX1;
        }
        else
        {
            this->hwRoiX1 = this->xCamSize - this->hwRoiX2;
        }
    }

    // Enforce vertical symmetry requirements
    if(this->camType == DllApi::cameraTypeEdge ||
            this->camType == DllApi::cameraTypeEdgeGl ||
            this->camType == DllApi::cameraTypeDimaxStd ||
            this->camType == DllApi::cameraTypeDimaxTv ||
            this->camType == DllApi::cameraTypeDimaxAutomotive)
    {
        if(this->hwRoiY1 <= this->yCamSize-this->hwRoiY2)
        {
            this->hwRoiY2 = this->yCamSize - this->hwRoiY1;
        }
        else
        {
            this->hwRoiY1 = this->yCamSize - this->hwRoiY2;
        }
    }

    // Enforce stepping requirements
    this->hwRoiX1 = (this->hwRoiX1 / this->camDescription.roiHorSteps) *
            this->camDescription.roiHorSteps;
    this->hwRoiY1 = (this->hwRoiY1 / this->camDescription.roiVertSteps) *
            this->camDescription.roiVertSteps;
    this->hwRoiX2 = ((this->hwRoiX2+this->camDescription.roiHorSteps-1) /
            this->camDescription.roiHorSteps) * this->camDescription.roiHorSteps;
    this->hwRoiY2 = ((this->hwRoiY2+this->camDescription.roiVertSteps-1) /
            this->camDescription.roiVertSteps) * this->camDescription.roiVertSteps;

    // Work out the software ROI that cuts off the remaining bits in coordinates
    // relative to the hardware ROI
    this->swRoiStartX = this->reqRoiStartX - this->hwRoiX1;
    this->swRoiStartY = this->reqRoiStartY - this->hwRoiY1;
    this->swRoiSizeX = this->reqRoiSizeX;
    this->swRoiSizeY = this->reqRoiSizeY;

    // Record the size of the frame coming from the camera
    this->xCamSize = this->hwRoiX2 - this->hwRoiX1;
    this->yCamSize = this->hwRoiY2 - this->hwRoiY1;

    // Now change to 1 based coordinates and inclusive end, set the ROI
    // in the hardware
    this->hwRoiX1 += 1;
    this->hwRoiY1 += 1;
    this->api->setRoi(this->camera,
            (unsigned short)this->hwRoiX1, (unsigned short)this->hwRoiY1,
            (unsigned short)this->hwRoiX2, (unsigned short)this->hwRoiY2);

    // Set up the software ROI
    ::memset(this->arrayDims, 0, sizeof(NDDimension_t) * Pco::numDimensions);
    this->arrayDims[Pco::xDimension].offset = this->swRoiStartX;
    this->arrayDims[Pco::yDimension].offset = this->swRoiStartY;
    this->arrayDims[Pco::xDimension].size = this->swRoiSizeX;
    this->arrayDims[Pco::yDimension].size = this->swRoiSizeY;
    this->arrayDims[Pco::xDimension].binning = this->swBinX;
    this->arrayDims[Pco::yDimension].binning = this->swBinY;
    this->arrayDims[Pco::xDimension].reverse = this->reverseX;
    this->arrayDims[Pco::yDimension].reverse = this->reverseY;
    this->roiRequired =
            this->arrayDims[Pco::xDimension].offset != 0 ||
            this->arrayDims[Pco::yDimension].offset != 0 ||
            (int)this->arrayDims[Pco::xDimension].size != this->xCamSize ||
            (int)this->arrayDims[Pco::yDimension].size != this->yCamSize ||
            this->arrayDims[Pco::xDimension].binning != 1 ||
            this->arrayDims[Pco::yDimension].binning != 1 ||
            this->arrayDims[Pco::xDimension].reverse != 0 ||
            this->arrayDims[Pco::yDimension].reverse != 0 ||
            (NDDataType_t)this->dataType != NDUInt16;
}

/**
 * Configure the pixel rate
 */
void Pco::cfgPixelRate() throw(PcoException)
{
    this->api->setPixelRate(this->camera, (unsigned long)this->pixRate);
    unsigned long v;
    this->api->getPixelRate(this->camera, &v);
    this->pixRate = (int)v;
}

/**
 * Write the acquisition times to the camera
 */
void Pco::cfgAcquisitionTimes() throw(PcoException)
{
    // Get the information
    // Work out the delay time to achieve the desired period.  Note that the
    // configured delay time is used unless it is zero, in which case the
    // acquisition period is used.
    double exposureTime = this->exposureTime;
    double delayTime = this->delayTime;
    if(delayTime == 0.0)
    {
        delayTime = std::max(this->acquisitionPeriod - this->exposureTime, 0.0);
    }
    // Check them against the camera's constraints;
    if(delayTime < this->minDelayTime)
    {
        delayTime = this->minDelayTime;
    }
    if(delayTime > this->maxDelayTime)
    {
        delayTime = this->maxDelayTime;
    }
    if(exposureTime < this->minExposureTime)
    {
        exposureTime = this->minExposureTime;
    }
    if(exposureTime > this->maxExposureTime)
    {
        exposureTime = this->maxExposureTime;
    }
    // Work out the best ranges to use to represent to the camera
    unsigned short exposureBase;
    unsigned long exposure;
    unsigned short delayBase;
    unsigned long delay;
    if(this->exposureTime < Pco::timebaseNanosecondsThreshold)
    {
        exposureBase = DllApi::timebaseNanoseconds;
    }
    else if(this->exposureTime < Pco::timebaseMicrosecondsThreshold)
    {
        exposureBase = DllApi::timebaseMicroseconds;
    }
    else
    {
        exposureBase = DllApi::timebaseMilliseconds;
    }
    if(delayTime < Pco::timebaseNanosecondsThreshold)
    {
        delayBase = DllApi::timebaseNanoseconds;
    }
    else if(delayTime < Pco::timebaseMicrosecondsThreshold)
    {
        delayBase = DllApi::timebaseMicroseconds;
    }
    else
    {
        delayBase = DllApi::timebaseMilliseconds;
    }
    // Set the camera
    delay = (unsigned long)(delayTime * DllApi::timebaseScaleFactor[delayBase]);
    exposure = (unsigned long)(exposureTime * DllApi::timebaseScaleFactor[exposureBase]);
    this->api->setDelayExposureTime(this->camera, delay, exposure,
            delayBase, exposureBase);
    // Read back what the camera is actually set to
    this->api->getDelayExposureTime(this->camera, &delay, &exposure,
            &delayBase, &exposureBase);
    this->exposureTime = (double)exposure / DllApi::timebaseScaleFactor[exposureBase];
    delayTime = (double)delay / DllApi::timebaseScaleFactor[delayBase];
    if(this->delayTime != 0.0)
    {
        this->delayTime = delayTime;
    }
    this->acquisitionPeriod = this->exposureTime + delayTime;
}

/**
 * Indicate to EPICS that acquisition has begun.
 */
void Pco::nowAcquiring() throw()
{
	TakeLock takeLock(this);
    // Get info
    this->arrayCounter = paramNDArrayCounter;
    this->numImages = paramADNumImages;
    this->numExposures = paramADNumExposures;
    if(this->imageMode == ADImageSingle)
    {
        this->numImages = 1;
    }
    // Clear counters
    this->numImagesCounter = 0;
    this->numExposuresCounter = 0;
    this->outOfNDArrays = 0;
    this->bufferQueueReadFailures = 0;
    this->buffersWithNoData = 0;
    this->misplacedBuffers = 0;
    this->missingFrames = 0;
    this->driverLibraryErrors = 0;
    // Set info
    paramADStatus = ADStatusReadout;
    paramADAcquire = 1;
    paramNDArraySize = this->xCamSize*this->yCamSize*sizeof(unsigned short);
    paramNDArraySizeX = this->xCamSize;
    paramNDArraySizeY = this->yCamSize;
    paramADNumImagesCounter = this->numImagesCounter;
    paramADNumExposuresCounter = this->numExposuresCounter;
    // Update EPICS
    this->updateErrorCounters();
}

/**
 * An acquisition has completed
 */
void Pco::acquisitionComplete() throw()
{
	TakeLock takeLock(this);
    paramADStatus = ADStatusIdle;
    paramADAcquire = 0;
    this->triggerTimer->stop();
}

/**
 * Exit the armed state
 */
void Pco::doDisarm() throw()
{
	TakeLock lock(this);
    paramArmMode = 0;
    paramDisarm = 0;
    try
    {
        this->api->setRecordingState(this->camera, DllApi::recorderStateOff);
    }
    catch(PcoException&)
    {
        // Not much we can do with this error
    }
    this->freeImageBuffers();
}

/**
 * Update EPICS with the state of the error counters
 */
void Pco::updateErrorCounters() throw()
{
	TakeLock takeLock(this);
    paramOutOfNDArrays = this->outOfNDArrays;
    paramBufferQueueReadFailures = this->bufferQueueReadFailures;
    paramBuffersWithNoData = this->buffersWithNoData;
    paramMisplacedBuffers = this->misplacedBuffers;
    paramMissingFrames = this->missingFrames;
    paramDriverLibraryErrors = this->driverLibraryErrors;
}

/**
 * Start the camera by sending a software trigger if we are in one
 * of the soft modes
 */
void Pco::startCamera() throw()
{
    // Start the camera if we are in one of the soft modes
    if(this->triggerMode == DllApi::triggerSoftware ||
        this->triggerMode == DllApi::triggerExternal)
    {
        unsigned short triggerState = 0;
        try
        {
            this->api->forceTrigger(this->camera, &triggerState);
        }
        catch(PcoException&)
        {
            this->driverLibraryErrors++;
            this->updateErrorCounters();
        }
        // Schedule a retry if it fails
        if(!triggerState)
        {
            // Trigger did not succeed, try again soon
            this->triggerTimer->start(Pco::triggerRetryPeriod, Pco::requestTrigger);
        }
    }
}

/**
 * Discard all images waiting in the queue.
 */
void Pco::discardImages() throw()
{
    while(this->receivedFrameQueue.pending() > 0)
    {
        NDArray* image = NULL;
        this->receivedFrameQueue.tryReceive(&image, sizeof(NDArray*));
        if(image != NULL)
        {
            image->release();
        }
    }
}

/**
 * Receive all available images from the camera.  This function is called in
 * response to an image ready event, but we read all images and cope if there are
 * none so that missing image ready events don't stall the system.  Receiving
 * stops when the queue is empty or the acquisition is complete.  Returns
 * true if the acquisition is complete.
 */
bool Pco::receiveImages() throw()
{
    // Poll the buffer queue
    // Note that the API has aready reset the event so the event status bit
    // returned by getBufferStatus will already be clear.  However, for
    // buffers that do have data ready return a statusDrv of zero.
    while(this->receivedFrameQueue.pending() > 0 &&
    		(this->imageMode == ADImageContinuous ||
            this->numImagesCounter < this->numImages))
    {
        // Get the image
        NDArray* image = NULL;
        this->receivedFrameQueue.tryReceive(&image, sizeof(NDArray*));
        if(image != NULL)
        {
            // What is the number of the image?  If the image does not
            // contain the BCD image number
            // use the dead reckoning number instead.
            long imageNumber = this->lastImageNumber + 1;
            if(this->timestampMode == DllApi::timestampModeBinary ||
                this->timestampMode == DllApi::timestampModeBinaryAndAscii)
            {
                imageNumber = this->extractImageNumber(
                        (unsigned short*)image->pData);
            }
            // If this is the image we are expecting?
            if(imageNumber != this->lastImageNumber+1)
            {
                this->missingFrames++;
                printf("Missing frame, got=%ld, exp=%ld\n", imageNumber, this->lastImageNumber+1);
                TakeLock takeLock(this);
                paramMissingFrames = this->missingFrames;
            }
            this->lastImageNumber = imageNumber;
            // Do software ROI, binning and reversal if required
            if(this->roiRequired)
            {
                NDArray* scratch;
                this->pNDArrayPool->convert(image, &scratch,
                        (NDDataType_t)this->dataType, this->arrayDims);
                image->release();
                image = scratch;
            }
            // Handle summing of multiple exposures
            bool nextImageReady = false;
            if(this->numExposures > 1)
            {
                this->numExposuresCounter++;
                if(this->numExposuresCounter > 1)
                {
                    switch(image->dataType)
                    {
                    case NDUInt8:
                    case NDInt8:
                        sumArray<epicsUInt8>(image, this->imageSum);
                        break;
                    case NDUInt16:
                    case NDInt16:
                        sumArray<epicsUInt16>(image, this->imageSum);
                        break;
                    case NDUInt32:
                    case NDInt32:
                        sumArray<epicsUInt32>(image, this->imageSum);
                        break;
                    default:
                        break;
                    }
                    // throw away the previous accumulator
                    this->imageSum->release();
                }
                // keep the sum of previous images for the next iteration
                this->imageSum = image;
                if(this->numExposuresCounter >= this->numExposures)
                {
                    // we have finished accumulating
                    nextImageReady = true;
                    this->numExposuresCounter = 0;
                }
            }
            else
            {
                nextImageReady = true;
            }
            if(nextImageReady)
            {
                // Attach the image information
                image->uniqueId = this->arrayCounter;
                epicsTimeStamp imageTime;
                if(this->timestampMode == DllApi::timestampModeBinary ||
                        this->timestampMode == DllApi::timestampModeBinaryAndAscii)
                {
                    this->extractImageTimeStamp(&imageTime,
                            (unsigned short*)image->pData);
                }
                else
                {
                    epicsTimeGetCurrent(&imageTime);
                }
                image->timeStamp = imageTime.secPastEpoch +
                        imageTime.nsec / Pco::oneNanosecond;
                this->getAttributes(image->pAttributeList);
                // Show the image to the gang system
                if(this->gangConnection)
                {
                	this->gangConnection->sendImage(image, this->numImagesCounter);
                }
                if(!this->gangServer ||
                		!gangServer->imageReceived(this->numImagesCounter, image))
                {
                	// Gang system did not consume it, pass it on now
                	imageComplete(image);
                }
            }
        }
    }
    TakeLock takelLock(this);
    paramADNumExposuresCounter = this->numExposuresCounter;
    paramImageNumber = this->lastImageNumber;
    return this->imageMode != ADImageContinuous &&
    		this->numImagesCounter >= this->numImages;
}

/**
 * An image has been completed, pass it on.
 */
void Pco::imageComplete(NDArray* image)
{
    // Update statistics
    this->arrayCounter++;
    this->numImagesCounter++;
    // Pass the array on
    this->doCallbacksGenericPointer(image, NDArrayData, 0);
    image->release();
    TakeLock takeLock(this);
    paramNDArrayCounter = arrayCounter;
    paramADNumImagesCounter = this->numImagesCounter;
}

/**
 * Handle the construction of images in the ganged mode.
 * Returns true if the acquisition is complete.
 */
bool Pco::makeImages()
{
	bool result = false;
	if(this->gangServer)
	{
		TakeLock takeLock(this);
		gangServer->makeCompleteImages(takeLock);
		result = this->imageMode != ADImageContinuous &&
				this->numImagesCounter >= this->numImages;
	}
	return result;
}

/**
 * Convert BCD coded number in image to int 
 */
long Pco::bcdToInt(unsigned short pixel) throw()
{
    int shiftLowBcd = 0;
    if(paramBitAlignment == DllApi::bitAlignmentMsb)
    {
        // In MSB mode, need to shift down
        shiftLowBcd = Pco::bitsPerShortWord - this->camDescription.dynResolution;
    }
    int shiftHighBcd = shiftLowBcd + Pco::bitsPerNybble;
    long p1 = (pixel>>shiftLowBcd)&(Pco::nybbleMask);
    long p2 = (pixel>>shiftHighBcd)&(Pco::nybbleMask);
    return p2*bcdDigitValue + p1;    
}

/**
 * Convert bcd number in first 4 pixels of image to extract image counter value
 */
long Pco::extractImageNumber(unsigned short* imagebuffer) throw()
{

    long imageNumber = 0;
    for(int i=0; i<Pco::bcdPixelLength; i++) 
    {
        imageNumber *= Pco::bcdDigitValue * Pco::bcdDigitValue;
        imageNumber += bcdToInt(imagebuffer[i]);
    };
    return imageNumber;
}

/**
 * Convert bcd numbers in pixels 5 to 14 of image to extract time stamp 
 */
void Pco::extractImageTimeStamp(epicsTimeStamp* imageTime,
        unsigned short* imageBuffer) throw()
{
    unsigned long nanoSec = 0;
    struct tm ct;
    ct.tm_year = bcdToInt(imageBuffer[4])*100 + bcdToInt(imageBuffer[5] - 1900);
    ct.tm_mon = bcdToInt(imageBuffer[6])-1;
    ct.tm_mday = bcdToInt(imageBuffer[7]);
    ct.tm_hour = bcdToInt(imageBuffer[8]);
    ct.tm_min = bcdToInt(imageBuffer[9]);
    ct.tm_sec = bcdToInt(imageBuffer[10]);
    nanoSec = (bcdToInt(imageBuffer[11])*10000 + bcdToInt(imageBuffer[12])*100 +
            bcdToInt(imageBuffer[13]))*1000;
   
#if 0
    // JAT: We'll comment this out for now and see what the PCO
    //      does return.
    // fix year if necessary
    // (pco4000 seems to always return 2036 for year)
    if(ct.tm_year >= 2036)
    {
        ct.tm_year = this->cameraYear;
    }
#endif
        
    epicsTimeFromTM (imageTime, &ct, nanoSec );
}

/**
 * Register the gang server object
 */
void Pco::registerGangServer(GangServer* gangServer)
{
	this->gangServer = gangServer;
	lock();
	paramGangMode = gangModeServer;
	unlock();
}


/**
 * Register the gang client object
 */
void Pco::registerGangConnection(GangConnection* gangConnection)
{
	this->gangConnection = gangConnection;
	lock();
	paramGangMode = gangModeConnection;
	unlock();
}

/**
 * Helper function to sum 2 NDArrays
 */
template<typename T> void Pco::sumArray(NDArray* startingArray,
        NDArray* addArray) throw()
{
    T* inOutData = reinterpret_cast<T*>(startingArray->pData);
    T* addData = reinterpret_cast<T*>(addArray->pData);
    NDArrayInfo_t inInfo;

    startingArray->getInfo(&inInfo);
    for(int i=0; i<this->xCamSize*this->yCamSize; i++)
    {
        *inOutData += *addData;
        inOutData++;
        addData++;
    }
}

// IOC shell configuration command
extern "C" int pcoConfig(const char* portName, int maxBuffers, size_t maxMemory)
{
    Pco* existing = Pco::getPco(portName);
    if(existing == NULL)
    {
        new Pco(portName, maxBuffers, maxMemory);
    }
    else
    {
        printf("Error: port name \"%s\" already exists\n", portName);
    }
    return asynSuccess;
}
static const iocshArg pcoConfigArg0 = {"Port name", iocshArgString};
static const iocshArg pcoConfigArg1 = {"maxBuffers", iocshArgInt};
static const iocshArg pcoConfigArg2 = {"maxMemory", iocshArgInt};
static const iocshArg * const pcoConfigArgs[] = {&pcoConfigArg0, &pcoConfigArg1,
        &pcoConfigArg2};
static const iocshFuncDef configPco = {"pcoConfig", 3, pcoConfigArgs};
static void configPcoCallFunc(const iocshArgBuf *args)
{
    pcoConfig(args[0].sval, args[1].ival, args[2].ival);
}

/** Register the commands */
static void pcoRegister(void)
{
    iocshRegister(&configPco, configPcoCallFunc);
}
extern "C" { epicsExportRegistrar(pcoRegister); }

