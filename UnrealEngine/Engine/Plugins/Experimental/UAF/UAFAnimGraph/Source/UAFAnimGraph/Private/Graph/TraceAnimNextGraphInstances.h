// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "EvaluationVM/EvaluationProgram.h"
#include "RewindDebugger/AnimNextTrace.h"

#if ANIMNEXT_TRACE_ENABLED

struct FAnimNextGraphInstance;

namespace UE::UAF
{
	struct FEvaluationProgram;
	
	void TraceGraphInstances(const FAnimNextGraphInstance& RootGraph);
	void TraceEvaluationProgram(const FEvaluationProgram& Program, const FAnimNextGraphInstance& RootGraph);
}

#define TRACE_ANIMNEXT_GRAPHINSTANCES(RootGraph) UE::UAF::TraceGraphInstances(RootGraph);
#define TRACE_ANIMNEXT_EVALUATIONPROGRAM(Program, RootGraph) UE::UAF::TraceEvaluationProgram(Program, RootGraph);

#else

#define TRACE_ANIMNEXT_GRAPHINSTANCES(RootGraph)
#define TRACE_ANIMNEXT_EVALUATIONPROGRAM(Program, RootGraph)

#endif
