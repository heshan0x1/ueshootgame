// Copyright Epic Games, Inc. All Rights Reserved.

#include "Data/PCGPolygon2DData.h"
#include "Metadata/Accessors/IPCGAttributeAccessorTpl.h"
#include "UObject/ObjectKey.h"

namespace PCGTemporary
{
	/**
	* Templated accessor for polygon vertices accessor.
	* NOTE FOR 5.7.2: The original accessor is NOT thread-safe on Set, and is replaced by this one since we can't modify headers in minor versions.
	* As a temporary fix and hack, we'll access directly the values in the polygon vertices. There is a huge assumption that vertices/holes arrays are not modified.
	* Also, writing is thread-safe only if multiple threads are never writing to the same range of indices.
	*/
	template<typename T, EPCGPolygon2DProperties Target>
	class FPCGPolygon2DVerticesAccessor : public IPCGAttributeAccessorT<FPCGPolygon2DVerticesAccessor<T, Target>>
	{
	public:
		using Type = T;
		using Super = IPCGAttributeAccessorT<FPCGPolygon2DVerticesAccessor<T, Target>>;

		FPCGPolygon2DVerticesAccessor(bool bIsReadOnly)
			: Super(bIsReadOnly)
		{
		}

		bool GetRangeImpl(TArrayView<Type> OutValues, int32 Index, const IPCGAttributeAccessorKeys& Keys) const
		{
			const void* ContainerKeys = nullptr;
			TArrayView<const void*> ContainerKeysView(&ContainerKeys, 1);
			if (!Keys.GetKeys(Index, ContainerKeysView))
			{
				return false;
			}

			// Validation to not access keys that are not the expected type. Done after the GetKeys, as we also want to discard other type of incompatible
			// keys (like a Default Metadata entry key)
			if (!ensure(Keys.IsClassSupported(UPCGPolygon2DData::StaticClass())))
			{
				return false;
			}

			const UPCGPolygon2DData* Data = static_cast<const UPCGPolygon2DData*>(ContainerKeys);
			const FTransform& Transform = Data->GetTransform();
			const UE::Geometry::FGeneralPolygon2d& Polygon = Data->GetPolygon();
			const TMap<int, TPair<int, int>>& SegmentIndexToSegmentAndHoleIndices = Data->GetSegmentIndexToSegmentAndHoleIndices();

			const int32 NumPoints = SegmentIndexToSegmentAndHoleIndices.Num();
			if (NumPoints == 0)
			{
				return false;
			}

			for (int32 i = 0; i < OutValues.Num(); ++i)
			{
				const int32 CurrIndex = (Index + i) % NumPoints;
				auto [SegmentIndex, HoleIndex] = SegmentIndexToSegmentAndHoleIndices[CurrIndex];

				if constexpr (Target == EPCGPolygon2DProperties::Position)
				{
					static_assert(std::is_same_v<Type, FVector>);
					const UE::Geometry::FSegment2d Segment = Polygon.Segment(SegmentIndex, HoleIndex);
					OutValues[i] = Transform.TransformPosition(FVector(Segment.StartPoint(), 0.0));
				}
				else if constexpr (Target == EPCGPolygon2DProperties::Rotation)
				{
					static_assert(std::is_same_v<Type, FQuat>);
					//@todo_pcg: there is a Polygon.GetNormal that we could use, but it blends normals across vertices.
					const UE::Geometry::FSegment2d Segment = Polygon.Segment(SegmentIndex, HoleIndex);
					OutValues[i] = Transform.TransformRotation(FRotationMatrix::MakeFromXZ(FVector(Segment.Direction, 0.0), FVector::UpVector).ToQuat());
				}
				else if constexpr (Target == EPCGPolygon2DProperties::SegmentIndex)
				{
					static_assert(std::is_same_v<Type, int32>);
					OutValues[i] = SegmentIndex;
				}
				else if constexpr (Target == EPCGPolygon2DProperties::HoleIndex)
				{
					static_assert(std::is_same_v<Type, int32>);
					OutValues[i] = HoleIndex;
				}
				else if constexpr (Target == EPCGPolygon2DProperties::SegmentLength)
				{
					static_assert(std::is_same_v<Type, double>);
					const UE::Geometry::FSegment2d Segment = Polygon.Segment(SegmentIndex, HoleIndex);
					OutValues[i] = Segment.Length();
				}
				else if constexpr (Target == EPCGPolygon2DProperties::LocalPosition)
				{
					static_assert(std::is_same_v<Type, FVector2d>);
					const UE::Geometry::FSegment2d Segment = Polygon.Segment(SegmentIndex, HoleIndex);
					OutValues[i] = Segment.StartPoint();
				}
				else if constexpr (Target == EPCGPolygon2DProperties::LocalRotation)
				{
					static_assert(std::is_same_v<Type, FQuat>);
					const UE::Geometry::FSegment2d Segment = Polygon.Segment(SegmentIndex, HoleIndex);
					OutValues[i] = FRotationMatrix::MakeFromXZ(FVector(Segment.Direction, 0.0), FVector::UpVector).ToQuat();
				}
				else
				{
					// Pitfall static assert
					static_assert(!std::is_same_v<Type, Type>);
				}
			}

			return true;
		}

