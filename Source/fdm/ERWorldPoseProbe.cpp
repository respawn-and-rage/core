#include "ERWorldPoseProbe.h"
#include "ERCharacter.h"
#include "ERWeapon.h"
#include "ERPostureMesh.h"
#include "ERWorldWeaponPose.h"
#include "ERMovement.h"
#include "ERMatch.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkeletalMeshSocket.h"
#include "Engine/StaticMeshActor.h"
#include "Components/StaticMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "Materials/MaterialInterface.h"
#include "Engine/PointLight.h"
#include "Components/PointLightComponent.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/HUD.h"
#include "Net/UnrealNetwork.h"
#include "Misc/Paths.h"
#include "Engine/GameViewportClient.h"
#include "EngineUtils.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Rendering/SkinWeightVertexBuffer.h"
#include "BoneWeights.h"
namespace
{
double LowestVisibleZ(USkeletalMeshComponent* Body,double* CoreFloor=nullptr,FBox* VisibleBounds=nullptr)
{
    const auto* Asset=Body->GetSkeletalMeshAsset();
    const auto& LOD=Asset->GetResourceForRendering()->LODRenderData[0];
    const auto& Ref=Asset->GetRefSkeleton();
    TArray<FMatrix44f> Matrices;Body->CacheRefToLocalMatrices(Matrices);
    const auto* Character=Cast<AERCharacter>(Body->GetOwner());
    if(Character)for(const FName BoneName:Character->GetTeamAppearance().HiddenBones)
    {
        const int32 Root=Ref.FindBoneIndex(BoneName);if(Root==INDEX_NONE)continue;
        for(int32 I=0;I<Ref.GetNum();++I)if(I==Root || Ref.BoneIsChildOf(I,Root))
            Matrices[I].SetIdentity(),Matrices[I].M[0][0]=Matrices[I].M[1][1]=Matrices[I].M[2][2]=0;
    }
    TArray<FVector3f> Points;Points.SetNumZeroed(LOD.GetNumVertices());
    TArray<bool> Core;Core.SetNumZeroed(Points.Num());
    const int32 LimbRoots[]={Ref.FindBoneIndex(TEXT("upperarm_l")),Ref.FindBoneIndex(TEXT("upperarm_r")),Ref.FindBoneIndex(TEXT("foot_l")),Ref.FindBoneIndex(TEXT("foot_r"))};
    auto CoreBone=[&](int32 Bone)
    {
        for(int32 Root:LimbRoots)if(Root!=INDEX_NONE && (Bone==Root || Ref.BoneIsChildOf(Bone,Root)))return false;
        return true;
    };
    if(CoreFloor)*CoreFloor=DBL_MAX;
    if(VisibleBounds)*VisibleBounds=FBox(ForceInit);
    const bool MeasureContacts=FParse::Param(FCommandLine::Get(),TEXT("ERProneContacts"));
    TArray<int32> ContactGroups;
    if(MeasureContacts)ContactGroups.Init(INDEX_NONE,Points.Num());
    const int32 Roots[]={Ref.FindBoneIndex(TEXT("foot_l")),Ref.FindBoneIndex(TEXT("foot_r")),
        Ref.FindBoneIndex(TEXT("thigh_l")),Ref.FindBoneIndex(TEXT("thigh_r")),
        Ref.FindBoneIndex(TEXT("lowerarm_l")),Ref.FindBoneIndex(TEXT("lowerarm_r"))};
    double Contacts[]={DBL_MAX,DBL_MAX,DBL_MAX,DBL_MAX,DBL_MAX,DBL_MAX};
    const auto* Weights=Body->GetSkinWeightBuffer(0);
    // Use the cached matrices explicitly: they include the hidden-bone collapse.
    // The engine's single-vertex convenience overload evaluates live matrices.
    for(const auto& Section:LOD.RenderSections)
        for(uint32 Vertex=0;Vertex<Section.NumVertices;++Vertex)
        {
            const uint32 BufferVertex=Section.GetVertexBufferIndex()+Vertex;
            const FVector3f Position=LOD.StaticVertexBuffers.PositionVertexBuffer.VertexPosition(BufferVertex);
            FVector3f Skinned=FVector3f::ZeroVector;float Strongest=0;int32 Dominant=0;
            for(uint32 Influence=0;Influence<Weights->GetMaxBoneInfluences();++Influence)
            {
                const float Weight=Weights->GetBoneWeight(BufferVertex,Influence)*UE::AnimationCore::InvMaxRawBoneWeightFloat;
                if(Weight<=0)continue;
                const int32 Bone=Section.BoneMap[Weights->GetBoneIndex(BufferVertex,Influence)];
                if(Weight>Strongest){Strongest=Weight;Dominant=Bone;}
                Skinned+=FVector3f(Matrices[Bone].TransformPosition(Position))*Weight;
            }
            Points[Section.BaseVertexIndex+Vertex]=Skinned;Core[Section.BaseVertexIndex+Vertex]=CoreBone(Dominant);
            if(MeasureContacts)
            {
                for(int32 Group=0;Group<UE_ARRAY_COUNT(Roots);++Group)
                    if(Roots[Group]!=INDEX_NONE && (Dominant==Roots[Group] || Ref.BoneIsChildOf(Dominant,Roots[Group])))
                    {ContactGroups[Section.BaseVertexIndex+Vertex]=Group;break;}
                const FName Name=Ref.GetBoneName(Dominant);
                if(Name==TEXT("knee_pad_l") || Name==TEXT("kneepad_l"))ContactGroups[Section.BaseVertexIndex+Vertex]=2;
                if(Name==TEXT("knee_pad_r") || Name==TEXT("kneepad_r"))ContactGroups[Section.BaseVertexIndex+Vertex]=3;
            }
        }
    if(Points.IsEmpty())return -DBL_MAX;
    TArray<uint32> Indices;LOD.MultiSizeIndexContainer.GetIndexBuffer(Indices);
    double Lowest=DBL_MAX;
    for(const auto& Section:LOD.RenderSections)
    {
        if(!Body->IsMaterialSectionShown(Section.MaterialIndex,0))continue;
        for(uint32 T=Section.BaseIndex;T<Section.BaseIndex+Section.NumTriangles*3;T+=3)
        {
            if(Points[Indices[T]].IsNearlyZero(.001f) || Points[Indices[T+1]].IsNearlyZero(.001f) || Points[Indices[T+2]].IsNearlyZero(.001f))continue;
            for(uint32 Corner=0;Corner<3;++Corner)
            {
                const uint32 Vertex=Indices[T+Corner];
                const FVector Point=Body->GetComponentTransform().TransformPosition(FVector(Points[Vertex]));
                if(MeasureContacts && ContactGroups[Vertex]!=INDEX_NONE)
                {
                    const int32 Group=ContactGroups[Vertex];
                    if(Group<4 || FVector::DistSquared(Point,Body->GetBoneLocation(Ref.GetBoneName(Roots[Group])))<400)
                        Contacts[Group]=FMath::Min(Contacts[Group],Point.Z);
                }
                Lowest=FMath::Min(Lowest,Point.Z);
                if(CoreFloor && Core[Vertex])*CoreFloor=FMath::Min(*CoreFloor,Point.Z);
                if(VisibleBounds)*VisibleBounds+=Point;
            }
        }
    }
    if(MeasureContacts)UE_LOG(LogTemp,Display,TEXT("ER_PRONE_CONTACT mesh=%s toe=%.3f,%.3f knee=%.3f,%.3f elbow=%.3f,%.3f floor=%.3f"),
        *Asset->GetPathName(),Contacts[0],Contacts[1],Contacts[2],Contacts[3],Contacts[4],Contacts[5],Lowest);
    return Lowest;
}
void BuildProneFixture(UWorld* World,bool WithWall)
{
    auto Block=[&](FVector Position,FVector Scale,const TCHAR* Material)
    {
        auto* Obstacle=World->SpawnActor<AStaticMeshActor>(Position,FRotator::ZeroRotator);
        auto* Part=Obstacle->GetStaticMeshComponent();
        Part->SetMobility(EComponentMobility::Movable);
        Part->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));
        Part->SetMaterial(0,LoadObject<UMaterialInterface>(nullptr,Material));
        Part->SetCollisionProfileName(TEXT("BlockAll"));Obstacle->SetActorScale3D(Scale);
    };
    Block(FVector(30000,0,-10),FVector(30,30,.2),TEXT("/Game/EclipseRecoil/Art/MI_Floor.MI_Floor"));
    if(FParse::Param(FCommandLine::Get(),TEXT("ERWorldPoseSideWall")))Block(FVector(30000,60,100),FVector(4,.1,2),TEXT("/Game/EclipseRecoil/Art/MI_Wall.MI_Wall"));
    if(WithWall)Block(FVector(30150,0,100),FVector(.1,4,2),TEXT("/Game/EclipseRecoil/Art/MI_Wall.MI_Wall"));
}
bool ProneFixtureReady(UWorld* World,bool WithWall)
{
    FHitResult Floor,Wall;FCollisionQueryParams Query(SCENE_QUERY_STAT(ERProneFixture),false);
    const bool FloorReady=World->LineTraceSingleByChannel(Floor,FVector(30000,500,100),FVector(30000,500,-100),ECC_Pawn,Query) && FMath::Abs(Floor.ImpactPoint.Z)<.1;
    const bool WallReady=!WithWall || (World->LineTraceSingleByChannel(Wall,FVector(30100,100,100),FVector(30200,100,100),ECC_Pawn,Query) && FMath::Abs(Wall.ImpactPoint.X-30145)<.1);
    const bool SideReady=!FParse::Param(FCommandLine::Get(),TEXT("ERWorldPoseSideWall")) ||
        (World->LineTraceSingleByChannel(Wall,FVector(30000,30,100),FVector(30000,100,100),ECC_Pawn,Query) && FMath::Abs(Wall.ImpactPoint.Y-55)<.1);
    return FloorReady && WallReady && SideReady;
}
constexpr int32 Cases=13,Total=20*Cases;
bool ProneGallery(){return FParse::Param(FCommandLine::Get(),TEXT("ERWorldPoseProne"));}
const int32 Slots[]={1,2,3,4,5,6,7,4,5,6,7,5,5};
bool AllGallery(){return FParse::Param(FCommandLine::Get(),TEXT("ERWorldPoseAllGallery"));}
bool HandDetail(){return FParse::Param(FCommandLine::Get(),TEXT("ERWorldPoseHands"));}
int32 Override(const TCHAR* Key){int32 Value=INDEX_NONE;FParse::Value(FCommandLine::Get(),Key,Value);return Value;}
bool SingleGallery(){return Override(TEXT("ERWorldPoseSkin="))!=INDEX_NONE && Override(TEXT("ERWorldPoseCase="))!=INDEX_NONE;}
int32 ProbeSkin(int32 Sample){const int32 Value=Override(TEXT("ERWorldPoseSkin="));return Value!=INDEX_NONE ? Value:AllGallery() ? Sample:Sample/Cases;}
int32 ProbeCase(int32 Sample){const int32 Value=Override(TEXT("ERWorldPoseCase="));return Value!=INDEX_NONE ? Value:AllGallery() ? 3:Sample%Cases;}
int32 ProbeSlot(int32 Sample){const int32 Item=Override(TEXT("ERWorldPoseItem="));return Item!=INDEX_NONE ? Item:Slots[ProbeCase(Sample)];}
int32 ProbeTotal(){return SingleGallery() ? 1:AllGallery() ? 20:FParse::Param(FCommandLine::Get(),TEXT("ERWorldPoseQuick")) ? Cases:Total;}
}
AERWorldPoseProbe::AERWorldPoseProbe()
{PrimaryActorTick.bCanEverTick=true;PrimaryActorTick.TickGroup=TG_PostUpdateWork;bReplicates=true;bAlwaysRelevant=true;SetNetUpdateFrequency(30);}
void AERWorldPoseProbe::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{Super::GetLifetimeReplicatedProps(OutLifetimeProps);DOREPLIFETIME(AERWorldPoseProbe,Subject);DOREPLIFETIME(AERWorldPoseProbe,Sample);}
void AERWorldPoseProbe::Configure()
{
    if(ProneGallery())
    {
        Subject->GetMesh()->SetForcedLOD(1);
        auto* Movement=Cast<UERMovement>(Subject->GetCharacterMovement());
        Movement->Locomotion.ProneBlend=1;Movement->Locomotion.HalfHeight=34;
        Subject->GetCapsuleComponent()->SetCapsuleHalfHeight(34);
        Subject->SetActorLocation(FVector(30000,0,36.15));Movement->ApplyPosturePresentation();
    }
    auto* State=Subject->GetPlayerState<AERPlayerState>();
    const int32 Skin=ProbeSkin(Sample),Case=ProbeCase(Sample);State->Team=Skin<10 ? EERTeam::Alpha:EERTeam::Omega;State->ActiveSkin=Skin%10;State->ForceNetUpdate();
    Subject->ServerSelectWeapon(ProbeSlot(Sample));
    if(Subject->Weapon){Subject->Weapon->ScopeLevel=Case>=7 ? 1:0;Subject->Weapon->ForceNetUpdate();}
    if(Subject->GetController())Subject->GetController()->SetControlRotation(FRotator(Case==11 ? 35:Case==12 ? -35:0,0,0));
    if(ProneGallery() && FParse::Param(FCommandLine::Get(),TEXT("ERWorldPoseWall")))
    {
        auto* Movement=Cast<UERMovement>(Subject->GetCharacterMovement());
        Subject->SetActorLocation(FVector(29800,0,36.15));
        FHitResult Cover;
        Movement->MoveUpdatedComponent(FVector(360,0,0),Subject->GetActorQuat(),true,&Cover);
        if(!Cover.bBlockingHit || Subject->GetActorLocation().X<=29801 || Subject->GetActorLocation().X>=30110)
        {
            UE_LOG(LogTemp,Error,TEXT("ER_WORLD_POSE_FAIL sample=%d wall approach did not stop at weapon clearance"),Sample);
            Failed=true;
        }
    }
    Subject->ForceNetUpdate();Changed=Age;View=0;Captured=0;
}
bool AERWorldPoseProbe::CheckPose(bool Client)
{
    if(!Subject || !Subject->Weapon || !Subject->Weapon->Data)
    {UE_LOG(LogTemp,Error,TEXT("ER_WORLD_POSE_FAIL sample=%d missing subject, weapon, or data client=%d"),Sample,Client);Failed=true;return false;}
    if(ProneGallery() && !ProneFixtureReady(GetWorld(),FParse::Param(FCommandLine::Get(),TEXT("ERWorldPoseWall"))))
    {UE_LOG(LogTemp,Error,TEXT("ER_WORLD_POSE_FAIL sample=%d missing collision fixture client=%d"),Sample,Client);Failed=true;return false;}
    const auto* D=Subject->Weapon->Data.Get();auto* Body=Subject->GetMesh();const auto* State=Subject->GetPlayerState<AERPlayerState>();
    const int32 Skin=ProbeSkin(Sample),Case=ProbeCase(Sample);
    if(!State || State->ActiveSkin!=Skin%10 || (State->Team==EERTeam::Alpha)!=(Skin<10))
    {UE_LOG(LogTemp,Error,TEXT("ER_WORLD_POSE_FAIL sample=%d replicated skin mismatch client=%d"),Sample,Client);Failed=true;return false;}
    const auto* Defaults=Subject->GetClass()->GetDefaultObject<AERCharacter>();
    if(!Defaults->ReferenceArsenal.IsValidIndex(ProbeSlot(Sample)) || D!=Defaults->ReferenceArsenal[ProbeSlot(Sample)])
    {UE_LOG(LogTemp,Error,TEXT("ER_WORLD_POSE_FAIL sample=%d replicated weapon mismatch client=%d"),Sample,Client);Failed=true;return false;}
    const auto Gun=Subject->WorldRifle->GetComponentTransform();
    if(!D->bWorldReferencePose)
    {
        const FTransform Expected=D->GripTransform*Body->GetSocketTransform(TEXT("HandGrip_R"));
        const auto Bounds=D->Mesh->GetBounds();
        FCollisionQueryParams Query(SCENE_QUERY_STAT(ERItemPoseProbe),false,Subject);
        FBox BodyBounds(ForceInit);
        const double Floor=ProneGallery() ? LowestVisibleZ(Body,nullptr,&BodyBounds):0;
        const bool BodyClear=!ProneGallery() || !GetWorld()->OverlapBlockingTestByChannel(BodyBounds.GetCenter(),FQuat::Identity,ECC_Pawn,FCollisionShape::MakeBox(BodyBounds.GetExtent()),Query);
        const bool Attached=Expected.Equals(Gun,.01f);
        const bool WeaponClear=!GetWorld()->OverlapBlockingTestByChannel(Gun.TransformPosition(Bounds.Origin),Gun.GetRotation(),ECC_Pawn,
            FCollisionShape::MakeBox(Bounds.BoxExtent*Gun.GetScale3D().GetAbs()),Query);
        const bool Good=Attached && Floor>=-.5 && BodyClear && WeaponClear && Subject->GetTeamAppearance().Matches(Body);
        UE_LOG(LogTemp,Display,TEXT("ER_WORLD_POSE_%s sample=%d skin=%d item=%d floor=%.3f body=%d weapon=%d attached=%d client=%d"),
            Good ? TEXT("PASS"):TEXT("FAIL"),Sample,Skin,ProbeSlot(Sample),Floor,BodyClear,WeaponClear,Attached,Client);
        Failed|=!Good;return Good;
    }
    const auto& CatalogSkin=ERSkins::Select(Skin<10 ? Defaults->AlphaSkins:Defaults->OmegaSkins,Skin<10 ? Defaults->AlphaAppearance:Defaults->OmegaAppearance,Skin%10);
    const FVector ExpectedSkinSeat=CatalogSkin.WorldSupportPalmOffsetCm;
    const bool SkinSeat=Subject->GetTeamAppearance().WorldSupportPalmOffsetCm.Equals(ExpectedSkinSeat,.001f);
    const FVector ExpectedSupport=D->bOneHanded ? D->WorldPistolSupportContact:
        D->WorldLeftPalmContact+ExpectedSkinSeat/D->WorldWeaponScale;
    const float R=FVector::Distance(Body->GetSocketLocation(TEXT("ER_Palm_R")),Gun.TransformPosition(D->WorldRightPalmContact));
    const FVector Left=Body->GetSocketLocation(TEXT("ER_Palm_L"));float L=MAX_flt;
    L=FVector::Distance(Left,Gun.TransformPosition(ExpectedSupport));
    const float Dot=FVector::DotProduct(Gun.GetUnitAxis(EAxis::X),Subject->GetBaseAimRotation().Vector());
    const FQuat ExpectedPalm=Gun.GetRotation()*D->WorldRightPalmRotation.Quaternion();
    const FQuat ActualPalm=Body->GetSocketTransform(TEXT("ER_Palm_R")).GetRotation();
    const float WristAngle=FMath::RadiansToDegrees(ExpectedPalm.AngularDistance(ActualPalm));
    const FVector LeftForearm=(Body->GetSocketLocation(TEXT("hand_l"))-Body->GetSocketLocation(TEXT("lowerarm_l"))).GetSafeNormal();
    const float LeftContinuation=FVector::DotProduct(LeftForearm,Body->GetSocketTransform(TEXT("ER_Palm_L")).GetUnitAxis(EAxis::X));
    const float ElbowHeight=Gun.GetLocation().Z-Body->GetSocketLocation(TEXT("lowerarm_r")).Z;
    const bool Scale=Gun.GetScale3D().Equals(FVector(D->WorldWeaponScale),.001);
    bool Clearance=true;
    if(ProneGallery())
    {
        const float ElbowZ=FMath::Min(Body->GetSocketLocation(TEXT("lowerarm_r")).Z,Body->GetSocketLocation(TEXT("lowerarm_l")).Z);
        double CoreFloor=0;FBox BodyBounds(ForceInit);
        const double VertexFloor=LowestVisibleZ(Body,&CoreFloor,&BodyBounds);
        UE_LOG(LogTemp,Display,TEXT("ER_PRONE_GEOMETRY sample=%d skin=%d mesh=%s floor=%.4f core=%.4f client=%d"),Sample,Skin,*Body->GetSkeletalMeshAsset()->GetPathName(),VertexFloor,CoreFloor,Client);
        const auto Bounds=D->Mesh->GetBounds();
        FCollisionQueryParams Query(SCENE_QUERY_STAT(ERProneWeaponProbe),false,Subject);
        const bool BodyClear=BodyBounds.IsValid!=0 && !GetWorld()->OverlapBlockingTestByChannel(BodyBounds.GetCenter(),FQuat::Identity,ECC_Pawn,FCollisionShape::MakeBox(BodyBounds.GetExtent()),Query);
        const bool WeaponClear=!GetWorld()->OverlapBlockingTestByChannel(Gun.TransformPosition(Bounds.Origin),Gun.GetRotation(),ECC_Pawn,FCollisionShape::MakeBox(Bounds.BoxExtent*Gun.GetScale3D().GetAbs()),Query);
        Clearance=ElbowZ>=0 && (FParse::Param(FCommandLine::Get(),TEXT("ERProneCalibration")) || (VertexFloor>=-.5 && BodyClear)) && WeaponClear;
        if(!Clearance)UE_LOG(LogTemp,Display,TEXT("ER_PRONE_OBSTRUCTION sample=%d body=%d weapon=%d bounds=%s"),Sample,BodyClear,WeaponClear,*BodyBounds.ToString());
        UE_LOG(LogTemp,Display,TEXT("ER_PRONE_CLEARANCE sample=%d elbows=%.3f clear=%d client=%d"),Sample,ElbowZ,Clearance,Client);
    }
    const bool Good=R<.5f && L<1.5f && (Dot>.995 || (ProneGallery() && !FParse::Param(FCommandLine::Get(),TEXT("ERWorldPoseWall")))) && WristAngle<1.f && (ProneGallery() || D->bOneHanded || LeftContinuation>0.f) && Scale && SkinSeat && Clearance && D->bWorldReferencePose && Subject->GetTeamAppearance().Matches(Body);
    if(!Good && !Client)
    {
        const FVector Shoulder=Body->GetSocketLocation(TEXT("upperarm_l")),Elbow=Body->GetSocketLocation(TEXT("lowerarm_l"));
        const FVector Target=Gun.TransformPosition(ExpectedSupport);
        const float Reach=FVector::Distance(Shoulder,Elbow)+FVector::Distance(Elbow,Left);
        UE_LOG(LogTemp,Display,TEXT("ER_WORLD_POSE_REACH sample=%d shoulder=%s target=%s hand=%s reach=%.3f target_distance=%.3f"),
            Sample,*Shoulder.ToString(),*Target.ToString(),*Left.ToString(),Reach,FVector::Distance(Shoulder,Target));
    }
    UE_LOG(LogTemp,Display,TEXT("ER_WORLD_POSE_%s sample=%d skin=%d case=%d weapon=%s right=%.3f left=%.3f wrist_angle=%.3f left_continuation=%.3f elbow_height=%.3f aim=%.5f scale=%.3f client=%d"),Good ? TEXT("PASS"):TEXT("FAIL"),Sample,Skin,Case,*D->DisplayName,R,L,WristAngle,LeftContinuation,ElbowHeight,Dot,Gun.GetScale3D().X,Client);
    Failed|=!Good;return Good;
}
void AERWorldPoseProbe::Capture(int32 Angle)
{
    auto* PC=UGameplayStatics::GetPlayerController(GetWorld(),0);if(!PC)return;
    if(!Camera)Camera=GetWorld()->SpawnActor<ACameraActor>();
    const FVector Centre=HandDetail() ? Subject->GetMesh()->GetSocketLocation(TEXT("ER_Palm_L")):Subject->GetActorLocation()+(ProneGallery() ? FVector(35,0,0):FVector(10,0,SingleGallery() ? 42:15));
    const FVector Views[]={HandDetail() ? FVector(130,100,70):FVector(270,210,40),HandDetail() ? FVector(170,0,50):FVector(320,0,20),HandDetail() ? FVector(0,160,50):SingleGallery() ? FVector(0,300,20):FVector(-240,-210,50),HandDetail() ? FVector(35,10,175):FVector(70,25,390)};
    Camera->SetActorLocation(Centre+Views[FMath::Clamp(Angle,0,3)]);
    Camera->SetActorRotation((Centre-Camera->GetActorLocation()).Rotation());Camera->GetCameraComponent()->FieldOfView=ProneGallery() ? 60:HandDetail() ? 24:SingleGallery() ? 28:42;PC->SetViewTarget(Camera);
    Captured=Age;
}
void AERWorldPoseProbe::Tick(float Delta)
{
    Super::Tick(Delta);Age+=Delta;
    if(!HasAuthority())
    {
        if(ProneGallery() && !Ready)
        {
            BuildProneFixture(GetWorld(),FParse::Param(FCommandLine::Get(),TEXT("ERWorldPoseWall")));
            Ready=true;
        }
        if(Sample>=0 && Sample<ProbeTotal() && Sample!=Seen && Subject && Age>3)
        {
            if(Changed!=Sample || Captured==0){Changed=Sample;Captured=Age;}
            const auto* State=Subject->GetPlayerState<AERPlayerState>();const auto* D=Subject->Weapon ? Subject->Weapon->Data.Get():nullptr;
            const auto* Defaults=Subject->GetClass()->GetDefaultObject<AERCharacter>();const int32 Skin=ProbeSkin(Sample),Slot=ProbeSlot(Sample);
            const bool Replicated=State && State->ActiveSkin==Skin%10 && (State->Team==EERTeam::Alpha)==(Skin<10) &&
                D && Defaults->ReferenceArsenal.IsValidIndex(Slot) && D==Defaults->ReferenceArsenal[Slot];
            if(Replicated && Subject->GetTeamAppearance().Matches(Subject->GetMesh()) && Age-Captured>.35f && CheckPose(true))Seen=Sample;
        }
        return;
    }
    if(!Ready)
    {
        if(FParse::Param(FCommandLine::Get(),TEXT("ERWorldPoseNetwork")))
        {
            int32 Players=0;for(auto It=GetWorld()->GetPlayerControllerIterator();It;++It)++Players;
            if(Players<2)return;
            if(NetworkReadyAt==0)NetworkReadyAt=Age;
            if(Age-NetworkReadyAt<4)return;
        }
        auto* PC=UGameplayStatics::GetPlayerController(GetWorld(),0);Subject=PC ? Cast<AERCharacter>(PC->GetPawn()):nullptr;
        if(FParse::Param(FCommandLine::Get(),TEXT("ERWorldPoseExternal")))
            for(TActorIterator<AERCharacter> It(GetWorld());It;++It)if(*It!=Subject && It->Weapon){Subject=*It;break;}
        if(!Subject || !Subject->Weapon || Age<5)return;
        Subject->bAlwaysRelevant=true;Subject->TeleportTo(FVector(30000,0,92.5),FRotator::ZeroRotator,false,true);Subject->GetCharacterMovement()->DisableMovement();
        PC->SetIgnoreMoveInput(true);PC->SetIgnoreLookInput(true);if(PC->GetHUD())PC->GetHUD()->bShowHUD=false;
        Subject->FirstPersonBody->SetHiddenInGame(true);Subject->FirstPersonRifle->SetHiddenInGame(true);Subject->FirstPersonSight->SetHiddenInGame(true);
        Subject->GetMesh()->SetOwnerNoSee(false);Subject->WorldRifle->SetOwnerNoSee(false);Subject->WorldSight->SetOwnerNoSee(false);
        if(ProneGallery())BuildProneFixture(GetWorld(),FParse::Param(FCommandLine::Get(),TEXT("ERWorldPoseWall")));
        if(FApp::CanEverRender())
        {
            auto* Cube=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube"));
            for(int32 I=0;I<2;++I)
            {
                if(ProneGallery())continue;
                auto* Back=GetWorld()->SpawnActor<AStaticMeshActor>(FVector(30000-(I ? 100:0),0,I ? 180:-10),FRotator::ZeroRotator);
                Back->GetStaticMeshComponent()->SetStaticMesh(Cube);Back->GetStaticMeshComponent()->SetMaterial(0,LoadObject<UMaterialInterface>(nullptr,TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial")));
                Back->SetActorScale3D(I ? FVector(.1,30,8):FVector(30,30,.2));Back->GetStaticMeshComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            }
            for(float Side:{-1.f,1.f})
            {auto* Light=GetWorld()->SpawnActor<APointLight>(FVector(30300,Side*240,310),FRotator::ZeroRotator);Light->SetMobility(EComponentMobility::Movable);Light->PointLightComponent->SetIntensity(20000);Light->PointLightComponent->SetAttenuationRadius(1200);Light->PointLightComponent->SetCastShadows(true);}
        }
        Ready=true;Sample=0;Configure();return;
    }
    const int32 TestTotal=ProbeTotal();
    if(Sample>=TestTotal)
    {if(Age-Changed>4){UE_LOG(LogTemp,Display,TEXT("ER_WORLD_POSE_COMPLETE result=%s samples=%d"),Failed ? TEXT("FAIL"):TEXT("PASS"),TestTotal);FPlatformMisc::RequestExitWithStatus(false,Failed ? 1:0);}return;}
    if(AdvanceAt>0)
    {
        if(Age-AdvanceAt<.1f)return;
        AdvanceAt=0;++Sample;Changed=Age;ForceNetUpdate();if(Sample<TestTotal)Configure();return;
    }
    const bool Gallery=FApp::CanEverRender() && FParse::Param(FCommandLine::Get(),TEXT("ERWorldPoseGallery"));
    const int32 Skin=ProbeSkin(Sample),Case=ProbeCase(Sample);
    const bool Shot=Gallery && (SingleGallery() || AllGallery() || Skin==0 || Skin==5 || Skin==10 || Skin==15);
    if(Age-Changed<(ProneGallery() && GetNetMode()==NM_Standalone ? 1.25f:GetNetMode()==NM_Standalone ? .4f:.75f))return;
    if(ProneGallery() && Subject->GetMesh()->GetSkeletalMeshAsset()->GetResourceForRendering()->LODRenderData[0].GetNumVertices()==0)
    {
        Subject->GetMesh()->SetForcedLOD(1);
        if(Age-Changed>10){UE_LOG(LogTemp,Error,TEXT("ER_WORLD_POSE_FAIL sample=%d missing resident LOD0"),Sample);FPlatformMisc::RequestExitWithStatus(false,1);}
        return;
    }
    if(Seen!=Sample){CheckPose(false);Seen=Sample;}
    if(Shot)
    {
        if(Captured==0){Capture(View);return;}
        if(Age-Captured<.6f)return;
        FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/FString::Printf(TEXT("Testing/WorldPoses/Skin%02d-Case%02d-View%d%s.png"),Skin,Case,View,HandDetail() ? TEXT("-Hands"):TEXT("")),false,false);
        if(++View<(AllGallery() || SingleGallery() ? 4:2)){Captured=0;Changed=Age;return;}
        AdvanceAt=Age;return;
    }
    ++Sample;Changed=Age;ForceNetUpdate();if(Sample<TestTotal)Configure();
}
