#include "ERHitFidelityProbe.h"
#include "ERCharacter.h"
#include "ERMatch.h"
#include "ERMovement.h"
#include "ERPostureMesh.h"
#include "ERWeapon.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "PhysicsEngine/BodyInstance.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "StaticMeshResources.h"
namespace
{
constexpr float HitProbeCellSize=2.f;
constexpr int32 HitProbeWidth=170,HitProbeHeight=120;
const int32 HitProbeSkins[]={0,3,5,10,15};
const int32 HitProbeSlots[]={0,4,6};
constexpr int32 HitProbeSkinCount=UE_ARRAY_COUNT(HitProbeSkins),HitProbeSlotCount=UE_ARRAY_COUNT(HitProbeSlots),HitProbePostureCount=3;
// The last skin rows repeat the rifle stance while running.
constexpr int32 HitProbeStillCount=HitProbeSkinCount*HitProbeSlotCount*HitProbePostureCount,HitProbeSampleCount=HitProbeStillCount+HitProbeSkinCount;
bool LagMode(){return FParse::Param(FCommandLine::Get(),TEXT("ERHitLagProbe"));}
int32 SkinOf(int32 Sample){return Sample>=HitProbeStillCount ? HitProbeSkins[Sample-HitProbeStillCount]:HitProbeSkins[Sample/(HitProbeSlotCount*HitProbePostureCount)];}
int32 SlotOf(int32 Sample){return Sample>=HitProbeStillCount ? 4:HitProbeSlots[(Sample/HitProbePostureCount)%HitProbeSlotCount];}
int32 PostureOf(int32 Sample){return Sample>=HitProbeStillCount ? 0:Sample%HitProbePostureCount;}
const TCHAR* PostureName(int32 Posture){return Posture==2 ? TEXT("prone"):Posture==1 ? TEXT("crouch"):TEXT("stand");}
void Dilate(TArray<bool>& Mask)
{
    const TArray<bool> Old=Mask;
    for(int32 Y=1;Y<HitProbeHeight-1;++Y)for(int32 X=1;X<HitProbeWidth-1;++X)
        if(Old[Y*HitProbeWidth+X-1] || Old[Y*HitProbeWidth+X+1] || Old[(Y-1)*HitProbeWidth+X] || Old[(Y+1)*HitProbeWidth+X])Mask[Y*HitProbeWidth+X]=true;
}
}
AERHitFidelityProbe::AERHitFidelityProbe()
{
    PrimaryActorTick.bCanEverTick=true;
    // Lag samples fire where input fires: after the controller, before the move is sent.
    PrimaryActorTick.TickGroup=LagMode() ? TG_PrePhysics:TG_PostUpdateWork;
    bReplicates=true;bAlwaysRelevant=true;
}
void AERHitFidelityProbe::BeginPlay()
{
    Super::BeginPlay();
    if(LagMode() && GetWorld()->GetFirstPlayerController())AddTickPrerequisiteActor(GetWorld()->GetFirstPlayerController());
}
bool AERHitFidelityProbe::Moving() const {return Sample>=HitProbeStillCount;}
void AERHitFidelityProbe::Tick(float Delta)
{
    Super::Tick(Delta);Age+=Delta;
    if(!LagMode()){TickFidelity();return;}
    if(GetNetMode()==NM_Client)TickLagClient();else TickLagHost();
}
void AERHitFidelityProbe::Configure()
{
    auto* State=Subject->GetPlayerState<AERPlayerState>();const int32 Skin=SkinOf(Sample);
    State->Team=Skin<10 ? EERTeam::Alpha:EERTeam::Omega;State->ActiveSkin=Skin%10;State->ForceNetUpdate();
    Subject->ServerSelectWeapon(SlotOf(Sample));
    auto* M=Cast<UERMovement>(Subject->GetCharacterMovement());
    const int32 Posture=PostureOf(Sample);
    M->bProneWanted=Posture==2;
    if(Posture==1)Subject->Crouch();else Subject->UnCrouch();
    if(Subject->GetController())Subject->GetController()->SetControlRotation(FRotator::ZeroRotator);
    Changed=Age;
}
void AERHitFidelityProbe::TickFidelity()
{
    if(!Ready)
    {
        auto* PC=UGameplayStatics::GetPlayerController(GetWorld(),0);
        Subject=PC ? Cast<AERCharacter>(PC->GetPawn()):nullptr;
        if(!Subject || !Subject->Weapon || Age<5)return;
        Subject->GetMesh()->ClothBlendWeight=0;
        Ready=true;Sample=0;Configure();return;
    }
    if(Moving())
    {
        if(Age-NextShot>1.2f){NextShot=Age;MoveSign=-MoveSign;}
        Subject->AddMovementInput(FVector::YAxisVector,MoveSign);
    }
    if(Age-Changed<(PostureOf(Sample)==2 ? 2.5f:1.6f))return;
    Measure();
    if(++Sample>=HitProbeSampleCount)
    {
        UE_LOG(LogTemp,Display,TEXT("ER_HIT_FIDELITY_COMPLETE samples=%d"),HitProbeSampleCount);
        FPlatformMisc::RequestExitWithStatus(false,0);return;
    }
    Configure();
}
void AERHitFidelityProbe::Measure()
{
    auto* Body=Subject->GetMesh();const auto* Asset=Body->GetSkeletalMeshAsset();
    const auto& LOD=Asset->GetResourceForRendering()->LODRenderData[0];
    TArray<FMatrix44f> RefToLocals;Body->CacheRefToLocalMatrices(RefToLocals);
    const auto& Ref=Asset->GetRefSkeleton();
    // Hidden props render collapsed, so they are not part of the visible figure.
    for(const FName Bone:Subject->GetTeamAppearance().HiddenBones)
    {
        const int32 Root=Ref.FindBoneIndex(Bone);if(Root==INDEX_NONE)continue;
        for(int32 I=0;I<Ref.GetNum();++I)if(I==Root || Ref.BoneIsChildOf(I,Root))RefToLocals[I].SetIdentity(),RefToLocals[I].M[0][0]=RefToLocals[I].M[1][1]=RefToLocals[I].M[2][2]=0;
    }
    TArray<FVector3f> Local;
    USkeletalMeshComponent::ComputeSkinnedPositions(Body,Local,RefToLocals,LOD,*Body->GetSkinWeightBuffer(0));
    if(Local.IsEmpty()){UE_LOG(LogTemp,Error,TEXT("ER_HIT_FIDELITY_FAIL sample=%d no skinned positions"),Sample);return;}
    TArray<FVector> World;World.SetNumUninitialized(Local.Num());
    const FTransform Frame=Body->GetComponentTransform();
    TArray<bool> Collapsed;Collapsed.SetNumZeroed(Local.Num());
    for(int32 I=0;I<Local.Num();++I){Collapsed[I]=Local[I].IsNearlyZero(.001f);World[I]=Frame.TransformPosition(FVector(Local[I]));}
    TArray<uint32> Indices;LOD.MultiSizeIndexContainer.GetIndexBuffer(Indices);
    TArray<uint32> Triangles;double Lowest=DBL_MAX;
    TArray<USceneComponent*> Parts;Body->GetChildrenComponents(false,Parts);
    for(const auto& Section:LOD.RenderSections)
    {
        if(!Body->IsMaterialSectionShown(Section.MaterialIndex,0))continue;
        for(uint32 T=Section.BaseIndex;T<Section.BaseIndex+Section.NumTriangles*3;T+=3)
        {
            const uint32 A=Indices[T],B=Indices[T+1],C=Indices[T+2];
            if(Collapsed[A] || Collapsed[B] || Collapsed[C])continue;
            Triangles.Add(A);Triangles.Add(B);Triangles.Add(C);
            Lowest=FMath::Min3(Lowest,FMath::Min(World[A].Z,World[B].Z),World[C].Z);
        }
    }
    // Replacement heads are static gear on the head bone, not skeletal sections.
    for(const auto* Part:Parts)
    {
        const auto* Gear=Cast<UStaticMeshComponent>(Part);
        if(!Gear || !Gear->ComponentHasTag(TEXT("ERSkinGear")) || !Gear->GetStaticMesh())continue;
        const auto& Resource=Gear->GetStaticMesh()->GetRenderData()->LODResources[0];
        const auto& Points=Resource.VertexBuffers.PositionVertexBuffer;
        if(!Points.GetVertexData())continue;
        const int32 First=World.Num();
        for(uint32 I=0;I<Points.GetNumVertices();++I)World.Add(Gear->GetComponentTransform().TransformPosition(FVector(Points.VertexPosition(I))));
        TArray<uint32> GearIndices;Resource.IndexBuffer.GetCopy(GearIndices);
        for(const uint32 I:GearIndices)Triangles.Add(First+I);
    }
    const float HalfHeight=Subject->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
    const FVector Actor=Subject->GetActorLocation(),Bottom=Actor-FVector(0,0,HalfHeight);
    FHitResult Floor;FCollisionQueryParams Query(SCENE_QUERY_STAT(ERHitFidelity),false,Subject);
    const bool bFloor=GetWorld()->LineTraceSingleByChannel(Floor,Actor,Actor-FVector(0,0,500),ECC_Visibility,Query);
    const auto* M=Subject->GetCharacterMovement();
    const double FloorZ=bFloor ? Floor.ImpactPoint.Z:0;
    if(PostureOf(Sample)==2)
    {
        FVector LowestPoint=FVector::ZeroVector;double LowestZ=DBL_MAX;
        for(uint32 Vertex : Triangles)if(World[Vertex].Z<LowestZ){LowestZ=World[Vertex].Z;LowestPoint=World[Vertex];}
        FName NearestBone;double NearestDistance=DBL_MAX;
        for(int32 Bone=0;Bone<Ref.GetNum();++Bone)
        {
            const double Distance=FVector::DistSquared(Body->GetBoneTransform(Bone).GetLocation(),LowestPoint);
            if(Distance<NearestDistance){NearestDistance=Distance;NearestBone=Ref.GetBoneName(Bone);}
        }
        UE_LOG(LogTemp,Display,TEXT("ER_PRONE_VERTEX_FLOOR sample=%d gap=%.3f nearest=%s point=%s"),Sample,LowestZ-FloorZ,*NearestBone.ToString(),*LowestPoint.ToString());
    }
    float MaxCm=0,MaxDeg=0;FName WorstBone;
    for(const auto* Instance:Body->Bodies)
    {
        if(!Instance || !Instance->IsValidBodyInstance())continue;
        const FTransform Physics=Instance->GetUnrealWorldTransform();
        const FTransform Bone=Body->GetBoneTransform(Instance->InstanceBoneIndex);
        const float Cm=FVector::Distance(Physics.GetLocation(),Bone.GetLocation());
        const float Deg=FMath::RadiansToDegrees(Physics.GetRotation().AngularDistance(Bone.GetRotation()));
        if(Cm>MaxCm){MaxCm=Cm;WorstBone=Ref.GetBoneName(Instance->InstanceBoneIndex);}
        MaxDeg=FMath::Max(MaxDeg,Deg);
    }
    if(Sample==0)
    {
        FERShotHistory Synthetic;FERShotPose Old;Old.Time=1;
        for(const auto* Instance:Body->Bodies)
        {
            FTransform Transform=Instance && Instance->IsValidBodyInstance() ? Instance->GetUnrealWorldTransform():FTransform::Identity;
            Transform.AddToTranslation(FVector(0,300,0));Old.Bodies.Add(Transform);
        }
        Synthetic.Append(Old.Time)=Old;
        const FVector Original=Body->Bodies[0]->GetUnrealWorldTransform().GetLocation();
        const FVector Center=Original+FVector(0,300,0);
        FHitResult HistoricalHit,CurrentHit,ShortHit;
        const bool Rewound=Synthetic.Trace(Body,HistoricalHit,Center-FVector(500,0,0),Center+FVector(500,0,0),Query,1,0);
        const bool Current=Body->LineTraceComponent(CurrentHit,Center-FVector(500,0,0),Center+FVector(500,0,0),Query);
        const bool Short=Synthetic.Trace(Body,ShortHit,Center-FVector(500,0,0),Center-FVector(200,0,0),Query,1,FERShotHistory::EdgeTolerance);
        const bool Unchanged=Body->Bodies[0]->GetUnrealWorldTransform().GetLocation().Equals(Original,.001);
        const FVector OldContact=HistoricalHit.ImpactPoint;
        Synthetic.ProjectToCurrent(Body,HistoricalHit,1);
        const bool Wound=HistoricalHit.ImpactPoint.Equals(OldContact-FVector(0,300,0),.001);
        TArray<FTransform> Prepared;Synthetic.Sample(1,Prepared);FHitResult CachedHit;
        const bool Cached=Synthetic.TracePose(Body,CachedHit,Center-FVector(500,0,0),Center+FVector(500,0,0),Query,&Prepared,0);
        const bool Equivalent=Cached && CachedHit.ImpactPoint.Equals(OldContact,.001) && CachedHit.BoneName==HistoricalHit.BoneName;
        const bool Valid=Rewound && !Current && !Short && Unchanged && Wound && Equivalent && HistoricalHit.BoneName!=NAME_None;
        UE_LOG(LogTemp,Display,TEXT("ER_HIT_HISTORY_QUERY result=%s rewound=%d current=%d short=%d unchanged=%d wound=%d bone=%s"),
            Valid ? TEXT("PASS"):TEXT("FAIL"),Rewound,Current,Short,Unchanged,Wound,*HistoricalHit.BoneName.ToString());
        if(!Valid){FPlatformMisc::RequestExitWithStatus(false,1);return;}
    }
    const int32 Skin=SkinOf(Sample),Slot=SlotOf(Sample),Posture=PostureOf(Sample);
    UE_LOG(LogTemp,Display,TEXT("ER_HIT_FIDELITY_POSE sample=%d skin=%d slot=%d posture=%s moving=%d speed=%.0f body_max_cm=%.2f body_max_deg=%.2f worst_bone=%s capsule_gap=%.2f floor_dist=%.2f sole_gap=%.2f mesh_z=%.2f"),
        Sample,Skin,Slot,PostureName(Posture),Moving(),M->Velocity.Size2D(),MaxCm,MaxDeg,*WorstBone.ToString(),
        Bottom.Z-FloorZ,M->CurrentFloor.FloorDist,Lowest-FloorZ,Body->GetRelativeLocation().Z);
    const bool bImage=Slot==4;
    const FVector Feet(Actor.X,Actor.Y,bFloor ? FloorZ:Bottom.Z);
    MeasureView(World,Triangles,FVector::YAxisVector,TEXT("front"),Feet,bImage);
    MeasureView(World,Triangles,FVector::XAxisVector,TEXT("side"),Feet,bImage);
}
void AERHitFidelityProbe::MeasureView(const TArray<FVector>& World,const TArray<uint32>& Triangles,FVector Across,const TCHAR* ViewName,const FVector& Feet,bool bImage)
{
    auto* Body=Subject->GetMesh();
    const FVector Along=FVector::CrossProduct(Across,FVector::UpVector);
    const auto ToGrid=[&](const FVector& P){return FVector2D((FVector::DotProduct(P-Feet,Across)+HitProbeWidth*HitProbeCellSize*.5)/HitProbeCellSize,((P-Feet).Z+20)/HitProbeCellSize);};
    const auto Index=[](int32 X,int32 Y){return Y*HitProbeWidth+X;};
    TArray<bool> Visible,Hit;Visible.SetNumZeroed(HitProbeWidth*HitProbeHeight);Hit.SetNumZeroed(HitProbeWidth*HitProbeHeight);
    for(int32 T=0;T<Triangles.Num();T+=3)
    {
        const FVector2D A=ToGrid(World[Triangles[T]]),B=ToGrid(World[Triangles[T+1]]),C=ToGrid(World[Triangles[T+2]]);
        const double Area=(B.X-A.X)*(C.Y-A.Y)-(B.Y-A.Y)*(C.X-A.X);if(FMath::IsNearlyZero(Area))continue;
        const int32 X0=FMath::Max(0,FMath::FloorToInt32(FMath::Min3(A.X,B.X,C.X))),X1=FMath::Min(HitProbeWidth-1,FMath::CeilToInt32(FMath::Max3(A.X,B.X,C.X)));
        const int32 Y0=FMath::Max(0,FMath::FloorToInt32(FMath::Min3(A.Y,B.Y,C.Y))),Y1=FMath::Min(HitProbeHeight-1,FMath::CeilToInt32(FMath::Max3(A.Y,B.Y,C.Y)));
        for(int32 Y=Y0;Y<=Y1;++Y)for(int32 X=X0;X<=X1;++X)
        {
            const double PX=X+.5,PY=Y+.5;
            const double U=((B.X-PX)*(C.Y-PY)-(B.Y-PY)*(C.X-PX))/Area,V=((C.X-PX)*(A.Y-PY)-(C.Y-PY)*(A.X-PX))/Area;
            if(U>=0 && V>=0 && U+V<=1)Visible[Index(X,Y)]=true;
        }
    }
    int32 Both=0,VisibleOnly=0,HitOnly=0,FeetVisible=0,FeetMissed=0;
    int32 Margin3Missed=0,Margin3Phantom=0,Margin5Missed=0,Margin5Phantom=0;
    FCollisionQueryParams Query(SCENE_QUERY_STAT(ERHitFidelityRay),false);
    for(int32 Y=0;Y<HitProbeHeight;++Y)for(int32 X=0;X<HitProbeWidth;++X)
    {
        const FVector Point=Feet+Across*((X+.5)*HitProbeCellSize-HitProbeWidth*HitProbeCellSize*.5)+FVector(0,0,(Y+.5)*HitProbeCellSize-20);
        FHitResult Ray;
        Hit[Index(X,Y)]=CastChecked<UERPostureMesh>(Body)->ShotHistory.Trace(Body,Ray,Point+Along*500,Point-Along*500,Query,
            GetWorld()->GetTimeSeconds(),FERShotHistory::EdgeTolerance);
        const bool V=Visible[Index(X,Y)],H=Hit[Index(X,Y)];
        Both+=V && H;VisibleOnly+=V && !H;HitOnly+=H && !V;
        if(V && Y*HitProbeCellSize-20<20){++FeetVisible;FeetMissed+=!H;}
        FHitResult Sweep;
        const bool Near3=H || Body->SweepComponent(Sweep,Point+Along*500,Point-Along*500,FQuat::Identity,FCollisionShape::MakeSphere(3.f));
        const bool Near5=Near3 || Body->SweepComponent(Sweep,Point+Along*500,Point-Along*500,FQuat::Identity,FCollisionShape::MakeSphere(5.f));
        Margin3Missed+=V && !Near3;Margin3Phantom+=Near3 && !V;Margin5Missed+=V && !Near5;Margin5Phantom+=Near5 && !V;
    }
    // A band of 3 cells (6cm) separates honest capsule fit from a shifted hit volume.
    TArray<bool> NearHit=Hit,NearVisible=Visible;
    for(int32 I=0;I<3;++I){Dilate(NearHit);Dilate(NearVisible);}
    int32 DeepMiss=0,DeepPhantom=0;
    for(int32 I=0;I<HitProbeWidth*HitProbeHeight;++I){DeepMiss+=Visible[I] && !NearHit[I];DeepPhantom+=Hit[I] && !NearVisible[I];}
    const int32 Shown=FMath::Max(1,Both+VisibleOnly);
    const int32 Hits=FMath::Max(1,Both+HitOnly);
    UE_LOG(LogTemp,Display,TEXT("ER_HIT_FIDELITY_VIEW sample=%d skin=%d slot=%d posture=%s moving=%d view=%s visible_cells=%d hit_cells=%d missed_pct=%.1f phantom_pct=%.1f deep_miss_pct=%.1f deep_phantom_pct=%.1f feet_visible=%d feet_missed_pct=%.1f margin3_missed=%.1f margin3_phantom=%.1f margin5_missed=%.1f margin5_phantom=%.1f"),
        Sample,SkinOf(Sample),SlotOf(Sample),PostureName(PostureOf(Sample)),Moving(),ViewName,Both+VisibleOnly,Both+HitOnly,
        100.f*VisibleOnly/Shown,100.f*HitOnly/Hits,100.f*DeepMiss/Shown,100.f*DeepPhantom/Hits,FeetVisible,100.f*FeetMissed/FMath::Max(1,FeetVisible),
        100.f*Margin3Missed/Shown,100.f*Margin3Phantom/Shown,100.f*Margin5Missed/Shown,100.f*Margin5Phantom/Shown);
    if(!bImage)return;
    TArray<FColor> Pixels;Pixels.SetNumUninitialized(HitProbeWidth*HitProbeHeight);
    for(int32 Y=0;Y<HitProbeHeight;++Y)for(int32 X=0;X<HitProbeWidth;++X)
    {
        const bool V=Visible[Index(X,HitProbeHeight-1-Y)],H=Hit[Index(X,HitProbeHeight-1-Y)];
        Pixels[Index(X,Y)]=V && H ? FColor(150,150,150):V ? FColor(230,60,50):H ? FColor(40,110,240):FColor(250,250,250);
    }
    const FString Path=FPaths::ProjectSavedDir()/FString::Printf(TEXT("Testing/HitFidelity/skin%02d-%s-%s-%s.bmp"),SkinOf(Sample),PostureName(PostureOf(Sample)),Moving() ? TEXT("moving"):TEXT("still"),ViewName);
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path),true);
    FFileHelper::CreateBitmap(*Path,HitProbeWidth,HitProbeHeight,Pixels.GetData());
}
void AERHitFidelityProbe::TickLagHost()
{
    if(!Ready)
    {
        int32 Players=0;for(auto It=GetWorld()->GetPlayerControllerIterator();It;++It)++Players;
        auto* PC=UGameplayStatics::GetPlayerController(GetWorld(),0);
        Subject=PC ? Cast<AERCharacter>(PC->GetPawn()):nullptr;
        if(Players<2 || !Subject || Age<5)return;
        for(TActorIterator<AERCharacter> It(GetWorld());It;++It)if(*It!=Subject)Shooter=*It;
        const auto* State=GetWorld()->GetGameState<AERGameState>();
        if(!Shooter || !Subject->bNetworkReady || !Shooter->bNetworkReady || !State || !State->IsMatchInProgress())return;
        Shooter->ServerSelectWeapon(4);
        Ready=true;Changed=Age;return;
    }
    if(Age-NextShot>1.4f){NextShot=Age;MoveSign=-MoveSign;}
    Subject->AddMovementInput(FVector::YAxisVector,MoveSign);
    if(Shooter->Weapon)Shooter->Weapon->MagazineAmmo=FMath::Max(Shooter->Weapon->MagazineAmmo,5);
    if(Age-Changed>32)
    {
        UE_LOG(LogTemp,Display,TEXT("ER_HIT_LAG_COMPLETE"));
        FPlatformMisc::RequestExitWithStatus(false,0);
    }
}
void AERHitFidelityProbe::TickLagClient()
{
    auto* PC=GetWorld()->GetFirstPlayerController();
    auto* Me=PC ? Cast<AERCharacter>(PC->GetPawn()):nullptr;
    const auto* State=GetWorld()->GetGameState<AERGameState>();
    if(!Me || !Me->bNetworkReady || !Me->Weapon || !Me->Weapon->Data ||
        Me->Weapon->Data->Payload!=EERPayload::Bullet || !State || !State->IsMatchInProgress() || Age<9)return;
    if(FParse::Param(FCommandLine::Get(),TEXT("ERFireInputProbe")) && !Me->Weapon->Tuned()->bAutomatic)return;
    if(!Ready)
    {
        for(TActorIterator<AERCharacter> It(GetWorld());It;++It)if(*It!=Me)Subject=*It;
        if(!Subject)return;
        Me->GetCharacterMovement()->PrimaryComponentTick.AddPrerequisite(this,PrimaryActorTick);
        Ready=true;NextShot=Age+1;
    }
    if(FParse::Param(FCommandLine::Get(),TEXT("ERFireInputProbe"))){TickFireInputClient(Me);return;}
    // Steady tracking: a real shooter's view keeps turning while the trigger breaks.
    const float Rate=120.f;
    FRotator View=PC->GetControlRotation();View.Pitch=0;View.Yaw=FRotator::NormalizeAxis(View.Yaw+Rate*GetWorld()->GetDeltaSeconds());
    PC->SetControlRotation(View);
    if(Age<NextShot)return;
    NextShot=Age+.75f;
    const FVector Seen=Subject->GetMesh()->GetBoneLocation(TEXT("spine_03"));
    UE_LOG(LogTemp,Display,TEXT("ER_HIT_LAG_CLIENT shot=%d target=%s yaw=%.3f dt=%.4f"),Shots++,*Seen.ToCompactString(),View.Yaw,GetWorld()->GetDeltaSeconds());
    Me->SetFireInputHeld(true);Me->SetFireInputHeld(false);
}
void AERHitFidelityProbe::TickFireInputClient(AERCharacter* Me)
{
    if(FireInputPhase>=6 || Age<NextShot)return;
    auto Check=[this](bool Pass,const TCHAR* Label)
    {
        FireInputFailed|=!Pass;
        UE_LOG(LogTemp,Display,TEXT("ER_FIRE_INPUT_%s %s"),Pass?TEXT("PASS"):TEXT("FAIL"),Label);
    };
    const auto& Ack=Me->FireAcknowledgement;
    switch(FireInputPhase)
    {
    case 0:
    {
        // Shoot away from the moving target: this probe checks transport and
        // presentation, without a death/respawn obscuring release ordering.
        Me->GetController()->SetControlRotation(FRotator(70,0,0));
        const int32 Ammo=Me->Weapon->MagazineAmmo;
        const uint32 Presented=Me->PredictedShotFeedbackCount;
        Me->SetFireInputHeld(true);
        FireInputSequence=CastChecked<UERMovement>(Me->GetCharacterMovement())->FireInput.Sequence;
        Check(Me->PredictedShotFeedbackCount==Presented+1,TEXT("immediate_owner_feedback_before_ack"));
        Check(Me->Weapon->MagazineAmmo==Ammo,TEXT("prediction_never_mutates_ammo"));
        NextShot=Age+.7f;break;
    }
    case 1:
        Me->SetFireInputHeld(false);NextShot=Age+1.5f;break;
    case 2:
        Check(Ack.Sequence==FireInputSequence && !Ack.Firing && Ack.ShotOrdinal>=2,TEXT("automatic_burst_and_release_recovered"));
        ReleasedShotCount=Ack.ShotOrdinal;NextShot=Age+1;break;
    case 3:
        Check(Ack.Sequence==FireInputSequence && !Ack.Firing && Ack.ShotOrdinal==ReleasedShotCount,TEXT("release_does_not_restart_on_later_moves"));
        Me->SetFireInputHeld(true);Me->SetFireInputHeld(false);
        FireInputSequence=CastChecked<UERMovement>(Me->GetCharacterMovement())->FireInput.Sequence;
        NextShot=Age+1.5f;break;
    case 4:
        Check(Ack.Sequence==FireInputSequence && !Ack.Firing && Ack.ShotOrdinal==1,TEXT("same_frame_tap_is_exactly_one_shot"));
        NextShot=Age+1;break;
    case 5:
        Check(Ack.Sequence==FireInputSequence && !Ack.Firing && Ack.ShotOrdinal==1,TEXT("tap_retransmission_is_idempotent"));
        UE_LOG(LogTemp,Display,TEXT("ER_FIRE_INPUT_COMPLETE result=%s"),FireInputFailed?TEXT("FAIL"):TEXT("PASS"));
        break;
    }
    ++FireInputPhase;
}
