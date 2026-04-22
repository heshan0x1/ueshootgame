// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include <nls/DiffData.h>
#include <carbon/Common.h>
#include <carbon/common/Pimpl.h>

#include <vector>

namespace dna
{
class Reader;
class Writer;
}

CARBON_NAMESPACE_BEGIN(TITAN_NAMESPACE)

template <class T>
class TwistSwingLogic
{
public:

    TwistSwingLogic();
    ~TwistSwingLogic();

    TwistSwingLogic(TwistSwingLogic&&);
    TwistSwingLogic& operator=(TwistSwingLogic&&);

    TwistSwingLogic(const TwistSwingLogic&);
    TwistSwingLogic& operator=(const TwistSwingLogic&);

    DiffData<T> EvaluateJointsFromJoints(const DiffData<T>& jointDiff) const;
    DiffData<T> EvaluateJointsFromRawControls(const DiffData<T>& rawControls) const;

    void RemoveJoints(const std::vector<int>& newToOldJointMapping);

    bool Init(const dna::Reader* reader, bool withJointScaling = false);
    void Write(dna::Writer* writer);

    private:
        struct Private;
        Pimpl<Private> m;
};

CARBON_NAMESPACE_END(TITAN_NAMESPACE)


