#pragma once
#include "CoreMinimal.h"

enum class EProjectTravelDeadline {Wait,Complete,Abort};
struct FProjectTravelDeadline
{
    static EProjectTravelDeadline Evaluate(double Elapsed,bool EngineTransition,bool WorldChanged)
    {
        if(WorldChanged&&!EngineTransition)return EProjectTravelDeadline::Complete;
        if(EngineTransition&&Elapsed>=120)return EProjectTravelDeadline::Abort;
        return EProjectTravelDeadline::Wait;
    }
};
