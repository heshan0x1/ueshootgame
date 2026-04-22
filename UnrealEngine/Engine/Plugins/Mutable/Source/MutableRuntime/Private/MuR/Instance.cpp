// Copyright Epic Games, Inc. All Rights Reserved.


#include "MuR/Instance.h"

#include "HAL/LowLevelMemTracker.h"
#include "Misc/AssertionMacros.h"
#include "MuR/MutableMath.h"

namespace UE::Mutable::Private
{
    TSharedPtr<FInstance> FInstance::Clone() const
    {
		LLM_SCOPE_BYNAME(TEXT("MutableRuntime"));
		TSharedPtr<FInstance> Result = MakeShared<FInstance>();

        *Result = *this;

        return Result;
    }
	
	
	int32 FInstance::GetDataSize() const
	{
		return 16 + sizeof(*this) + Components.GetAllocatedSize() + ExtensionData.GetAllocatedSize();
	}
	
    
    int32 FInstance::GetComponentCount() const
    {
		return Components.Num();
    }
	
	
	int32 FInstance::GetLODCount( int32 ComponentIndex ) const
	{
		if (Components.IsValidIndex(ComponentIndex))
		{
			return Components[ComponentIndex].LODs.Num();
		}
		check(false);
		return 0;
	}
	
	
	uint16 FInstance::GetComponentId( int32 ComponentIndex ) const
	{
		if (Components.IsValidIndex(ComponentIndex))
		{
			return Components[ComponentIndex].Id;
		}
		else
		{
			check(false);
		}

		return 0;
	}

	
    int32 FInstance::GetSurfaceCount( int32 ComponentIndex, int32 LODIndex ) const
    {
		if (Components.IsValidIndex(ComponentIndex) &&
			Components[ComponentIndex].LODs.IsValidIndex(LODIndex))
		{
			return Components[ComponentIndex].LODs[LODIndex].Surfaces.Num();
		}
		else
		{
			check(false);
		}

		return 0;
	}
	
    
    uint32 FInstance::GetSurfaceId(int32 ComponentIndex, int32 LODIndex, int32 SurfaceIndex ) const
    {
        if (Components.IsValidIndex(ComponentIndex) &&
			Components[ComponentIndex].LODs.IsValidIndex(LODIndex) &&
			Components[ComponentIndex].LODs[LODIndex].Surfaces.IsValidIndex(SurfaceIndex) )
        {
            return Components[ComponentIndex].LODs[LODIndex].Surfaces[SurfaceIndex].InternalId;
        }
		else
		{
			check(false);
		}

        return 0;
    }

    
    int32 FInstance::FindSurfaceById(int32 ComponentIndex, int32 LODIndex, uint32 id ) const
    {
		if (Components.IsValidIndex(ComponentIndex) &&
			Components[ComponentIndex].LODs.IsValidIndex(LODIndex))
		{
			for (int32 i = 0; i < Components[ComponentIndex].LODs[LODIndex].Surfaces.Num(); ++i)
			{
				if (Components[ComponentIndex].LODs[LODIndex].Surfaces[i].InternalId == id)
				{
					return i;
				}
			}
		}
		else
		{
			check(false);
		}

        return -1;
    }
	
	
	void FInstance::FindBaseSurfaceBySharedId(int32 CompIndex, uint32 SharedId, int32& OutSurfaceIndex, int32& OutLODIndex) const
	{
		if (Components.IsValidIndex(CompIndex))
		{
			for (int32 LodIndex = 0; LodIndex < Components[CompIndex].LODs.Num(); LodIndex++)
			{
				const FInstanceLOD& LOD = Components[CompIndex].LODs[LodIndex];
				for (int32 SurfaceIndex = 0; SurfaceIndex < LOD.Surfaces.Num(); ++SurfaceIndex)
				{
					if (LOD.Surfaces[SurfaceIndex].SharedId == SharedId)
					{
						OutSurfaceIndex = SurfaceIndex;
						OutLODIndex = LodIndex;
						return;
					}
				}
			}

		}

		OutSurfaceIndex = INDEX_NONE;
		OutLODIndex = INDEX_NONE;
	}

	
	uint32 FInstance::GetSharedSurfaceId(int32 ComponentIndex, int32 LODIndex, int32 SurfaceIndex) const
	{
		if (Components.IsValidIndex(ComponentIndex) &&
			Components[ComponentIndex].LODs.IsValidIndex(LODIndex) &&
			Components[ComponentIndex].LODs[LODIndex].Surfaces.IsValidIndex(SurfaceIndex))
		{
			return Components[ComponentIndex].LODs[LODIndex].Surfaces[SurfaceIndex].SharedId;
		}
		else
		{
			check(false);
		}

		return 0;
	}
	
    
    uint32 FInstance::GetSurfaceCustomId(int32 ComponentIndex, int32 LODIndex, int32 SurfaceIndex ) const
    {
        if (Components.IsValidIndex(ComponentIndex) &&
			Components[ComponentIndex].LODs.IsValidIndex(LODIndex) &&
			Components[ComponentIndex].LODs[LODIndex].Surfaces.IsValidIndex(SurfaceIndex))
        {
            return Components[ComponentIndex].LODs[LODIndex].Surfaces[SurfaceIndex].ExternalId;
        }
		else
		{
			check(false);
		}

        return 0;
    }
	
	
    int32 FInstance::GetImageCount(int32 ComponentIndex, int32 LODIndex, int32 SurfaceIndex ) const
	{
		check(Components.IsValidIndex(ComponentIndex));
		check(Components[ComponentIndex].LODs.IsValidIndex(LODIndex));
		check(Components[ComponentIndex].LODs[LODIndex].Surfaces.IsValidIndex(SurfaceIndex));

		return Components[ComponentIndex].LODs[LODIndex].Surfaces[SurfaceIndex].Images.Num();
	}

	
    int32 FInstance::GetVectorCount(int32 ComponentIndex, int32 LODIndex, int32 SurfaceIndex ) const
	{
		check(Components.IsValidIndex(ComponentIndex));
		check(Components[ComponentIndex].LODs.IsValidIndex(LODIndex));
		check(Components[ComponentIndex].LODs[LODIndex].Surfaces.IsValidIndex(SurfaceIndex));

		return Components[ComponentIndex].LODs[LODIndex].Surfaces[SurfaceIndex].Vectors.Num();
	}

	
    int32 FInstance::GetScalarCount(int32 ComponentIndex, int32 LODIndex, int32 SurfaceIndex ) const
	{
		check(Components.IsValidIndex(ComponentIndex));
		check(Components[ComponentIndex].LODs.IsValidIndex(LODIndex));
		check(Components[ComponentIndex].LODs[LODIndex].Surfaces.IsValidIndex(SurfaceIndex));

		return Components[ComponentIndex].LODs[LODIndex].Surfaces[SurfaceIndex].Scalars.Num();
	}

    
    int32 FInstance::GetStringCount(int32 ComponentIndex, int32 LODIndex, int32 SurfaceIndex ) const
    {
		check(Components.IsValidIndex(ComponentIndex));
		check(Components[ComponentIndex].LODs.IsValidIndex(LODIndex));
		check(Components[ComponentIndex].LODs[LODIndex].Surfaces.IsValidIndex(SurfaceIndex));

		return Components[ComponentIndex].LODs[LODIndex].Surfaces[SurfaceIndex].Strings.Num();
	}
	
    
	FMeshId FInstance::GetMeshId(int32 ComponentIndex, int32 LODIndex) const
    {
        check(Components.IsValidIndex(ComponentIndex));
        check(Components[ComponentIndex].LODs.IsValidIndex(LODIndex));

		return Components[ComponentIndex].LODs[LODIndex].MeshId;
    }

	
	FImageId FInstance::GetImageId(int32 ComponentIndex, int32 LODIndex, int32 SurfaceIndex, int32 ImageIndex) const
	{
		check(Components.IsValidIndex(ComponentIndex));
		check(Components[ComponentIndex].LODs.IsValidIndex(LODIndex));
        check(Components[ComponentIndex].LODs[LODIndex].Surfaces.IsValidIndex(SurfaceIndex));
        check(Components[ComponentIndex].LODs[LODIndex].Surfaces[SurfaceIndex].Images.IsValidIndex(ImageIndex));

		return Components[ComponentIndex].LODs[LODIndex].Surfaces[SurfaceIndex].Images[ImageIndex].Id;
	}

	
    FName FInstance::GetImageName(int32 ComponentIndex, int32 LODIndex, int32 SurfaceIndex, int32 ImageIndex ) const
	{
		check(Components.IsValidIndex(ComponentIndex));
		check(Components[ComponentIndex].LODs.IsValidIndex(LODIndex));
        check(Components[ComponentIndex].LODs[LODIndex].Surfaces.IsValidIndex(SurfaceIndex));
		check(Components[ComponentIndex].LODs[LODIndex].Surfaces[SurfaceIndex].Images.IsValidIndex(ImageIndex));

        return Components[ComponentIndex].LODs[LODIndex].Surfaces[SurfaceIndex].Images[ImageIndex].Name;
	}
	
	
	FVector4f FInstance::GetVector(int32 ComponentIndex, int32 LODIndex, int32 SurfaceIndex, int32 VectorIndex) const
	{
		check(Components.IsValidIndex(ComponentIndex));
		check(Components[ComponentIndex].LODs.IsValidIndex(LODIndex));
        check(Components[ComponentIndex].LODs[LODIndex].Surfaces.IsValidIndex(SurfaceIndex));
		check(Components[ComponentIndex].LODs[LODIndex].Surfaces[SurfaceIndex].Vectors.IsValidIndex(VectorIndex));

        return Components[ComponentIndex].LODs[LODIndex].Surfaces[SurfaceIndex].Vectors[VectorIndex].Value;
	}
	
	
	FName FInstance::GetVectorName(int32 ComponentIndex, int32 LODIndex, int32 SurfaceIndex, int32 VectorIndex) const
	{
		check(Components.IsValidIndex(ComponentIndex));
		check(Components[ComponentIndex].LODs.IsValidIndex(LODIndex));
        check(Components[ComponentIndex].LODs[LODIndex].Surfaces.IsValidIndex(SurfaceIndex));
		check(Components[ComponentIndex].LODs[LODIndex].Surfaces[SurfaceIndex].Vectors.IsValidIndex(VectorIndex));

        return Components[ComponentIndex].LODs[LODIndex].Surfaces[SurfaceIndex].Vectors[VectorIndex].Name;
	}
	
	
    float FInstance::GetScalar(int32 ComponentIndex, int32 LODIndex, int32 SurfaceIndex, int32 ScalarIndex ) const
	{
		check(Components.IsValidIndex(ComponentIndex));
		check(Components[ComponentIndex].LODs.IsValidIndex(LODIndex));
        check(Components[ComponentIndex].LODs[LODIndex].Surfaces.IsValidIndex(SurfaceIndex));
		check(Components[ComponentIndex].LODs[LODIndex].Surfaces[SurfaceIndex].Scalars.IsValidIndex(ScalarIndex));

        return Components[ComponentIndex].LODs[LODIndex].Surfaces[SurfaceIndex].Scalars[ScalarIndex].Value;
	}

	
	FName FInstance::GetScalarName(int32 ComponentIndex, int32 LODIndex, int32 SurfaceIndex, int32 ScalarIndex) const
	{
		check(Components.IsValidIndex(ComponentIndex));
		check(Components[ComponentIndex].LODs.IsValidIndex(LODIndex));
        check(Components[ComponentIndex].LODs[LODIndex].Surfaces.IsValidIndex(SurfaceIndex));
		check(Components[ComponentIndex].LODs[LODIndex].Surfaces[SurfaceIndex].Scalars.IsValidIndex(ScalarIndex));

        return Components[ComponentIndex].LODs[LODIndex].Surfaces[SurfaceIndex].Scalars[ScalarIndex].Name;
	}

	
    FString FInstance::GetString(int32 ComponentIndex, int32 LODIndex, int32 SurfaceIndex, int32 StringIndex ) const
    {
        check(Components.IsValidIndex(ComponentIndex));
        check(Components[ComponentIndex].LODs.IsValidIndex(LODIndex));
        check(Components[ComponentIndex].LODs[LODIndex].Surfaces.IsValidIndex(SurfaceIndex));
		check(Components[ComponentIndex].LODs[LODIndex].Surfaces[SurfaceIndex].Strings.IsValidIndex(StringIndex));

		bool bValid = Components[ComponentIndex].LODs[LODIndex].Surfaces[SurfaceIndex].Strings.IsValidIndex(StringIndex);
		if (bValid)
        {
            return Components[ComponentIndex].LODs[LODIndex].Surfaces[SurfaceIndex].Strings[StringIndex].Value;
        }

        return "";
    }
	
    
	FName FInstance::GetStringName(int32 ComponentIndex, int32 LODIndex, int32 SurfaceIndex, int32 StringIndex) const
    {
        check(Components.IsValidIndex(ComponentIndex));
        check(Components[ComponentIndex].LODs.IsValidIndex(LODIndex));
        check(Components[ComponentIndex].LODs[LODIndex].Surfaces.IsValidIndex(SurfaceIndex));

		bool bValid = Components[ComponentIndex].LODs[LODIndex].Surfaces[SurfaceIndex].Strings.IsValidIndex(StringIndex);
		if (bValid)
		{
            return Components[ComponentIndex].LODs[LODIndex].Surfaces[SurfaceIndex].Strings[StringIndex].Name;
        }

        return NAME_None;
    }

