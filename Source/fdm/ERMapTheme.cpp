#include "ERMapTheme.h"
#include "Materials/MaterialInterface.h"
#include "Materials/Material.h"
#include "Materials/MaterialFunction.h"
#include "Materials/MaterialExpressionMaterialFunctionCall.h"

bool UERMapSurfaceLibrary::RestoreOwnedFunctionInputs(UObject* Source, UObject* Owned)
{
#if WITH_EDITOR
    if(!Source || !Owned || !Source->GetPathName().StartsWith(TEXT("/Game/TwinmotionMaterials/")) ||
       !Owned->GetPathName().StartsWith(TEXT("/Game/EclipseRecoil/Art/SharedMaterials/")))return false;
    auto Expressions=[](UObject* Object)
    {
        if(auto* Material=Cast<UMaterial>(Object))return TArray<UMaterialExpression*>(Material->GetExpressions());
        if(auto* Function=Cast<UMaterialFunction>(Object))return TArray<UMaterialExpression*>(Function->GetExpressions());
        return TArray<UMaterialExpression*>();
    };
    const auto Original=Expressions(Source),Copies=Expressions(Owned);
    if(Original.IsEmpty() || Original.Num()!=Copies.Num())return false;
    TMap<FName,UMaterialExpression*> ByName;
    for(auto* Node:Copies)ByName.Add(Node->GetFName(),Node);
    // SetMaterialFunction rebuilds its pins, including unchanged engine calls.
    // Restore each edge with its exact output index and channel mask.
    for(auto* Node:Original)if(Node->IsA<UMaterialExpressionMaterialFunctionCall>())
    {
        auto* const* Found=ByName.Find(Node->GetFName());if(!Found)return false;
        auto* Copy=*Found;
        for(int32 Index=0;;++Index)
        {
            const auto* Input=Node->GetInput(Index);auto* Target=Copy->GetInput(Index);
            if(!Input){if(Target)return false;break;}
            if(!Target || Node->GetInputName(Index)!=Copy->GetInputName(Index))return false;
            UMaterialExpression* Remapped=nullptr;
            if(Input->Expression)
            {
                auto* const* Match=ByName.Find(Input->Expression->GetFName());if(!Match)return false;
                Remapped=*Match;
            }
            *Target=*Input;Target->Expression=Remapped;
        }
    }
    Owned->MarkPackageDirty();return true;
#else
    return false;
#endif
}

bool UERMapSurfaceLibrary::RefreshOwnedMaterial(UMaterialInterface* Surface)
{
#if WITH_EDITOR
    if(!Surface || !Surface->GetPathName().StartsWith(TEXT("/Game/EclipseRecoil/Art/SharedMaterials/")))return false;
    Surface->GetTextureStreamingData().Reset();
    if(auto* Material=Cast<UMaterial>(Surface))Material->UpdateCachedExpressionData();
    Surface->MarkPackageDirty();
    return true;
#else
    return false;
#endif
}

bool UERMapSurfaceLibrary::Contains(const UMaterialInterface* Surface) const
{
    return Surface && Surfaces.ContainsByPredicate([&](const auto& Entry){return Entry.Get()==Surface;});
}

bool UERMapSurfaceLibrary::IsValidLibrary() const
{
    if(Revision<1 || Surfaces.IsEmpty() || Surfaces.Num()!=SourcePackages.Num())return false;
    TSet<const UMaterialInterface*> Unique;
    TSet<FString> Sources;
    for(int32 Index=0;Index<Surfaces.Num();++Index)
    {
        const auto* Surface=Surfaces[Index].Get();
        if(!Surface || Unique.Contains(Surface) || Sources.Contains(SourcePackages[Index]) ||
           !Surface->GetPathName().StartsWith(TEXT("/Game/EclipseRecoil/Art/SharedMaterials/")) ||
           !SourcePackages[Index].StartsWith(TEXT("/Game/TwinmotionMaterials/Materials/")))return false;
        Unique.Add(Surface);Sources.Add(SourcePackages[Index]);
    }
    return true;
}
