/* GangServer.h
 *
 * Revamped PCO area detector driver.
 * The communication connection class for a ganged pair
 *
 * Author:  Giles Knap
 *          Jonathan Thompson
 *
 */

#include "PerformanceMonitor.h"
#include "TraceStream.h"
#include "Pco.h"
#include "TakeLock.h"
#include "FreeLock.h"
#include <sstream>

// Constructor
// When used at the client end, the server is NULL
PerformanceMonitor::PerformanceMonitor(Pco* pco, TraceStream* trace)
	: pco(pco)
	, trace(trace)
	, paramCntGoodFrame(pco, "PCO_PERFCNT_GOODFRAME")
	, paramCntMissingFrame(pco, "PCO_PERFCNT_MISSINGFRAME")
	, paramCntOutOfArrays(pco, "PCO_PERFCNT_OUTOFARRAYS")
	, paramCntInvalidFrame(pco, "PCO_PERFCNT_INVALIDFRAME")
	, paramCntFrameStatusError(pco, "PCO_PERFCNT_FRAMESTATUSERROR")
	, paramCntWaitFault(pco, "PCO_PERFCNT_WAITFAULT")
	, paramCntDriverError(pco, "PCO_PERFCNT_DRIVERERROR")
	, paramCntCaptureError(pco, "PCO_PERFCNT_CAPTUREERROR")
	, paramCntPollGetFrame(pco, "PCO_PERFCNT_POLLGETFRAME")
	, paramCntFault(pco, "PCO_PERFCNT_FAULT")
	, paramAccReboot(pco, "PCO_PERFACC_REBOOT")
	, paramAccConnect(pco, "PCO_PERFACC_CONNECT")
	, paramAccArm(pco, "PCO_PERFACC_ARM")
	, paramAccStart(pco, "PCO_PERFACC_START")
	, paramAccGoodFrame(pco, "PCO_PERFACC_GOODFRAME")
	, paramAccMissingFrame(pco, "PCO_PERFACC_MISSINGFRAME")
	, paramAccOutOfArrays(pco, "PCO_PERFACC_OUTOFARRAYS")
	, paramAccInvalidFrame(pco, "PCO_PERFACC_INVALIDFRAME")
	, paramAccFrameStatusError(pco, "PCO_PERFACC_FRAMESTATUSERROR")
	, paramAccWaitFault(pco, "PCO_PERFACC_WAITFAULT")
	, paramAccDriverError(pco, "PCO_PERFACC_DRIVERERROR")
	, paramAccCaptureError(pco, "PCO_PERFACC_CAPTUREERROR")
	, paramAccPollGetFrame(pco, "PCO_PERFACC_POLLGETFRAME")
	, paramAccFault(pco, "PCO_PERFACC_FAULT")
	, paramReset(pco, "PCO_PERF_RESET", 0,
			new AsynParam::Notify<PerformanceMonitor>(this, &PerformanceMonitor::onReset))
{
	// Set up the counter maps
	this->session[PERF_GOODFRAME] = &this->paramCntGoodFrame;
	this->session[PERF_MISSINGFRAME] = &this->paramCntMissingFrame;
	this->session[PERF_OUTOFARRAYS] = &this->paramCntOutOfArrays;
	this->session[PERF_INVALIDFRAME] = &this->paramCntInvalidFrame;
	this->session[PERF_FRAMESTATUSERROR] = &this->paramCntFrameStatusError;
	this->session[PERF_WAITFAULT] = &this->paramCntWaitFault;
	this->session[PERF_DRIVERERROR] = &this->paramCntDriverError;
	this->session[PERF_CAPTUREERROR] = &this->paramCntCaptureError;
	this->session[PERF_POLLGETFRAME] = &this->paramCntPollGetFrame;
	this->accumulating[PERF_REBOOT] = &this->paramAccReboot;
	this->accumulating[PERF_CONNECT] = &this->paramAccConnect;
	this->accumulating[PERF_ARM] = &this->paramAccArm;
	this->accumulating[PERF_START] = &this->paramAccStart;
	this->accumulating[PERF_GOODFRAME] = &this->paramAccGoodFrame;
	this->accumulating[PERF_MISSINGFRAME] = &this->paramAccMissingFrame;
	this->accumulating[PERF_OUTOFARRAYS] = &this->paramAccOutOfArrays;
	this->accumulating[PERF_INVALIDFRAME] = &this->paramAccInvalidFrame;
	this->accumulating[PERF_FRAMESTATUSERROR] = &this->paramAccFrameStatusError;
	this->accumulating[PERF_WAITFAULT] = &this->paramAccWaitFault;
	this->accumulating[PERF_DRIVERERROR] = &this->paramAccDriverError;
	this->accumulating[PERF_CAPTUREERROR] = &this->paramAccCaptureError;
	this->accumulating[PERF_POLLGETFRAME] = &this->paramAccPollGetFrame;
}

// Destructor
PerformanceMonitor::~PerformanceMonitor()
{
}

// Increment a counter
void PerformanceMonitor::count(TakeLock& takeLock, PerformanceMonitor::Param param, bool fault)
{
	std::map<PerformanceMonitor::Param, IntegerParam*>::iterator pos;
	// Advance the session counter
	pos = this->session.find(param);
	if(pos != this->session.end())
	{
		*(pos->second) = *(pos->second) + 1;
	}
	// Advance the accumulating counter
	pos = this->accumulating.find(param);
	if(pos != this->accumulating.end())
	{
		*(pos->second) = *(pos->second) + 1;
	}
	// Advance the overall fault counters
	if(fault)
	{
		paramCntFault = paramCntFault + 1;
		paramAccFault = paramAccFault + 1;
	}
}

// Reset the session counters
void PerformanceMonitor::clear(TakeLock& takeLock)
{
	(*trace) << "Clear session counters" << std::endl;
	std::map<PerformanceMonitor::Param, IntegerParam*>::iterator pos;
	for(pos=this->session.begin(); pos!=this->session.end(); ++pos)
	{
		*(pos->second) = 0;
	}
	paramCntFault = 0;
}

// Reset all counters
void PerformanceMonitor::onReset(TakeLock& takeLock)
{
	// The session counters
	this->clear(takeLock);
	// The accumulating counters
	(*trace) << "Clear accumulating counters" << std::endl;
	std::map<PerformanceMonitor::Param, IntegerParam*>::iterator pos;
	for(pos=this->accumulating.begin(); pos!=this->accumulating.end(); ++pos)
	{
		*(pos->second) = 0;
	}
	paramAccFault = 0;
}