	FMaterialId FInstance::GetOverlayMaterialId(int32 ComponentIndex) const
	{
		check(Components.IsValidIndex(ComponentIndex));
		return Components[ComponentIndex].OverlayMaterialId;
	}

	FMaterialId FInstance::GetMaterialId(int32 ComponentIndex, int32 LODIndex, int32 SurfaceIndex) const
	{
		check(Components.IsValidIndex(ComponentIndex));
		check(Components[ComponentIndex].LODs.IsValidIndex(LODIndex));
		check(Components[ComponentIndex].LODs[LODIndex].Surfaces.IsValidIndex(SurfaceIndex));

		return Components[ComponentIndex].LODs[LODIndex].Surfaces[SurfaceIndex].MaterialId;
	}

    
	int32 FInstance::GetExtensionDataCount() const
	{
		return ExtensionData.Num();
	}

	
	void FInstance::GetExtensionData(int32 Index, TSharedPtr<const FExtensionData>& OutExtensionData, FName& OutName) const
	{
		check(ExtensionData.IsValidIndex(Index));

		OutExtensionData = ExtensionData[Index].Data;
		OutName = ExtensionData[Index].Name;
	}

    
	int32 FInstance::AddComponent()
	{
		int32 result = Components.Emplace();
		return result;
	}
	
    
    int32 FInstance::AddLOD( int32 ComponentIndex )
    {
        // Automatically create the necessary lods and components
        while (ComponentIndex >= Components.Num())
        {
            AddComponent();
        }

        return Components[ComponentIndex].LODs.Emplace();
    }

	
    int32 FInstance::AddSurface( int32 ComponentIndex, int32 LODIndex )
    {
        // Automatically create the necessary lods and components
        while (ComponentIndex >= Components.Num())
        {
            AddComponent();
        }
        while (LODIndex >= Components[ComponentIndex].LODs.Num())
        {
            AddLOD(ComponentIndex);
        }

        return Components[ComponentIndex].LODs[LODIndex].Surfaces.Emplace();
    }

    
    void FInstance::SetSurfaceName( int32 ComponentIndex, int32 LODIndex, int32 SurfaceIndex, FName Name)
    {
        // Automatically create the necessary lods and components
		while (ComponentIndex >= Components.Num())
		{
			AddComponent();
		}
		while (LODIndex >= Components[ComponentIndex].LODs.Num())
		{
			AddLOD(ComponentIndex);
		}
		while ( SurfaceIndex>=Components[ComponentIndex].LODs[LODIndex].Surfaces.Num() )
        {
            AddSurface( LODIndex, ComponentIndex );
        }

        FInstanceSurface& surface = Components[ComponentIndex].LODs[LODIndex].Surfaces[SurfaceIndex];
        surface.Name = Name;
    }


