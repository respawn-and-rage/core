#include "ERFieldClothingProbe.h"
#include "ERCharacter.h"
#include "ERMovement.h"
#include "ERPostureMesh.h"
#include "ERMatch.h"
#include "ERWeapon.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "ClothingAsset.h"
#include "ClothingSystemRuntimeTypes.h"
#include "Engine/SkeletalMesh.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "PhysicsEngine/SkeletalBodySetup.h"
#include "Kismet/GameplayStatics.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/HUD.h"
#include "Engine/GameViewportClient.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Misc/Paths.h"
#include "ERMenuPreview.h"
#include "ERLoadout.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Components/SceneCaptureComponent2D.h"
#include "ImageUtils.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
namespace
{
const int32 FieldClothingProbeSkins[]={0,2,3,4,15,16,17,18,19};
constexpr int32 Cases=8;
bool AllLODs(){return FParse::Param(FCommandLine::Get(),TEXT("ERFieldClothingLODs"));}
int32 ChosenSkin(){int32 Skin=-1;FParse::Value(FCommandLine::Get(),TEXT("ERFieldClothingSkin="),Skin);return Skin;}
int32 SkinAt(int32 Sample){return ChosenSkin()>=0?ChosenSkin():FieldClothingProbeSkins[Sample/(AllLODs()?5:Cases)];}
int32 CaseAt(int32 Sample){return AllLODs()?2:Sample%Cases;}
int32 LODAt(int32 Sample){return AllLODs()?Sample%5:0;}
int32 Total(){return (ChosenSkin()>=0?1:UE_ARRAY_COUNT(FieldClothingProbeSkins))*(AllLODs()?5:Cases);}
float BodyDistance(const FVector& P,const USkeletalMeshComponent* Body,const UPhysicsAsset* Physics)
{
    float Distance=MAX_flt;
    for(auto B:Physics->SkeletalBodySetups)
    {
        const FVector Local=Body->GetSocketTransform(B->BoneName,RTS_Component).InverseTransformPosition(P);
        for(const auto& S:B->AggGeom.SphylElems)
        {
            const FVector Axis=S.Rotation.RotateVector(FVector(0,0,S.Length*.5));
            Distance=FMath::Min(Distance,float(FMath::PointDistToSegment(Local,S.Center-Axis,S.Center+Axis)-S.Radius));
        }
        for(const auto& S:B->AggGeom.SphereElems)Distance=FMath::Min(Distance,float((Local-S.Center).Length()-S.Radius));
        for(const auto& S:B->AggGeom.BoxElems)
        {
            const FVector Q=S.Rotation.UnrotateVector(Local-S.Center).GetAbs()-FVector(S.X,S.Y,S.Z)*.5;
            Distance=FMath::Min(Distance,float(Q.GetMax()));
        }
    }
    return Distance;
}

}
AERFieldClothingProbe::AERFieldClothingProbe()
{PrimaryActorTick.bCanEverTick=true;PrimaryActorTick.TickGroup=TG_PostUpdateWork;}
void AERFieldClothingProbe::Configure()
{
    const int32 Skin=SkinAt(Sample),Case=CaseAt(Sample);
    auto* State=Subject->GetPlayerState<AERPlayerState>();State->Team=Skin<10?EERTeam::Alpha:EERTeam::Omega;State->ActiveSkin=Skin%10;
    const auto& Appearance=Subject->GetTeamAppearance();auto* Body=Subject->GetMesh();
    Appearance.Apply(Body,true);Body->SetOwnerNoSee(false);Body->SetVisibility(true);Body->SetForcedLOD(1+LODAt(Sample));
    Body->bWaitForParallelClothTask=true;Body->VisibilityBasedAnimTickOption=EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
    FString Path=Appearance.CorpsePose->GetPathName();
    const TCHAR* Clip=Case==0?TEXT("MF_Rifle_Idle_ADS"):Case==3?TEXT("MF_Unarmed_Walk_Left"):Case==4?TEXT("MF_Unarmed_Walk_Bwd"):Case==7?TEXT("MM_Jump"):TEXT("MF_Unarmed_Walk_Fwd");
    Path.ReplaceInline(TEXT("MF_Rifle_Idle_ADS"),Clip);
    auto* Animation=LoadObject<UAnimSequence>(nullptr,*Path);if(!Animation){Failed=true;return;}
    Body->PlayAnimation(Animation,true);Body->SetPlayRate(Case==2?2.f:1.f);Body->ForceClothNextUpdateTeleportAndReset();
    auto* Move=CastChecked<UERMovement>(Subject->GetCharacterMovement());Move->SetComponentTickEnabled(false);
    Move->Locomotion.ProneBlend=Case==6?1.f:0.f;Move->Locomotion.HalfHeight=Case==6?34.f:Case==5?62.5f:92.5f;Move->ApplyPosturePresentation();
    Subject->bIsCrouched=Case==5;
    Subject->FirstPersonBody->SetVisibility(false);Subject->FirstPersonRifle->SetVisibility(false);
    Subject->GetController()->SetControlRotation(FRotator::ZeroRotator);
    if(auto* PC=UGameplayStatics::GetPlayerController(GetWorld(),0))
    {
        if(!Camera)Camera=GetWorld()->SpawnActor<ACameraActor>();
        const FVector Focus=Subject->GetActorLocation()+FVector(0,0,Case==6?-20:0);
        Camera->SetActorLocation(Focus+FVector(270,-210,50));Camera->SetActorRotation((Focus-Camera->GetActorLocation()).Rotation());Camera->GetCameraComponent()->SetFieldOfView(45);PC->SetViewTarget(Camera);
        if(PC->GetHUD())PC->GetHUD()->bShowHUD=false;UWidgetLayoutLibrary::RemoveAllWidgets(this);
    }
    Frames=Points=Deep=Missing=0;Worst=0;Started=Age;
    UE_LOG(LogTemp,Display,TEXT("ER_FIELD_CLOTHING_CASE skin=%d case=%d mesh=%s"),Skin,Case,*Appearance.Mesh->GetName());
}
void AERFieldClothingProbe::Measure()
{
    auto* Body=Subject->GetMesh();const auto& Data=Body->GetCurrentClothingData_GameThread();
    if(Data.IsEmpty()){++Missing;return;}
    const auto& Assets=Body->GetSkeletalMeshAsset()->GetMeshClothingAssets();
    ++Frames;
    for(const auto& Entry:Data)
    {
        if(!Assets.IsValidIndex(Entry.Key))continue;
        const auto* Cloth=Cast<UClothingAssetCommon>(Assets[Entry.Key]);if(!Cloth || !Cloth->PhysicsAsset || !Cloth->LodData.IsValidIndex(Entry.Value.LODIndex))continue;
        if(Entry.Value.LODIndex!=LODAt(Sample)){Failed=true;continue;}
        const auto* Map=Cloth->LodData[Entry.Value.LODIndex].PhysicalMeshData.FindWeightMap(EWeightMapTargetCommon::MaxDistance);if(!Map)continue;
        for(int32 I=0;I<Entry.Value.Positions.Num();++I)
        {
            if(!Map->Values.IsValidIndex(I) || Map->Values[I]<=.5f)continue;
            const FVector P=Entry.Value.ComponentRelativeTransform.TransformPosition(FVector(Entry.Value.Positions[I]));
            if(P.ContainsNaN()){Failed=true;continue;}
            const float D=BodyDistance(P,Body,Cloth->PhysicsAsset);++Points;Deep+=D < -1.f;Worst=FMath::Max(Worst,-D);
        }
    }
}
void AERFieldClothingProbe::Tick(float Delta)
{
    Super::Tick(Delta);
#if !UE_BUILD_SHIPPING
    Age+=Delta;
    if(FParse::Param(FCommandLine::Get(),TEXT("ERFieldClothingPreview"))){TickPreview();return;}
    if(AdvancePending)
    {
        AdvancePending=false;++Sample;
        if(Sample>=Total())
        {UE_LOG(LogTemp,Display,TEXT("ER_FIELD_CLOTHING_SIM_COMPLETE result=%s cases=%d"),Failed?TEXT("FAIL"):TEXT("PASS"),Sample);FPlatformMisc::RequestExitWithStatus(false,Failed?1:0);SetActorTickEnabled(false);return;}
        Configure();return;
    }
    if(!Subject)
    {
        if(Age<4)return;Subject=Cast<AERCharacter>(UGameplayStatics::GetPlayerPawn(GetWorld(),0));if(!Subject || !Subject->GetPlayerState<AERPlayerState>()){Subject=nullptr;return;}
        Subject->Health->ProtectionEndServerTime=0;
        Sample=0;Configure();return;
    }
    if(Age-Started<.2f)return;
    // A skin swap in Character::Tick may reapply the animation blueprint once.
    if(Subject->GetMesh()->GetAnimationMode()!=EAnimationMode::AnimationSingleNode){Configure();return;}
    if(Age-Started>.7f)Measure();
    if(Age-Started<2.2f)return;
    const bool Good=Frames>20 && Points>0 && Missing<5;
    Failed|=!Good;
    UE_LOG(LogTemp,Display,TEXT("ER_FIELD_CLOTHING_SAMPLE_%s skin=%d case=%d frames=%d free_points=%d deep_points=%d worst_cm=%.3f missing=%d lod=%d"),Good?TEXT("PASS"):TEXT("FAIL"),SkinAt(Sample),CaseAt(Sample),Frames,Points,Deep,Worst,Missing,LODAt(Sample));
    if(FParse::Param(FCommandLine::Get(),TEXT("ERFieldClothingGallery")))
    {const FString Suffix=AllLODs()?FString::Printf(TEXT("-lod-%d"),LODAt(Sample)):FString();FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/FString::Printf(TEXT("Testing/Clothing/skin-%d-case-%d%s.png"),SkinAt(Sample),CaseAt(Sample),*Suffix),false,false);}
    AdvancePending=true;
