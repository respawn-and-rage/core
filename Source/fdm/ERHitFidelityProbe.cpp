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
constexpr float Cell=2.f;
constexpr int32 Width=170,Height=120;
const int32 Skins[]={0,3,5,10,15};
const int32 Slots[]={0,4,6};
constexpr int32 SkinCount=UE_ARRAY_COUNT(Skins),SlotCount=UE_ARRAY_COUNT(Slots),PostureCount=3;
// The last skin rows repeat the rifle stance while running.
constexpr int32 Still=SkinCount*SlotCount*PostureCount,Total=Still+SkinCount;
bool LagMode(){return FParse::Param(FCommandLine::Get(),TEXT("ERHitLagProbe"));}
int32 SkinOf(int32 Sample){return Sample>=Still ? Skins[Sample-Still]:Skins[Sample/(SlotCount*PostureCount)];}
int32 SlotOf(int32 Sample){return Sample>=Still ? 4:Slots[(Sample/PostureCount)%SlotCount];}
int32 PostureOf(int32 Sample){return Sample>=Still ? 0:Sample%PostureCount;}
const TCHAR* PostureName(int32 Posture){return Posture==2 ? TEXT("prone"):Posture==1 ? TEXT("crouch"):TEXT("stand");}
void Dilate(TArray<bool>& Mask)
{
    const TArray<bool> Old=Mask;
    for(int32 Y=1;Y<Height-1;++Y)for(int32 X=1;X<Width-1;++X)
        if(Old[Y*Width+X-1] || Old[Y*Width+X+1] || Old[(Y-1)*Width+X] || Old[(Y+1)*Width+X])Mask[Y*Width+X]=true;
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
bool AERHitFidelityProbe::Moving() const {return Sample>=Still;}
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
    if(++Sample>=Total)
    {
        UE_LOG(LogTemp,Display,TEXT("ER_HIT_FIDELITY_COMPLETE samples=%d"),Total);
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
        Synthetic.Frames.Add(Old);
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
        const bool Valid=Rewound && !Current && !Short && Unchanged && Wound && HistoricalHit.BoneName!=NAME_None;
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
    const auto ToGrid=[&](const FVector& P){return FVector2D((FVector::DotProduct(P-Feet,Across)+Width*Cell*.5)/Cell,((P-Feet).Z+20)/Cell);};
    const auto Index=[](int32 X,int32 Y){return Y*Width+X;};
    TArray<bool> Visible,Hit;Visible.SetNumZeroed(Width*Height);Hit.SetNumZeroed(Width*Height);
    for(int32 T=0;T<Triangles.Num();T+=3)
    {
        const FVector2D A=ToGrid(World[Triangles[T]]),B=ToGrid(World[Triangles[T+1]]),C=ToGrid(World[Triangles[T+2]]);
        const double Area=(B.X-A.X)*(C.Y-A.Y)-(B.Y-A.Y)*(C.X-A.X);if(FMath::IsNearlyZero(Area))continue;
        const int32 X0=FMath::Max(0,FMath::FloorToInt32(FMath::Min3(A.X,B.X,C.X))),X1=FMath::Min(Width-1,FMath::CeilToInt32(FMath::Max3(A.X,B.X,C.X)));
        const int32 Y0=FMath::Max(0,FMath::FloorToInt32(FMath::Min3(A.Y,B.Y,C.Y))),Y1=FMath::Min(Height-1,FMath::CeilToInt32(FMath::Max3(A.Y,B.Y,C.Y)));
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
    for(int32 Y=0;Y<Height;++Y)for(int32 X=0;X<Width;++X)
    {
        const FVector Point=Feet+Across*((X+.5)*Cell-Width*Cell*.5)+FVector(0,0,(Y+.5)*Cell-20);
        FHitResult Ray;
        Hit[Index(X,Y)]=CastChecked<UERPostureMesh>(Body)->ShotHistory.Trace(Body,Ray,Point+Along*500,Point-Along*500,Query,
            GetWorld()->GetTimeSeconds(),FERShotHistory::EdgeTolerance);
        const bool V=Visible[Index(X,Y)],H=Hit[Index(X,Y)];
        Both+=V && H;VisibleOnly+=V && !H;HitOnly+=H && !V;
        if(V && Y*Cell-20<20){++FeetVisible;FeetMissed+=!H;}
        FHitResult Sweep;
        const bool Near3=H || Body->SweepComponent(Sweep,Point+Along*500,Point-Along*500,FQuat::Identity,FCollisionShape::MakeSphere(3.f));
        const bool Near5=Near3 || Body->SweepComponent(Sweep,Point+Along*500,Point-Along*500,FQuat::Identity,FCollisionShape::MakeSphere(5.f));
        Margin3Missed+=V && !Near3;Margin3Phantom+=Near3 && !V;Margin5Missed+=V && !Near5;Margin5Phantom+=Near5 && !V;
    }
    // A band of 3 cells (6cm) separates honest capsule fit from a shifted hit volume.
    TArray<bool> NearHit=Hit,NearVisible=Visible;
    for(int32 I=0;I<3;++I){Dilate(NearHit);Dilate(NearVisible);}
    int32 DeepMiss=0,DeepPhantom=0;
    for(int32 I=0;I<Width*Height;++I){DeepMiss+=Visible[I] && !NearHit[I];DeepPhantom+=Hit[I] && !NearVisible[I];}
    const int32 Shown=FMath::Max(1,Both+VisibleOnly);
    const int32 Hits=FMath::Max(1,Both+HitOnly);
    UE_LOG(LogTemp,Display,TEXT("ER_HIT_FIDELITY_VIEW sample=%d skin=%d slot=%d posture=%s moving=%d view=%s visible_cells=%d hit_cells=%d missed_pct=%.1f phantom_pct=%.1f deep_miss_pct=%.1f deep_phantom_pct=%.1f feet_visible=%d feet_missed_pct=%.1f margin3_missed=%.1f margin3_phantom=%.1f margin5_missed=%.1f margin5_phantom=%.1f"),
        Sample,SkinOf(Sample),SlotOf(Sample),PostureName(PostureOf(Sample)),Moving(),ViewName,Both+VisibleOnly,Both+HitOnly,
        100.f*VisibleOnly/Shown,100.f*HitOnly/Hits,100.f*DeepMiss/Shown,100.f*DeepPhantom/Hits,FeetVisible,100.f*FeetMissed/FMath::Max(1,FeetVisible),
        100.f*Margin3Missed/Shown,100.f*Margin3Phantom/Shown,100.f*Margin5Missed/Shown,100.f*Margin5Phantom/Shown);
    if(!bImage)return;
    TArray<FColor> Pixels;Pixels.SetNumUninitialized(Width*Height);
    for(int32 Y=0;Y<Height;++Y)for(int32 X=0;X<Width;++X)
    {
        const bool V=Visible[Index(X,Height-1-Y)],H=Hit[Index(X,Height-1-Y)];
        Pixels[Index(X,Y)]=V && H ? FColor(150,150,150):V ? FColor(230,60,50):H ? FColor(40,110,240):FColor(250,250,250);
    }
    const FString Path=FPaths::ProjectSavedDir()/FString::Printf(TEXT("Testing/HitFidelity/skin%02d-%s-%s-%s.bmp"),SkinOf(Sample),PostureName(PostureOf(Sample)),Moving() ? TEXT("moving"):TEXT("still"),ViewName);
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path),true);
    FFileHelper::CreateBitmap(*Path,Width,Height,Pixels.GetData());
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
        if(!Shooter)return;
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
    if(!Me || !Me->Weapon || Age<9)return;
    if(!Ready)
    {
        for(TActorIterator<AERCharacter> It(GetWorld());It;++It)if(*It!=Me)Subject=*It;
        if(!Subject)return;
        Me->GetCharacterMovement()->PrimaryComponentTick.AddPrerequisite(this,PrimaryActorTick);
        Ready=true;NextShot=Age+1;
    }
    // Steady tracking: a real shooter's view keeps turning while the trigger breaks.
    const float Rate=120.f;
    FRotator View=PC->GetControlRotation();View.Pitch=0;View.Yaw=FRotator::NormalizeAxis(View.Yaw+Rate*GetWorld()->GetDeltaSeconds());
    PC->SetControlRotation(View);
    if(Age<NextShot)return;
    NextShot=Age+.75f;
    const FVector Seen=Subject->GetMesh()->GetBoneLocation(TEXT("spine_03"));
    UE_LOG(LogTemp,Display,TEXT("ER_HIT_LAG_CLIENT shot=%d target=%s yaw=%.3f dt=%.4f"),Shots++,*Seen.ToCompactString(),View.Yaw,GetWorld()->GetDeltaSeconds());
    CastChecked<UERMovement>(Me->GetCharacterMovement())->SendMoveThisFrame();
    Me->ServerStartFire();Me->ServerStopFire();
}