	//---------------------------------------------------------------------------------------------
	void FInstance::SetMesh(int32 ComponentIndex, int32 LODIndex, const FMeshId& MeshId, FName Name)
	{
		// Automatically create the necessary lods and components
		while (ComponentIndex >= Components.Num())
		{
			AddComponent();
		}
		while (LODIndex >= Components[ComponentIndex].LODs.Num())
		{
			AddLOD(ComponentIndex);
		}

		FInstanceLOD& LOD = Components[ComponentIndex].LODs[LODIndex];
		LOD.MeshId = MeshId;
		LOD.MeshName = Name;
	}


    int32 FInstance::AddImage( int32 ComponentIndex, int32 LODIndex, int32 SurfaceIndex, const FImageId& ImageId, FName Name)
	{
		// Automatically create the necessary lods and components
		while (ComponentIndex >= Components.Num())
		{
			AddComponent();
		}
		while (LODIndex >= Components[ComponentIndex].LODs.Num())
		{
			AddLOD(ComponentIndex);
		}
		while (SurfaceIndex >= Components[ComponentIndex].LODs[LODIndex].Surfaces.Num())
		{
			AddSurface(LODIndex, ComponentIndex);
		}

		FInstanceSurface& Surface = Components[ComponentIndex].LODs[LODIndex].Surfaces[SurfaceIndex];
		return Surface.Images.Add({ ImageId, Name });
	}
	
	
    int32 FInstance::AddVector( int32 ComponentIndex, int32 LODIndex, int32 SurfaceIndex, const FVector4f& vec, FName Name)
	{
		// Automatically create the necessary lods and components
		while (ComponentIndex >= Components.Num())
		{
			AddComponent();
		}
		while (LODIndex >= Components[ComponentIndex].LODs.Num())
		{
			AddLOD(ComponentIndex);
		}
		while (SurfaceIndex >= Components[ComponentIndex].LODs[LODIndex].Surfaces.Num())
		{
			AddSurface(LODIndex, ComponentIndex);
		}

		FInstanceSurface& Surface = Components[ComponentIndex].LODs[LODIndex].Surfaces[SurfaceIndex];
		return Surface.Vectors.Add({ vec, Name } );
	}

	
    int32 FInstance::AddScalar( int32 ComponentIndex, int32 LODIndex, int32 SurfaceIndex, float sca, FName Name)
    {
        // Automatically create the necessary lods and components
		while (ComponentIndex >= Components.Num())
		{
			AddComponent();
		}
		while (LODIndex >= Components[ComponentIndex].LODs.Num())
		{
			AddLOD(ComponentIndex);
		}
		while (SurfaceIndex >= Components[ComponentIndex].LODs[LODIndex].Surfaces.Num())
		{
			AddSurface(LODIndex, ComponentIndex);
		}

		FInstanceSurface& Surface = Components[ComponentIndex].LODs[LODIndex].Surfaces[SurfaceIndex];
		return Surface.Scalars.Add({ sca, Name });
    }
	
    
    int32 FInstance::AddString( int32 ComponentIndex, int32 LODIndex, int32 SurfaceIndex, const FString& Value, FName Name)
    {
        // Automatically create the necessary lods and components
		while (ComponentIndex >= Components.Num())
		{
			AddComponent();
		}
		while (LODIndex >= Components[ComponentIndex].LODs.Num())
		{
			AddLOD(ComponentIndex);
		}
		while (SurfaceIndex >= Components[ComponentIndex].LODs[LODIndex].Surfaces.Num())
		{
			AddSurface(LODIndex, ComponentIndex);
		}

		FInstanceSurface& Surface = Components[ComponentIndex].LODs[LODIndex].Surfaces[SurfaceIndex];
		return Surface.Strings.Add({ Value, Name });
    }
	