#endif
}

void AERFieldClothingProbe::TickPreview()
{
#if !UE_BUILD_SHIPPING
    if(Age<4)return;
    if(!Preview)
    {
        Subject=Cast<AERCharacter>(UGameplayStatics::GetPlayerPawn(GetWorld(),0));if(!Subject)return;
        Preview=GetWorld()->SpawnActor<AERMenuPreview>(FVector(0,0,-20000),FRotator::ZeroRotator);
        const int32 Selected=ChosenSkin();
        const bool Omega=Selected>=10;
        const FName LoadoutRole=Selected==18?FName("grenadier"):Selected==16?FName("rifleman"):FName("sniper");
        const auto Kit=ERLoadouts::Resolve(LoadoutRole,"gunner",Omega,Subject);
        int32 CoatStudy=0;FParse::Value(FCommandLine::Get(),TEXT("EROfficerCoatStudy="),CoatStudy);
        const int32 Skin=CoatStudy?0:Selected>=0?Selected%10:Kit.Skin;
        const auto& Appearance=Omega?Subject->OmegaSkins[Skin]:Subject->AlphaSkins[Skin];
        Preview->ShowLoadout(Appearance,Kit,Subject->ReferenceArsenal);
        UE_LOG(LogTemp,Display,TEXT("ER_CLOTH_PREVIEW_SKIN index=%d name=%s"),Skin+(Omega?10:0),*Appearance.DisplayName);
        if(CoatStudy>0)
        {
            auto* Body=Preview->Mesh.Get();
            for(int32 Slot=0;Slot<Body->GetSkeletalMeshAsset()->GetMaterials().Num();++Slot)
            {
                const FString Name=Body->GetSkeletalMeshAsset()->GetMaterials()[Slot].MaterialSlotName.ToString().ToLower();
                if(Name==TEXT("m13_cloth2") || (CoatStudy>1 && Name==TEXT("m01_cloth")))
                    for(int32 L=0;L<Body->GetSkeletalMeshAsset()->GetLODNum();++L)Body->ShowMaterialSection(Slot,INDEX_NONE,false,L);
            }
            Body->bDisableClothSimulation=true;
        }
        Preview->SetDragging(true);
        float Yaw=-25;int32 LOD=0;
        FParse::Value(FCommandLine::Get(),TEXT("ERFieldClothingPreviewYaw="),Yaw);
        FParse::Value(FCommandLine::Get(),TEXT("ERFieldClothingPreviewLOD="),LOD);
        Preview->Rotate(FMath::FindDeltaAngleDegrees(Preview->GetOrbitYaw(),Yaw));
        Preview->Mesh->SetForcedLOD(1+FMath::Clamp(LOD,0,4));
        if(FParse::Param(FCommandLine::Get(),TEXT("ERFieldClothingPreviewNoWait")))Preview->Mesh->bWaitForParallelClothTask=false;
        if(FParse::Param(FCommandLine::Get(),TEXT("ERFieldClothingPreviewWait")))Preview->Mesh->bWaitForParallelClothTask=true;
        // Resize before warm-up. The render resource changes asynchronously;
        // resizing just before ReadPixels can return the previous pixel count.
        if(FParse::Param(FCommandLine::Get(),TEXT("ERFieldClothingPreviewHead")))Preview->Target->ResizeTarget(1024,1024);
        PreviewNext=Age+2.f;return;
    }
    if(Age<PreviewNext)return;
    const bool Wait=Preview->Mesh->bWaitForParallelClothTask;
    const bool CharacterPreview=FParse::Param(FCommandLine::Get(),TEXT("ERCharacterPreview"));
    int32 CoatStudy=0;FParse::Value(FCommandLine::Get(),TEXT("EROfficerCoatStudy="),CoatStudy);
    const auto& Cloth=Preview->Mesh->GetCurrentClothingData_AnyThread();
    if(Wait && Cloth.IsEmpty() && !CoatStudy && !CharacterPreview)Failed=true;
    FString Label=CoatStudy?FString::Printf(TEXT("coat-study-%d"),CoatStudy):Wait?TEXT("wait"):TEXT("baseline");
    if(ChosenSkin()>=0)Label=FString::Printf(TEXT("skin-%d-%s"),ChosenSkin(),*Label);
    if(CharacterPreview)
    {
        Label=TEXT("character-")+Label;
        const FTransform Frame=Preview->GetActorTransform();
        const FVector Head=Frame.InverseTransformPosition(Preview->Mesh->GetSocketLocation(TEXT("head")));
        const FVector Left=Frame.InverseTransformPosition(Preview->Mesh->GetSocketLocation(TEXT("foot_l")));
        const FVector Right=Frame.InverseTransformPosition(Preview->Mesh->GetSocketLocation(TEXT("foot_r")));
        Failed|=!Preview->bPreviewAvailable || !Preview->bFramingValid || Head.ContainsNaN();
        UE_LOG(LogTemp,Display,TEXT("ER_CHARACTER_PREVIEW_MEASURE skin=%d frame=%d scale=%.5f camera_cm=%.3f head_z=%.3f foot_z=%.3f"),ChosenSkin(),PreviewFrames,Preview->Mesh->GetComponentScale().X,Preview->OrbitDistance,Head.Z,FMath::Min(Left.Z,Right.Z));
    }
    if(FParse::Param(FCommandLine::Get(),TEXT("ERFieldClothingPreviewHead")))
    {
        Label=TEXT("head-")+Label;
        const FVector Focus=Preview->Mesh->GetSocketLocation(TEXT("head"))+FVector(0,0,-4);
        const FVector View=FRotator(0,Preview->GetOrbitYaw(),0).RotateVector(FVector(115,0,4));
        Preview->Capture->SetWorldLocation(Focus+View);
        Preview->Capture->SetWorldRotation((-View).Rotation());
        Preview->Capture->FOVAngle=38;
        Preview->Capture->CaptureScene();
    }
    const FString Folder=FPaths::ProjectSavedDir()/TEXT("Testing/ClothingPreview")/FString::Printf(TEXT("%s-yaw-%d-lod-%d"),*Label,FMath::RoundToInt(Preview->GetOrbitYaw()),Preview->Mesh->GetForcedLOD()-1);
    IFileManager::Get().MakeDirectory(*Folder,true);
    TArray<FColor> Pixels;auto* Resource=Preview->Target->GameThread_GetRenderTargetResource();
    if(!Resource->ReadPixels(Pixels)){Failed=true;return;}
    if(int64(Pixels.Num())!=int64(Preview->Target->SizeX)*Preview->Target->SizeY)
    {
        UE_LOG(LogTemp,Warning,TEXT("ER_CLOTH_PREVIEW_TARGET_PENDING pixels=%d expected=%d"),Pixels.Num(),Preview->Target->SizeX*Preview->Target->SizeY);
        PreviewNext=Age+.1f;return;
    }
    TArray64<uint8> PNG;FImageUtils::PNGCompressImageArray(Preview->Target->SizeX,Preview->Target->SizeY,Pixels,PNG);
    const FString File=Folder/FString::Printf(TEXT("frame-%02d.png"),PreviewFrames);
    if(!FFileHelper::SaveArrayToFile(PNG,*File))Failed=true;
    UE_LOG(LogTemp,Display,TEXT("ER_CLOTH_PREVIEW_FRAME frame=%d wait=%d sim_assets=%d time=%.3f"),PreviewFrames,Wait,Cloth.Num(),Preview->Mesh->GetPosition());
    PreviewNext=Age+.1f;
    if(++PreviewFrames>=36)
    {UE_LOG(LogTemp,Display,TEXT("ER_CLOTH_PREVIEW_COMPLETE result=%s"),Failed?TEXT("FAIL"):TEXT("PASS"));FPlatformMisc::RequestExitWithStatus(false,Failed?1:0);SetActorTickEnabled(false);}
#endif
}
