#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "ERMapTheme.generated.h"

// One shared, explicit set of owned derivatives of the reviewed Twinmotion finishes.
UCLASS(BlueprintType)
class FDM_API UERMapSurfaceLibrary : public UDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 Revision=1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TArray<TObjectPtr<class UMaterialInterface>> Surfaces;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TArray<FString> SourcePackages;
    bool IsValidLibrary() const;
    bool Contains(const class UMaterialInterface* Surface) const;
    // Migration support for non-reflected editor caches on owned derivatives.
    UFUNCTION(BlueprintCallable, Category="Materials") static bool RefreshOwnedMaterial(class UMaterialInterface* Surface);
    UFUNCTION(BlueprintCallable, Category="Materials") static bool RestoreOwnedFunctionInputs(UObject* Source, UObject* Owned);
};

// Visual roles only. Their order follows EERBlockMaterial; physics and HP retain
// the construction's original material regardless of the selected finish.
USTRUCT(BlueprintType)
struct FERMapThemePalette
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FName Name;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TArray<TObjectPtr<class UMaterialInterface>> Materials;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TObjectPtr<class UMaterialInterface> Wall;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TObjectPtr<class UMaterialInterface> Floor;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TObjectPtr<class UMaterialInterface> Ceiling;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TArray<TObjectPtr<class UMaterialInterface>> Grounds;
    // Optional coordinated steel finishes. Empty keeps the original palette.
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TObjectPtr<class UMaterialInterface> ShellSteel;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TObjectPtr<class UMaterialInterface> LadderSteel;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TObjectPtr<class UMaterialInterface> DeckSteel;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TObjectPtr<class UMaterialInterface> CargoSteel;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) bool PreserveCargoPaint=false;
};

UCLASS(BlueprintType)
class FDM_API UERMapThemeLibrary : public UDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 Revision=1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) bool RequireSharedSurfaces=false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TObjectPtr<UERMapSurfaceLibrary> SharedSurfaces;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TArray<FERMapThemePalette> Themes;
};