	void FInstance::SetMaterialId(int32 ComponentIndex, int32 LODIndex, int32 SurfaceIndex, const FMaterialId& MaterialId)
	{
		// Automatically create the necessary lods and components
		while (ComponentIndex >= Components.Num())
		{
			AddComponent();
		}
		while (LODIndex >= Components[ComponentIndex].LODs.Num())
		{
			AddLOD(ComponentIndex);
		}
		while (SurfaceIndex >= Components[ComponentIndex].LODs[LODIndex].Surfaces.Num())
		{
			AddSurface(LODIndex, ComponentIndex);
		}
		
		Components[ComponentIndex].LODs[LODIndex].Surfaces[SurfaceIndex].MaterialId = MaterialId;
	}


	void FInstance::SetOverlayMaterialId(int32 ComponentIndex, const FMaterialId& MaterialId)
	{
		while (ComponentIndex >= Components.Num())
		{
			AddComponent();
		}

		Components[ComponentIndex].OverlayMaterialId = MaterialId;
	}

	
	void FInstance::SetExtensionData(const TSharedRef<const FExtensionData>& Data, FName Name)
	{
		NamedExtensionData& Entry = ExtensionData.AddDefaulted_GetRef();
		Entry.Data = Data;
		Entry.Name = Name;
	}
}

