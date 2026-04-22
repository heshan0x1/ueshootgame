// Copyright Epic Games, Inc. All Rights Reserved.

#include "JsonObjectGraph/Stringify.h"
#include "JsonStringifyImpl.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(Stringify)

FUtf8String UE::JsonObjectGraph::Stringify(TConstArrayView<const UObject*> RootObjects, const FJsonStringifyOptions& Options)
{
	UE::Private::FJsonStringifyImpl Impl(RootObjects, Options);
	return Impl.ToJson();
}