		bool SetRangeImpl(TArrayView<const Type> InValues, int32 Index, IPCGAttributeAccessorKeys& Keys, EPCGAttributeAccessorFlags)
		{
			void* ContainerKeys = nullptr;
			TArrayView<void*> ContainerKeysView(&ContainerKeys, 1);
			if (!Keys.GetKeys(Index, ContainerKeysView))
			{
				return false;
			}

			// Validation to not access keys that are not the expected type. Done after the GetKeys, as we also want to discard other type of incompatible
			// keys (like a Default Metadata entry key)
			if (!ensure(Keys.IsClassSupported(UPCGPolygon2DData::StaticClass())))
			{
				return false;
			}

			UPCGPolygon2DData* Data = static_cast<UPCGPolygon2DData*>(ContainerKeys);
			const TMap<int, TPair<int, int>>& SegmentIndexToSegmentAndHoleIndices = Data->GetSegmentIndexToSegmentAndHoleIndices();

			const int32 NumPoints = SegmentIndexToSegmentAndHoleIndices.Num();
			if (NumPoints == 0)
			{
				return false;
			}

			if constexpr (Target != EPCGPolygon2DProperties::Position && Target != EPCGPolygon2DProperties::LocalPosition)
			{
				return false;
			}
			else
			{
				const UE::Geometry::FGeneralPolygon2d& Polygon = Data->GetPolygon();
				const FTransform& Transform = Data->GetTransform();
				
				FTransform InverseTransform;

				if constexpr (Target == EPCGPolygon2DProperties::Position)
				{
					InverseTransform = Transform.Inverse();
				}

				// 5.7.2 HACK: We const_cast to modify the values in place.
				UE::Geometry::TPolygon2<double>& Outer = const_cast<UE::Geometry::TPolygon2<double>&>(Polygon.GetOuter());
				TArray<UE::Geometry::TPolygon2<double>>& Holes = const_cast<TArray<UE::Geometry::TPolygon2<double>>&>(Polygon.GetHoles());

				for (int i = 0; i < InValues.Num(); ++i)
				{
					const int32 CurrIndex = (Index + i) % NumPoints;
					auto [SegmentIndex, HoleIndex] = SegmentIndexToSegmentAndHoleIndices[CurrIndex];

					UE::Geometry::TPolygon2<double>& Vertices = HoleIndex == -1 ? Outer : Holes[HoleIndex];

					if constexpr (Target == EPCGPolygon2DProperties::Position)
					{
						static_assert(std::is_same_v<Type, FVector3d>);
						FVector LocalPosition = InverseTransform.TransformPosition(InValues[i]);
						Vertices[SegmentIndex] = FVector2d(LocalPosition.X, LocalPosition.Y);
					}
					else if constexpr (Target == EPCGPolygon2DProperties::LocalPosition)
					{
						static_assert(std::is_same_v<Type, FVector2d>);
						Vertices[SegmentIndex] = InValues[i];
					}
					else
					{
						// Pitfall static assert
						static_assert(!std::is_same_v<Type, Type>);
					}
				}
				
				return true;
			}
		}
	};
}
